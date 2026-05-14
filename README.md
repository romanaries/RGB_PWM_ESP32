# RGB PWM ESP32-C3 Controller

Bezdratovy RGB PWM kontroler pre 3x prudovy LED driver LD06AJSA/CN5711 a 1 W RGB LED kanaly.

## Funkcie

- ovladanie z mobilu aj PC cez tmave web UI
- ESP32 sa pripaja do existujucej Wi-Fi, mobil/PC ostavaju na internete
- Wi-Fi konfiguracia cez portal `RGB-LAB-SETUP`, bez hesiel v kode
- 3 kanaly: R, G, B
- slider aj ciselny vstup 0-100 %
- samostatne akcne tlacidla `Turn on` / `Turn off` pre kanaly
- master akcne tlacidlo `Turn all on` / `Turn all off`
- plynuly linear ramp pri zmene hodnot a zap/vyp
- gamma korekcia PWM pre prirodzenejsi jas
- ulozenie poslednych hodnot do flash pamate

## Odporucane piny na jednej strane LuatOS ESP32-C3

| Signal | GPIO | Pin na doske |
| --- | ---: | ---: |
| PWM_R | GPIO0 | pin 2 |
| PWM_G | GPIO1 | pin 3 |
| PWM_B | GPIO12 | pin 4 |
| GND | GND | pin 14 |
| 5V | 5V | pin 16 |

Vyhybame sa GPIO18/GPIO19, lebo mozu byt pouzite pre USB, a GPIO20/GPIO21 nechavame volne pre UART.

## Zapojenie

```text
ESP32 GPIO0  -> CE/PWM drivera cervenej LED
ESP32 GPIO1  -> CE/PWM drivera zelenej LED
ESP32 GPIO12 -> CE/PWM drivera modrej LED
ESP32 GND    -> GND zdroja a vsetkych driverov
5V zdroj     -> VCC driverov
```

Kazdy LED kanal pouzi cez samostatny prudovy driver. Prud nastav trimrom na bezpecnu hodnotu pre tvoju LED, typicky okolo 300-350 mA pre 1 W LED. LED potrebuje chladenie.

## Prve spustenie

1. Flashni firmware.
2. ESP32 sa pokusi pripojit na ulozenu Wi-Fi.
3. Ak ziadnu nema alebo sa nepripoji, vytvori AP `RGB-LAB-SETUP`.
4. Mobilom sa pripoj na `RGB-LAB-SETUP`.
5. Otvor `192.168.4.1`, vyber Wi-Fi a zadaj heslo.
6. Po pripojeni otvor IP adresu ESP32 v prehliadaci.

BOOT tlacidlo pri starte vynuti Wi-Fi konfiguracny portal.
