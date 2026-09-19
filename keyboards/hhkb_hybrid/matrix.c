// SPDX-License-Identifier: GPL-2.0-or-later
// Topre capacitive matrix scan for the HHKB Hybrid (STM32L072, ADC1 channel 1).
// Based on Duncaen's HHKB Classic port (same main board); waits are bounded.
#include "quantum.h"

#define ADC_WAIT_LIMIT 10000U
/* Stock firmware releases a pressed key 50 counts below the actuation point */
#define RELEASE_HYSTERESIS 50

uint16_t actuation_point[MATRIX_COLS][MATRIX_ROWS] = {0};

static uint16_t adc_read_ch1(void) {
    for (uint32_t i = 0; i < ADC_WAIT_LIMIT && (ADC1->CR & ADC_CR_ADSTP); i++) {
    }
    ADC1->CR |= ADC_CR_ADSTART;
    for (uint32_t i = 0; i < ADC_WAIT_LIMIT && !(ADC1->ISR & ADC_ISR_EOC); i++) {
    }
    return ADC1->DR & 0xFFF;
}

bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    bool matrix_has_changed = false;
    // Row drive: B0, B1, B2, B10
    static const uint16_t row_select[MATRIX_ROWS] = {0x1, 0x2, 0x4, 0x400};
    // Col select: B12, B13, B14 (BSRR: set bits low word, reset bits high word)
    static const uint32_t col_select[MATRIX_COLS] = {
        0x70000000, 0x60001000, 0x50002000, 0x40003000, 0x30004000, 0x20005000, 0x10006000, 0x00007000,
        0x70000000, 0x60001000, 0x50002000, 0x40003000, 0x30004000, 0x20005000, 0x10006000,
    };

    palSetPort(GPIOB, 0x407);

    // power switch board
    gpio_write_pin_high(C14);
    // enable OpAmp
    gpio_write_pin_high(C7);

    gpio_write_pin_high(C6); // U1
    gpio_write_pin_low(B15); // U2

    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        if (col == 8) {
            gpio_write_pin_low(C6);   // U1
            gpio_write_pin_high(B15); // U2
        }
        GPIOB->BSRR.W = col_select[col];
        for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
            matrix_row_t last_row_value = current_matrix[row];

            wait_us(2);
            // Discharge capacitor
            gpio_write_pin_high(C8);
            wait_us(2);
            gpio_write_pin_low(C8);

            palClearPort(GPIOB, row_select[row]);
            wait_us(2);

            uint16_t val       = adc_read_ch1();
            bool     pressed   = current_matrix[row] & (1 << col);
            uint16_t threshold = actuation_point[col][row] - (pressed ? RELEASE_HYSTERESIS : 0);

            if (val > threshold) {
                current_matrix[row] |= (1 << col);
            } else {
                current_matrix[row] &= ~(1 << col);
            }

            if (current_matrix[row] ^ last_row_value) {
                matrix_has_changed = true;
            }

            palSetPort(GPIOB, row_select[row]);
        }
    }
    // Disable OpAmp
    gpio_write_pin_low(C7);

    return matrix_has_changed;
}
