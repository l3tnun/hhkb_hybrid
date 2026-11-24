# Analysis of the LED indication patterns of the stock firmware

> English translation of [../../reverse-engineering/led-indication.md](../../reverse-engineering/led-indication.md). If the two differ, the Japanese version is authoritative.

- Created: 2026-09-13
- Target: stock A0.48 STM32 application. Static analysis only. Intermediate artifacts: `build/b4/led/`
- LED0 = PA8 = **blue** (operating / connection state), LED1 = PA9 = **orange** (off / low-battery warning). Verified on hardware 2026-09-13 (a two-color LED; one is visible from outside). Both light when High. **LED1 takes priority in hardware**: while LED1 is lit, the logical state of LED0 is retained and LED1 is shown, and LED0 comes back when LED1 goes off ()
- Timer base: the TIM21 update interrupt increments each counter in 1 ms steps (on battery PSC=99/ARR=40 → 1.023kHz, on USB ARR=160). Thresholds are `>` comparisons (0x63=100ms, 0x3E7=1000ms, 0x2710=10s, 0xEA60=60s). The inactivity counter `0x2000009C` is in seconds
- State variable `0x20000044`: 0=off, 0x3C=USB idle, 0x3D=USB input in progress, 1=pairing, 2=waiting for registration, 3=BT connected, 4=reconnecting, 5=idle (stays awake)

## Patterns per state (all [C] confirmed in code)

| Situation | LED0 (PA8) | LED1 (PA9) |
|---|---|---|
| Long press of the button while off | Lights after 1 second, stays lit until released | – |
| Released after 1–10 seconds, boots | Lit until the connection is established, off on connection (state 3) | – |
| Long press of 10 seconds or more | Off at 10 seconds | Lit for 3 seconds, then off (does not boot) |
| Boot by reset / battery insertion (registration exists) | Lit (state 4, re-lit every 1 second) | – |
| USB boot, no registration (state 0x3C) | Lit for up to 60 seconds | – |
| Battery boot, no registration | Off | Lit for 3 seconds → STOP |
| BT reconnecting (state 4) | Lit | Off |
| BT connected (state 3) | Off | – |
| USB idle (0x3C) | Lit for 60 seconds after entering → off | – |
| USB input in progress (0x3D) | Lit for 1 second on input | – |
| Pairing (Fn+Q, state 1) | 200ms on / 200ms off (2.5Hz), until success or 600 seconds | – |
| Waiting for registration (Fn+Ctrl+n, state 2) | 100ms on / 100ms off (5Hz), until success or 600 seconds | – |
| Pairing succeeded | Blinking stops, off (to state 3) | – |
| Pairing cancelled (Fn+X) | Lit (state 4) | – |
| Switching the connection target (Fn+Ctrl+1–4) | Lit while switching → off on success | – |
| Short press (0.1–2 seconds, while connected) | Blinks (slot number + 1) times (200ms on / 800ms off, period 1 second) → off | – |
| Off (2-second long press / inactivity / timeout, state 0) | Off immediately | Lit for 3 seconds (waiting for nRF `83 01`, up to 3 seconds) → off |
| Inactivity sleep | Same as off | Same as off |
| Battery 15% (level1, battery only) | – | 600ms ×1 on change, thereafter at intervals of about 17.6 seconds → about 34.1 seconds |
| Battery 5%/0% (level2/3) | – | 600ms ×2 at intervals of about 17.6 seconds |
| Battery 0% (battery only) | Off | Lit until it stops (180 seconds / 2-second long press) or USB is plugged in |
| On USB power | Battery blinking is suppressed (`0x200000ED`) | Off |
| Registration deleted (`85`) | No indication | – |
| Charging | No LED code (there is no charging circuit) | – |

## Priority

- At the pin level, LED1 > LED0. The low-battery / off indication (LED1) covers the LED0 pattern, and LED0 comes back automatically when LED1 goes off
- The LEDs are initialized on every state transition (`0x20000010 != 0x20000044`), so things such as the slot blinking of a short press are cancelled when the state changes

## Undetermined

- The slot → blink count mapping (`0x200000AE = 0x80+slot`) needs to be checked once on real hardware
- The tick is 0.977ms on battery / 1.006ms on USB, so the blink period is off by about 3%

## Verification on hardware of the QMK implementation (B-4e stage 1) (2026-09-13)

- The user confirmed: BT switching / reconnecting = blue lit → off on connection, Fn+Q = blue blinking at 2.5Hz, Fn+Ctrl+n = blue blinking at 5Hz
- With the diagnostic Raw HID F4, confirmed PA8=blue / PA9=orange / both=orange (LED1 priority)
