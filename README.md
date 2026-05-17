# RGBW PWM ESP32-C3 Controller

Bezdratovy RGBW PWM kontroler pre 4x prudovy LED driver LDO6AJSA/LD06AJSA s obvodom CN5711 a vykonove LED kanaly.

## Funkcie

- ovladanie z mobilu aj PC cez tmave web UI
- ESP32 sa pripaja do existujucej Wi-Fi, mobil/PC ostavaju na internete
- Wi-Fi konfiguracia cez portal `RGB-LAB-SETUP`, bez hesiel v kode
- 4 kanaly: R, G, B, White
- slider aj ciselny vstup 0-100 %
- samostatne akcne tlacidla `Turn on` / `Turn off` pre kanaly
- master akcne tlacidlo `Turn all on` / `Turn all off`
- plynuly linear ramp pri zmene hodnot a zap/vyp
- gamma korekcia PWM pre prirodzenejsi jas
- okamzite ulozenie poslednych hodnot do flash pamate
- Wi-Fi LED indikacia: svieti pri pripojeni do Wi-Fi, blika v AP rezime
- captive portal odpovede pre automaticke otvorenie Wi-Fi konfiguracie v mobile

## Odporucane piny na LuatOS ESP32-C3

| Signal | GPIO | Pin na doske |
| --- | ---: | ---: |
| PWM_R | GPIO0 | pin 2 |
| PWM_G | GPIO1 | pin 3 |
| PWM_B | GPIO12 | pin 4 |
| PWM_W | GPIO10 | pin 21 |
| WIFI_LED | GPIO13 | pin 10 / D5 |
| GND | GND | pin 14 |
| 5V | 5V | pin 16 |

GPIO12 je zaroven pripojene na doskovu LED D4, preto svieti/blika spolu s modrym kanalom. GPIO13/D5 pouzivame ako stavovu Wi-Fi LED. Vyhybame sa GPIO18/GPIO19, lebo mozu byt pouzite pre USB, a GPIO20/GPIO21 nechavame volne pre UART.

## Zapojenie

```text
ESP32 GPIO0  -> CE/PWM drivera cervenej LED
ESP32 GPIO1  -> CE/PWM drivera zelenej LED
ESP32 GPIO12 -> CE/PWM drivera modrej LED
ESP32 GPIO10 -> CE/PWM drivera bielej LED
ESP32 GPIO13 -> stavova LED na doske
ESP32 GND    -> GND zdroja a vsetkych driverov
5V zdroj     -> VCC driverov
```

Kazdy LED kanal pouzi cez samostatny prudovy driver LDO6AJSA/LD06AJSA. Prud nastav trimrom na bezpecnu hodnotu pre tvoju LED, typicky okolo 300-350 mA pre 1 W LED. LED aj driver potrebuju chladenie.

Podrobnejsie poznamky k doske a prudovemu driveru su v adresari `Hardware/`.

## Prve spustenie

1. Flashni firmware.
2. ESP32 sa pokusi pripojit na ulozenu Wi-Fi.
3. Ak ziadnu nema alebo sa nepripoji, vytvori AP `RGB-LAB-SETUP`.
4. Mobilom sa pripoj na `RGB-LAB-SETUP`.
5. Mobil by mal automaticky ponuknut konfiguracnu stranku. Ak nie, otvor `192.168.4.1`, vyber Wi-Fi a zadaj heslo.
6. Po pripojeni otvor IP adresu ESP32 v prehliadaci.

BOOT tlacidlo pri starte vynuti Wi-Fi konfiguracny portal.
