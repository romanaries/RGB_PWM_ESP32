# LDO6AJSA / LD06AJSA LED Current Driver

The LED current driver used in this project is an LDO6AJSA/LD06AJSA module based on the CN5711 chip.

## Role in the Project

- each LED channel has its own driver module
- the ESP32-C3 controls brightness with a PWM signal on the driver's `CE` input
- the trimmer on the module sets the maximum LED current
- a common ground between the ESP32-C3 and all driver modules is required

## Typical Module Parameters

| Parameter | Value |
| --- | --- |
| Controller IC | CN5711 |
| Type | constant-current LED driver |
| Supply voltage | 2.8 to 6 V DC |
| Output current | adjustable, typically 30 to 1500 mA |
| PWM control | via `CE`, recommended up to 2 kHz |
| Dropout | typically around 0.37 V at 1.5 A |
| Protection | thermal regulation and overcurrent limiting |
| Module size | approximately 18 x 10 mm |

## Module Pins

| Pin | Description |
| --- | --- |
| `V1` / `VCC` | positive driver supply |
| `G` / `GND` | ground, connect to ESP32-C3 ground |
| `CE` | enable/PWM input; logic high enables the output |
| `LED` | LED output |

## Wiring to the ESP32-C3

```text
ESP32 GPIOx -> CE input of the driver
ESP32 GND   -> GND of the driver
5V supply   -> V1/VCC of the driver
LED driver  -> high-power LED
```

Four modules are needed for RGBW: red, green, blue and white.

## Notes

- The module is suitable for one high-power LED or parallel LED branches, not for multiple LEDs in series above the module supply voltage.
- At higher currents, both the LED and the driver module need proper heat dissipation.
- The module is commonly sold as `LDO6AJSA`, `LD06AJSA` or `CN5711 LED driver`.

## Sources

- DONE.LAND CN5711 / LDO6AJSA description: https://done.land/components/light/ledcontroller/cn5711/
- HESTORE LD06AJSA product page and datasheet link: https://www.hestore.eu/en/prod_10048979.html
- AliExpress search: https://www.aliexpress.com/w/wholesale-LD06AJSA.html
