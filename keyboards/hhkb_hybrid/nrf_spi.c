// SPDX-License-Identifier: MIT
// SPI1 slave link to the nRF52832 Bluetooth module (stock nRF firmware).
//
// Protocol (docs/reverse-engineering/bluetooth-protocol.md): the nRF is the SPI master (mode 3,
// 125 kHz, manual CS). A transaction is a command byte; for queries the nRF
// then clocks 0x00 until it receives 0xAA and reads a fixed number of bytes.
// 0x83/0x85/0x61/0x93 are 2-byte notifications with no answer.
// The STM32 asks for a poll by pulsing PB6 low; PD2 low lets the nRF run.
#include "quantum.h"
#include <hal.h>
#include "nrf_spi.h"

#define NRF_REQUEST_PIN B6
#define NRF_ENABLE_PIN  D2 /* low = nRF running */

/* SPI1 is IRQ 25 on STM32L0: vector table offset 0x40 + 25 * 4 = 0xA4.
   The ChibiOS SPI driver is disabled, so the vector is free. */
#define SPI1_IRQ_NUMBER   25
#define SPI1_IRQ_HANDLER  VectorA4
#define SPI1_IRQ_PRIORITY 1

#define NRF_SYNC           0xAA
#define NRF_CMD_HELLO      0x01
#define NRF_CMD_POLL       0x02
#define NRF_CMD_CONTROL    0x03
#define NRF_CMD_BATTERY    0x05
#define NRF_CMD_SLOT       0x06
#define NRF_HELLO_REPLY    0xE5
#define NRF_POLL_KEYBOARD  0x01 /* 0x02 answer: modifier + 6 keys */
#define NRF_POLL_CONSUMER  0x02 /* 0x02 answer: consumer bitmasks R2..R5 */
#define NRF_POLL_CONTROL   0x40 /* 0x02 answer: "poll 0x03 for a request" */
#define NRF_POLL_SLOT      0x10 /* 0x02 answer: "poll 0x06 for a slot" */
#define NRF_POLL_BATTERY   0x80 /* 0x02 answer: battery level follows (also asked by 0x05) */
#define NRF_CONTROL_PAIR   0x01 /* 0x03 answer: open pairing on slot */
#define NRF_CONTROL_DELETE 0x02 /* 0x03 answer: delete bond on slot */
#define NRF_CONTROL_IDLE   0x10 /* 0x03 answer: stop advertising / disconnect */
#define NRF_NOTIFY_LINK    0x83

#define REQUEST_RETRY_MS 30
#define REQUEST_RETRIES  5

#define TX_MAX 16

static uint8_t tx_buf[TX_MAX];
static uint8_t tx_len;
static uint8_t tx_pos;
static uint8_t param_for; /* pending 2-byte notification (0x83/0x85/0x61/0x93) */

/* Shared with the IRQ handler; written from the main loop under chSysLock(). */
static uint8_t report_mods;
static uint8_t report_keys[6];
static bool    report_pending;
static bool    control_pending;
static bool    control_polled; /* 0x02 answered with control_poll, waiting for 0x03/0x06 */
static uint8_t control_poll;   /* NRF_POLL_CONTROL or NRF_POLL_SLOT */
static uint8_t control_type;
static uint8_t control_value;
static uint8_t consumer_bits[4];
static bool    consumer_pending;
static bool    battery_pending;
static bool    battery_polled; /* 0x02 answered with 0x80, waiting for 0x05 */
static uint8_t battery_level;

static uint16_t last_request_time;
static uint8_t  request_retries;

static bool    hello_seen;   /* nRF sent its 0x01 hello */
static uint8_t last_link;    /* last 0x83 notification value */


static void tx_start(const uint8_t *data, uint8_t len) {
    memcpy(tx_buf, data, len);
    tx_len = len;
    tx_pos = 1;
    SPI1->DR = tx_buf[0];
    SPI1->CR2 |= SPI_CR2_TXEIE;
}

static void store_notification(uint8_t cmd, uint8_t value) {
    if (cmd == NRF_NOTIFY_LINK) {
        last_link = value;
    }
}

static void answer_poll(void) {
    uint8_t reply[9] = {NRF_SYNC};
    if (control_pending) {
        reply[1]       = control_poll;
        control_polled = true;
    } else if (battery_pending) {
        /* The stock STM32 sends 80 00 and the level on 0x05; the nRF also reads byte 1. */
        reply[1]       = NRF_POLL_BATTERY;
        reply[2]       = battery_level;
        battery_polled = true;
    } else if (consumer_pending) {
        reply[1] = NRF_POLL_CONSUMER;
        memcpy(&reply[2], consumer_bits, sizeof(consumer_bits));
        consumer_pending = false;
    } else {
        reply[1] = NRF_POLL_KEYBOARD;
        reply[2] = report_mods;
        memcpy(&reply[3], report_keys, sizeof(report_keys));
        if (report_pending) {
            report_pending = false;
        }
    }
    tx_start(reply, sizeof(reply));
}

static void answer_control(uint8_t cmd) {
    uint8_t expected = control_poll == NRF_POLL_SLOT ? NRF_CMD_SLOT : NRF_CMD_CONTROL;
    if (!control_polled || cmd != expected) {
        return; /* no request of ours: let the nRF time out, like the stock firmware */
    }
    uint8_t reply[3] = {NRF_SYNC, control_type, control_value};
    if (control_poll == NRF_POLL_SLOT) {
        reply[1] = control_value; /* 0x06 reads one byte: the slot */
        reply[2] = 0x00;
    }
    control_polled  = false;
    control_pending = false;
    tx_start(reply, sizeof(reply));
}

static void answer_battery(void) {
    if (!battery_polled) {
        return;
    }
    const uint8_t reply[] = {NRF_SYNC, battery_level, 0x00};
    battery_polled  = false;
    battery_pending = false;
    tx_start(reply, sizeof(reply));
}

static void handle_rx(uint8_t b) {
    if (param_for) {
        store_notification(param_for, b);
        param_for = 0;
        return;
    }
    switch (b) {
        case 0x00: /* poll / padding while the nRF reads an answer */
            return;
        case NRF_NOTIFY_LINK:
        case 0x85:
        case 0x61:
        case 0x93:
            param_for = b;
            return;
        case NRF_CMD_HELLO: {
            static const uint8_t reply[] = {NRF_SYNC, NRF_HELLO_REPLY};
            hello_seen = true;
            tx_start(reply, sizeof(reply));
            break;
        }
        case NRF_CMD_POLL:
            answer_poll();
            break;
        case NRF_CMD_CONTROL:
        case NRF_CMD_SLOT:
            answer_control(b);
            break;
        case NRF_CMD_BATTERY:
            answer_battery();
            break;
        default:
            break;
    }
}

OSAL_IRQ_HANDLER(SPI1_IRQ_HANDLER) {
    OSAL_IRQ_PROLOGUE();

    uint32_t sr = SPI1->SR;
    if (sr & SPI_SR_OVR) {
        /* Clearing OVR: read DR, then SR. The lost byte is not interpreted. */
        (void)SPI1->DR;
        (void)SPI1->SR;
    } else if (sr & SPI_SR_RXNE) {
        handle_rx((uint8_t)SPI1->DR);
    }

    if ((SPI1->CR2 & SPI_CR2_TXEIE) && (SPI1->SR & SPI_SR_TXE)) {
        if (tx_pos < tx_len) {
            SPI1->DR = tx_buf[tx_pos++];
        } else {
            SPI1->DR = 0x00;
            SPI1->CR2 &= ~SPI_CR2_TXEIE;
        }
    }

    OSAL_IRQ_EPILOGUE();
}

void nrf_spi_init(void) {
    gpio_write_pin_high(NRF_REQUEST_PIN);
    gpio_set_pin_output_push_pull(NRF_REQUEST_PIN);

    palSetLineMode(A15, PAL_MODE_ALTERNATE(0)); /* NSS  */
    palSetLineMode(B3, PAL_MODE_ALTERNATE(0));  /* SCK  */
    palSetLineMode(B4, PAL_MODE_ALTERNATE(0) | PAL_STM32_OSPEED_HIGHEST); /* MISO */
    palSetLineMode(B5, PAL_MODE_ALTERNATE(0));  /* MOSI */

    rccEnableSPI1(true);
    rccResetSPI1();
    /* Slave, hardware NSS, CPOL=1 CPHA=1, 8-bit, MSB first (stock HAL_SPI_Init). */
    SPI1->CR1 = SPI_CR1_CPOL | SPI_CR1_CPHA;
    SPI1->CR2 = SPI_CR2_RXNEIE;
    SPI1->CR1 |= SPI_CR1_SPE;
    SPI1->DR = 0x00;

    nvicEnableVector(SPI1_IRQ_NUMBER, SPI1_IRQ_PRIORITY);

    /* Let the nRF run (the bootloader leaves PD2 high). It sends its hello ~8 s later. */
    gpio_write_pin_low(NRF_ENABLE_PIN);
    gpio_set_pin_output_push_pull(NRF_ENABLE_PIN);
}

void nrf_spi_request(void) {
    gpio_write_pin_low(NRF_REQUEST_PIN);
    wait_us(500);
    gpio_write_pin_high(NRF_REQUEST_PIN);
    last_request_time = timer_read();
}

void nrf_spi_task(void) {
    bool waiting;
    chSysLock();
    waiting = report_pending || consumer_pending || control_pending || (battery_pending && !battery_polled);
    chSysUnlock();
    if (!waiting) {
        request_retries = 0;
        return;
    }
    if (request_retries < REQUEST_RETRIES && timer_elapsed(last_request_time) > REQUEST_RETRY_MS) {
        request_retries++;
        nrf_spi_request();
    }
}

void nrf_spi_set_pd2(bool high) {
    gpio_write_pin(NRF_ENABLE_PIN, high);
}

bool nrf_spi_ready(void) {
    return hello_seen;
}

int8_t nrf_spi_connected_slot(void) {
    uint8_t link = last_link;
    return (link & 0x0F) == 0x03 ? (int8_t)(link >> 4) : -1;
}

void nrf_spi_send_keyboard(const report_keyboard_t *report) {
    chSysLock();
    report_mods = report->mods;
    memcpy(report_keys, report->keys, sizeof(report_keys));
    report_pending = true;
    chSysUnlock();
    request_retries = 0;
    nrf_spi_request();
}

bool nrf_spi_report_idle(void) {
    return !report_pending;
}

static void control_start(uint8_t poll, uint8_t type, uint8_t value) {
    chSysLock();
    control_poll    = poll;
    control_type    = type;
    control_value   = value;
    control_polled  = false;
    control_pending = true;
    chSysUnlock();
    request_retries = 0;
    nrf_spi_request();
}

void nrf_spi_select_slot(uint8_t slot) {
    control_start(NRF_POLL_SLOT, 0, slot);
}

void nrf_spi_bt_idle(void) {
    control_start(NRF_POLL_CONTROL, NRF_CONTROL_IDLE, 0x00);
}

void nrf_spi_pair_slot(uint8_t slot) {
    control_start(NRF_POLL_CONTROL, NRF_CONTROL_PAIR, slot);
}

void nrf_spi_delete_slot(uint8_t slot) {
    control_start(NRF_POLL_CONTROL, NRF_CONTROL_DELETE, slot);
}

bool nrf_spi_control_busy(void) {
    return control_pending;
}

void nrf_spi_send_consumer(const uint8_t bits[4]) {
    chSysLock();
    memcpy(consumer_bits, bits, sizeof(consumer_bits));
    consumer_pending = true;
    chSysUnlock();
    request_retries = 0;
    nrf_spi_request();
}

void nrf_spi_send_battery(uint8_t level) {
    chSysLock();
    battery_level   = level > 100 ? 100 : level;
    battery_polled  = false;
    battery_pending = true;
    chSysUnlock();
    request_retries = 0;
    nrf_spi_request();
}



