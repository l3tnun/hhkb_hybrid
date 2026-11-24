# Analysis of the STM32 ⇔ nRF52832 communication protocol (Bluetooth)

> English translation of [../../reverse-engineering/bluetooth-protocol.md](../../reverse-engineering/bluetooth-protocol.md). If the two differ, the Japanese version is authoritative.

- Created: 2026-09-13 (static analysis)
- Target: stock A0.48. The STM32 application (`hhkb_stock_128k.bin` from 0x08010000) and the nRF application inside the HFB (flash from 0x26000)
- Static analysis + observation on real hardware (2026-09-13 13:58, §9). "Confirmed" means it could be confirmed in the code, "verified on hardware" means it could be observed with QMK, and "inferred" means a guess from call relationships
- Intermediate artifacts for the analysis: `build/b4/` (not tracked by Git. `app.s`/`boot.s` = STM32 code disassembly, `nrf/` = nRF side, `stm32/` = STM32 side)

## 1. Physical layer

| Item | STM32 side | nRF side | Confidence |
|---|---|---|---|
| Role | SPI1 **slave** (HAL Mode=0, NSS hardware input) | Legacy SPI0 **master** (0x40003000) `0x2b8b0` | Confirmed |
| Mode | CPOL=1, CPHA=1, 8bit, MSB first | CONFIG=0x06 (the same), 125kHz | Confirmed |
| NSS/CS | PA15 (AF0) | P0.22 driven manually as GPIO | Confirmed |
| SCK | PB3 (AF0) | P0.25 (floating between transactions) | Confirmed |
| MISO | PB4 (AF0) | P0.24 | Confirmed |
| MOSI | PB5 (AF0) | P0.23 | Confirmed |
| Request line | **PB6**: normally High, requests with a Low pulse (100 or 400 loops) | **P0.18**: input, schedules a cmd 02 transaction on the falling edge `0x2884d` | **Verified on hardware** (3 PB6 pulses → cmd 02 three times) |
| nRF enable | **PD2**: the nRF does not run while High. Stock drives it High in initialization and Low at the top of the main loop. The bootloader drives it High before the jump | — | **Verified on hardware** (hello about 8 seconds after PD2 Low) |
| Receive | RXNE interrupt | SPI0 interrupt | Confirmed |

Note: the earlier statement "PB6 = nRF reset" was wrong. It is a request (attention) pulse.

## 2. Shape of a transaction (as seen from the nRF)

```
Command send only (2 bytes of 83 xx / 85 xx / 61 xx / 93 xx, or 84 + 30 bytes):
    CS low → send the byte sequence → CS high            (no response is read)

Query (01..06, A0, F0):
    CS low → send cmd
    While sending 0x00, wait for up to 100 bytes until 0xAA is received
    After 0xAA, read N bytes (sending 0x00) → CS high
        N: 01→1, 02→8, 03→2, 04→30, 05→1, 06→1, A0/F0→1
    If 0xAA does not come, retry (01 is resent immediately, the others after a timer, up to 600 times)
```

The STM32's response has the form `00 AA <payload>`, with no checksum or length. The nRF discards everything up to 0xAA.

## 3. Queries from the nRF → STM32 and the STM32's responses

| nRF cmd | STM32's response (after 0xAA) | Meaning | Confidence |
|---|---|---|---|
| 01 | `E5` | Hello at startup. The STM32 waits in startup until it receives this | Confirmed |
| 02 | `01 mod k1 k2 k3 k4 k5 k6` | Keyboard report (in state 2/3). No change is `01` + zeros | Confirmed |
| 02 | `02 cc 00 ...` | Consumer (media) key | Confirmed (STM32). The handling on the nRF side is inferred |
| 02 | `80 00` → then `level` in reply to nRF cmd 05 | Battery level 0–100 (100 while on USB power) | Confirmed |
| 02 | `10 00` → `slot` in reply to nRF cmd 06 | **Switching the connection target**: asks whether the slot has a registration → the nRF answers `61 00` (yes; starts reconnecting to that slot) / `61 01` (no; does nothing) | Confirmed (detailed analysis 2026-09-13) |
| 02 | `40 00` → `01 slot` in reply to nRF cmd 03 | **Start pairing on slot slot** (public advertising, updates the slot's address. A new registration replaces the existing one) → `83 02` → `83 (slot<<4)\|3` when registration completes | Confirmed (the earlier statement "connect" was wrong) |
| 02 | `40 00` → `02 x` in reply to nRF cmd 03 | **Delete the registration of slot x** (x=0x99 deletes all) → nRF `85 p` (p=0 success) | Confirmed (the earlier statement "pairing" was wrong) |
| 02 | `40 00` → `10 00` in reply to nRF cmd 03 | Put BT into the idle state (stop advertising, disconnect) → `83 01` | Confirmed |
| 02 | `40 00` → `04 00` in reply to nRF cmd 03 | Requests transmission of the nRF info block (84 + 30 bytes, contains the firmware version "0.29") | Confirmed |
| 02 | `08` / `20` | Start of firmware update / request to resend the last 83 state (the former is not used by the stock STM32) | Inferred |
| A0 / F0 | E2/E3 frames | Relay of the nRF firmware update (not reached even in stock) | Confirmed |

## 4. Notifications from the nRF → STM32 (2 bytes)

| Bytes | Meaning | Confidence |
|---|---|---|
| `83 01` | Disconnected / idle | Inferred (medium) |
| `83 02` | Connection in progress / advertising | Inferred (medium) |
| `83 (slot<<4)\|3` | Connected to slot slot (0–3). The STM32 goes to state 3 and records it in the EEPROM | Confirmed (STM32-side processing) |
| `85 p` | Pairing result (STM32 side) / host LED state (nRF side) | The use disagrees between the two sides. Needs measurement |
| `61 00` / `61 01` | The slot asked about with cmd 06 is paired / not paired | Inferred (medium) |
| `93 xx` | After erase-all / error. The STM32 discards it | Inferred (low) |

## 5. Behavior of the STM32 side

- Key change: after the scan , if the state (`0x20000044`) is 2/3 and a report is pending, PB6 pulse → the nRF issues cmd 02 → the report is sent
- Every procedure the STM32 initiates begins with a PB6 pulse → wait for cmd 02 (20ms × 3 times; reset if that fails). The subsequent response waits are 100ms each
- Startup: wait for the nRF's `01` → get the info block → (`40` → `10` in reply to `03`)
- State variable `0x20000044`: 0/0x3C/0x3D = USB family, 1 = pairing, 2/3 = BT connection (3 = connected), 4 = BT selection / waiting for reconnection, 8 = other

## 6. Stock key operations (〜)

`0x20000061` = number of pressed keys, `0x200000E1` = a Ctrl-family modifier is pressed.

| Operation | Processing | Confidence |
|---|---|---|
| Fn+Q | → state 1 → PB6 pulse (start pairing) | Confirmed (the meaning is inferred) |
| Fn+Ctrl+1–4 | `(0〜3)`: stop USB transmission → select the slot and connect (state 3/4) | Confirmed (the meaning is inferred) |
| Fn+Ctrl+0 |: switch to USB | Inferred |
| 1–4 after Fn+Ctrl+Del | `(0〜3)` → (pairing/deletion family) | Inferred |
| Fn+Ctrl+W / M | Switch to USB + change of keymap mode () | Inferred |

## 7. Other signals (STM32)

| Signal | Contents | Confidence |
|---|---|---|
| PC13 | USB power detection (Low = powered) | Verified on hardware |
| PC5 | Power button (5ms debounce, long press of about 2 seconds) | Inferred (high) |
| PB7/PB8/PB9 | Battery level stage inputs → 100/15/5/0 | Inferred (medium) |
| PA8 / PA9 | LEDs (lit when High) | Confirmed (code) |
| PD2 | nRF enable (the nRF is stopped while High and starts on Low). Stock drives it High in initialization and Low at the top of the main loop | Verified on hardware (§9) |

## 8. Undetermined (needs checking on real hardware)

- The disagreement over the meaning of `85`, and the exact meanings of `83 01/02`, `61` and `93`
- Whether the nRF issues cmd 02 periodically even without the request line
- The actual byte timing (the allowed time until the response is placed in DR)

## 9. Observation on real hardware (QMK B-4a, 2026-09-13)

An SPI1 slave was implemented in QMK (`keyboards/hhkb_hybrid/nrf_spi.c`) and its statistics were observed. USB connected, batteries inserted.

| Time | Operation | Observation |
|---|---|---|
| 13:58:03 (3.9s after boot) | None (PD2 still High) | 0 bytes received. PA15(NSS) = 1 |
| 13:58:03 | PD2 → Low | — |
| 13:58:11 | (about 8 seconds later) | Received the nRF hello `01` → answered `AA E5`. Notification `83 03` |
| 13:58:21 | | Cumulative notifications 2 (`83 03`). PD2 = 0 |
| 13:58:21–24 | PB6 pulse ×3 | cmd `02` ×3, 10 bytes each (cmd + 1 waiting for 0xAA + 8 read). 0 overruns |

- `83 03` = connected to the host of slot 0 (if the interpretation of §4 is right). It is thought to have connected automatically to a device paired under the stock firmware
- The `AA` response is in time for the first dummy byte (placing it in DR inside the RXNE interrupt is sufficient)

## 10. Flow of pairing, switching the connection target and deletion (detailed analysis 2026-09-13)

Intermediate artifacts: `build/b4/pairing/`. **[C]** = confirmed in code, **[I]** = inferred.

| Operation (stock) | STM32 → nRF | nRF result |
|---|---|---|
| Fn+Q | `40` → `10 00` in reply to 03 (idle), state 1 | `83 01` |
| Fn+Ctrl+n in state 1 | `40` → `01 n` in reply to 03 (start pairing) | `83 02` → `83 (n<<4)\|3` when registration completes. The old registration is deleted after the new peer has been saved [C] |
| Fn+Ctrl+n (normal) | `10` → `n` in reply to 06 | If `61 00`, reconnection starts (`83 (n<<4)\|3` within at most 60 seconds); if `61 01`, nothing happens [C] |
| In state 1, Fn+Ctrl+Del and then Fn+Ctrl+Del+n | `40` → `02 n` in reply to 03 | `85 00` success / `85 01` failure [C] |
| In state 1/2, Fn+Z and then Fn+Z+Del | `40` → `02 99` in reply to 03 (delete all) | `85 00` [C] |
| Fn+X (state 1/2) | Cancel pairing → reconnection scan | [C] |

- The BLE address differs per slot (name "HHKB-Hybrid_n") [C]. If the same device is registered in another slot, the Peer Manager may overwrite the existing key and break the registration of the original slot [I]
- If all slots are unregistered, stock automatically sends `01 00` (pairing on slot 1) [C]. QMK does not imitate this
- Stock resets the MCU on a response-wait timeout. QMK also resets on the same principle (with longer wait times. §13)

**Operations that erase/break an existing registration**: `02 n` and `02 99`, `01 n` on a registered slot, and re-registering the same device in another slot. Safe operations: `10 00`, and the `10`/06 query (if registered, reconnection starts).

## 11. QMK implementation (B-4d)

- Fn+Ctrl+1–4: switch with `10`/06 (sends nothing if it is the currently connected slot)
- Fn+Q: idle with `10 00` → waiting for pairing. Then `01 n` with Fn+Ctrl+n. Cancelled by Fn+X or after 60 seconds (`10`/06 to the previous slot)
- Deletion (`02 n`) is API only, with no key assignment
- "Fn+Ctrl+1 sends `01 00` if not connected" in B-4c-1 was wrong. It was never sent because the keyboard was always connected with `83 03` during testing (diagnostic controls=0)
- Giving up on reconnection follows stock (no automatic pairing). If not connected, use `10`/06; after a disconnection send nothing (the nRF re-advertises by itself); up to 60 seconds (blue LED). After failure, put the nRF into idle with `10 00` and turn the LED off. On battery with SW6 OFF, STOP; with SW6 ON / USB, stay awake and retry on a key or a power-button short press. The user-facing specification is in [../usage.md](../usage.md)

### Key release does not reach the old host on a slot switch (known defect)

When switching to another slot with `Fn+Ctrl+n` while connected over BT, the Ctrl press sometimes remains on the host from before the switch (it stays held down). The cause is that no all-keys-released report is sent to the old link at the switch.

- `Fn+Ctrl+1–4` is a combo whose detection condition is that Ctrl is pressed, and at the time of the switch Ctrl is still physically held. The Ctrl press report has already been sent to the host from before the switch, but the Ctrl release reaches only the link after the switch.
- `set_output_bt()` on the QMK side (`keyboards/hhkb_hybrid/hhkb_hybrid.c`) calls `clear_keyboard()` (release on the path being left) only when the output path changes. A BT→BT switch does not change `output_bt`, so no release is sent, and `select_bt_slot()` sends `nrf_spi_select_slot()` as it is.
- `answer_poll()` in `nrf_spi.c` gives `control_pending` the highest priority, so if a switch request (`10`/06, `40`→`03`) is queued before the release report has been read by the nRF, the release is deferred and is sent to the link after the switch. The release → switch order has to be guaranteed by waiting.
- On `Fn+Ctrl+1–4`, stock does "stop sending keys → select the slot", sending the release through the `cmd=0x02` release path before switching (§6, [stm32-analysis.md](stm32-analysis.md) §2-1). The QMK implementation has no such release step.

Countermeasure (implemented): before sending a switching command (`select`/`pair`/`idle`/`delete`), `bt_release_before_control()` in `hhkb_hybrid.c` sends an all-keys-released report to the nRF with `clear_keyboard()`, waits until `nrf_spi_report_idle()` becomes true (the nRF has read it), and then waits briefly (the same technique as `jis_settle()`). For the BT→USB output switch, the same wait is inserted when leaving BT in `set_output_bt()`.

## 12. Results on real hardware (B-4d, 2026-09-13 15:00–15:11)

| Item | Result |
|---|---|
| Battery level | Answered the nRF's 02 with `80 100` and the following 05 with `100` (the exchange worked). 100% in the iPad's battery widget (100% by specification while on USB power) |
| Media keys | iPad: volume +/− OK, mute no reaction. Linux: mute OK → judged to be how iPadOS handles it |
| Pairing | With Fn+Q → Fn+Ctrl+2, `83 03`→`01`→`02`; "HHKB-Hybrid_2" was detected from the PC (BlueZ 5.85) and pairing succeeded with Just Works → `83 13` |
| Problem while USB is connected | Right after pairing, a passkey entry prompt was shown and it could not be entered; disconnected with `83 01` (cause undetermined) |
| Battery only | Connected automatically to the PC (slot 2) at boot; input, mute and screenshot OK. Switching with Fn+Ctrl+1 (iPad) / Fn+Ctrl+2 (PC) OK |
| Ctrl remaining on a slot switch | While connected to the host of slot 1, switching to slot 2 with `Fn+Ctrl+2` with Ctrl held leaves Ctrl held down on the host of slot 1 (observed by the user). The cause is in §11. The release countermeasure is implemented (build checked); verification on hardware has not been done |

## 13. The defect of stopping without being able to reconnect, and the response-wait reset (static analysis 2026-10-07)

### Symptom (user report)

With QMK on battery power and DIP SW6 ON, after causing a disconnection, for example by turning off Bluetooth on the host, and after the LED has gone off because reconnection was given up, pressing a key some time later leaves the blue LED lit without reconnecting. Turning the power OFF→ON reconnects immediately. The time until it occurs is unknown, and there is no environment that reproduces it.

### Behavior of the stock STM32 [C]

- **Every request to the nRF is protected by a response wait**. The request functions first call `0x080109fc`: PB6 pulse → if cmd 02 does not come within 20 ms, pulse again; after 3 failures, `NVIC_SystemReset` (AIRCR = 0x05FA0004). The subsequent wait for the 06/03 query (`0x0801cfb4`, 100 ms each) also resets on timeout. After the reset the bootloader drives PD2 High, so the nRF is reset too (the same as power-cycling).
- **Disconnection in state 3** (`83 01`): the STM32 sends nothing and waits 60 seconds (from `0x08019964`). After that it sends `0x0801ad50` (`40` → `10 00` in reply to 03, up to 6 times at 200 ms intervals until `83 01`) and goes to state 5.
- **A key in state 5**: state 4 (`0x200000bb` = 1) → `10`/06 (`0x080104fc`) to the registered slot (`0x200000af[n]`). If `61 01` (unregistered), automatic pairing (`0x08010798`); if `61 00`, waits up to 60 seconds and goes to state 5 or 0.

### Behavior of the stock nRF [C]

- SPI query (`0x00028b24`): if 0xAA does not come within 100 bytes, it retries 10 ms later (app_timer 0x148 ticks), and gives up after at most 600 times. The retry count returns to 0 on success.
- The notification/query flags (`0x20002938`) are processed one item per loop iteration, unless an SPI transaction is in progress (`0x2000293e`). The main events (`0x20002910`: key transmission, selection, idle, pairing, deletion) are not processed during an nRF firmware update (`0x20003084`) or during registration deletion (`0x20002b35`).
- cmd 06 (`0x00028950`): if registered, it notifies `61 00` and raises the selection event → `0x0002a010`: if not connected, `0x00029c5c` (mode 2; starts over from ble_advertising_init in `0x00029484` and does FAST advertising, with whitelist).
- The 02 response `80 x` (battery) updates the Battery Service on the spot (`0x0002a770`). A 05 query does not necessarily follow. `10`/`40` are always followed by 06/03 (`0x00028c94`).
- On disconnection, the SDK's ble_advertising re-advertises automatically (on-disconnect enabled. It is disabled in `0x00029518` only when the nRF itself disconnects).
- System OFF (SVC 0x33) is not used. The error handler (`0x0002861c`) does nothing (`bx lr`), so SoftDevice errors are silently ignored.

### Differences from QMK and countermeasures (implemented)

The state in which the nRF stops cannot be determined by static analysis. Stock recovers by a reset when the nRF does not respond, but QMK did not have that; it gave up after pulsing PB6 5 times, and the request remained (the remaining idle request also blocked slot selection). The countermeasures are as follows (`nrf_spi.c`, `hhkb_hybrid.c`).

| Countermeasure | Contents |
|---|---|
| Response wait | After the hello, if a key, a media key, the battery (until read by 02) or a connection target request (until 06/03) is not read for 1 second or more, it is "no response". It is longer than stock's 20–100 ms to allow for the nRF stalling during SoftDevice flash operations. PB6 is pulsed 5 times every 30 ms at first, and after that keeps being pulsed every 100 ms |
| Reset | On no response, `mcu_reset()` (the same as stock. It is the same path as power ON, so the nRF is reset too). Only while outputting to Bluetooth |
| Retry reset (QMK-specific) | After giving up (GIVEUP, the nRF has reported not connected), on a key or a power-button short press, reset immediately without sending `10`/06. This is because on real hardware (iPad Bluetooth OFF for 11 minutes → ON), with `10`/06 SPI kept responding but it did not reconnect, and after a reset it reconnected |
| Replacing the idle request | An idle request (`10 00`) that has not been read yet is replaced by a slot selection |
| Right after disconnection | As in stock, nothing is sent for 60 seconds (left to the nRF's automatic re-advertising) |
| nRF restart | On a second hello, the connection state is cleared and the last slot is selected again |

### Overwriting of control requests (fixed 2026-10-07)

QMK used to overwrite a control request that the nRF was in the middle of reading (02 answered with `10`/`40`, and 06/03 not yet arrived) with a new request. When overwritten, the nRF's 06/03 cannot be answered, and the nRF retries that query every 10 ms (the retry count returns to 0 every time another transaction succeeds, so it may never stop). `control_start()` waits up to 100 ms if there is a request being read, before issuing the next request.

## 14. Power OFF does not disconnect (static analysis 2026-10-07)

### Symptom

Even when the keyboard is turned OFF with a 2-second long press of the power button, it remains shown as connected on the iPad side. This did not change even after QMK was fixed to deliver the idle request (`40` → `10 00` in reply to 03) reliably.

### Cause (a defect of the stock nRF) [C]

- The handling of the idle request (`0x00029fbc`) disconnects with `0x000294fc` if connected. However, while `0x20002a76` (a notification is being sent) is 1, it holds the disconnection pending (`0x20002a73` = 1) and executes it when `0x20002a76` becomes 0 on `BLE_GATTS_EVT_HVN_TX_COMPLETE` (0x57, `0x00029da4`).
- `0x20002a76` returns to 0 only when the count of notifications being sent, `0x20003048`, is 0. It is +1 for every notification sent (key `0x0002a638`, media key `0x0002a6c4` with 4 notifications per call, battery `0x0002a770`) and −1 per HVN_TX_COMPLETE (`0x0002a8b0`). The SoftDevice reports the completion of several notifications together through the `count` of this event, but the nRF does not look at `count`.
- As a result the notification count stays higher than the real value and does not return to zero while connected (it is set to 0 only on connection `0x0002a800` and on disconnection `0x0002a850`). The disconnection of the idle request is not executed. The stock STM32 also only waits 3 seconds for `83 01`, so in the same situation stock should not disconnect either.
- The same pending should also apply when switching to another slot while connected (`0x0002a010` → `0x0002954c`) (it may fail to switch. Unverified on real hardware, and no countermeasure has been taken on the QMK side).

### Countermeasure (QMK)

On power OFF (`enter_off()` in `power.c`), after sending the idle request and waiting up to 3 seconds for `83 01`, PD2 is driven High to stop the nRF before entering STOP. The link is dropped by the timeout on the host side. Power ON is a reset, and the bootloader starts the nRF again, so leaving it stopped is not a problem.
