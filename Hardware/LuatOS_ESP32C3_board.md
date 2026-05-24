# LuatOS ESP32-C3 Board

This project uses a LuatOS ESP32-C3 board configured in PlatformIO as `airm2m_core_esp32c3`.

## Project Pin Summary

| Signal | GPIO | Note |
| --- | ---: | --- |
| Red PWM | GPIO3 | PWM output to the driver's `CE` input |
| Green PWM | GPIO4 | PWM output to the driver's `CE` input |
| Blue PWM | GPIO5 | PWM output to the driver's `CE` input |
| White PWM | GPIO7 | PWM output to the driver's `CE` input |
| Wi-Fi LED | GPIO13 | on-board D5 LED, solid when connected and blinking in setup AP mode |
| GND | GND | common ground for the ESP32-C3 and LED drivers |
| 5V | 5V | LED driver supply, depending on the selected power source |

## Important Board Notes

- the board uses an ESP32-C3 module
- board dimensions are approximately 21 x 51 mm
- GPIO pins can be used as PWM outputs
- the board has two red indicator LEDs
- the project PWM outputs use `GPIO3`, `GPIO4`, `GPIO5` and `GPIO7` to avoid strapping pins, USB pins, UART0 pins and on-board LED pins
- `GPIO18` and `GPIO19` may be used for USB, so this project keeps them free
- the recommended Arduino board selection is `AirM2M CORE ESP32C3`

## Sources

- LuatOS board documentation: https://wiki.luatos.com/chips/esp32c3/board.html
