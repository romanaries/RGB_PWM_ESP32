# LuatOS ESP32-C3 Board

Projekt pouziva dosku LuatOS ESP32-C3, v PlatformIO nastavenu ako `airm2m_core_esp32c3`.

## Zhrnutie pre tento projekt

| Signal | GPIO | Poznamka |
| --- | ---: | --- |
| Red PWM | GPIO3 | PWM vystup na CE drivera |
| Green PWM | GPIO4 | PWM vystup na CE drivera |
| Blue PWM | GPIO5 | PWM vystup na CE drivera |
| White PWM | GPIO6 | PWM vystup na CE drivera |
| Wi-Fi LED | GPIO13 | doskova LED D5, svieti pri Wi-Fi, blika v AP rezime |
| GND | GND | spolocna zem ESP32 a LED driverov |
| 5V | 5V | napajanie driverov, podla pouziteho zdroja |

## Dolezite body z dokumentacie dosky

- jadro dosky je ESP32-C3
- rozmery dosky su priblizne 21 x 51 mm
- doska ma 4 PWM vystupy, pouzitelne cez GPIO
- doska ma 2 cervene indikujuce LED
- PWM vystupy projektu su presunute na GPIO3/GPIO4/GPIO5/GPIO6, aby sa nepouzivali strapping, USB, UART0 ani doskove LED piny
- `GPIO18` a `GPIO19` mozu byt pouzite pre USB, preto ich projekt nepouziva
- pre Arduino je odporucany vyber dosky `AirM2M CORE ESP32C3`

## Zdroje

- LuatOS dokumentacia dosky: https://wiki.luatos.com/chips/esp32c3/board.html
