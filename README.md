# Ultrasonic Parking Sensor — STM32G431RB (Bare Metal)

A proximity warning system using an HC-SR04 ultrasonic distance sensor and an STM32 Nucleo-G431RB board. Written entirely at the register level without HAL libraries.

## What it does

The sensor continuously measures the distance to the nearest object. A buzzer beeps faster as the object gets closer, similar to a car parking sensor.

| Distance     | Buzzer behavior     |
|--------------|---------------------|
| < 20 cm      | Fast beep (100 ms)  |
| 20 – 40 cm   | Medium beep (200 ms)|
| 40 – 80 cm   | Slow beep (350 ms)  |
| > 80 cm      | Off                 |

## Hardware

- **Board:** STM32 Nucleo-G431RB
- **Sensor:** HC-SR04 ultrasonic distance sensor
- **Buzzer:** Active buzzer on PB11

### Pin mapping

| Function | Pin | Description                        |
|----------|-----|------------------------------------|
| Trigger  | PA7 | 15 µs pulse to start measurement   |
| Echo     | PA6 | Input capture via TIM3 channel 1   |
| Buzzer   | PB11| Toggled at variable rate            |

## How it works

1. A 15 µs trigger pulse is sent on PA7 every ~50 ms.
2. TIM3 input capture records the rising and falling edges of the echo signal on PA6.
3. The echo duration is converted to distance in cm using the speed of sound (~58 µs per cm round-trip).
4. The buzzer beep rate is adjusted based on the measured distance.

The project uses a simple state machine (`IDLE → WAIT_RISING → WAIT_FALLING → DONE`) driven by the TIM3 capture interrupt. SysTick provides a 1 ms timebase for buzzer timing.

## Build

Built with **STM32CubeIDE 2.1.0**. Import the project into your workspace and build.

## Notes

- No HAL or LL drivers — all peripheral access is done through direct register manipulation.
- System clock is assumed to be the default 24 MHz HSI (no PLL configuration).
- TIM3 is prescaled to 1 µs per tick for easy time measurement.
