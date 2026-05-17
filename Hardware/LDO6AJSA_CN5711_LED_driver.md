# LDO6AJSA / LD06AJSA LED Current Driver

Ovladany prudovy zdroj pre LED v tomto projekte je modul LDO6AJSA/LD06AJSA s obvodom CN5711.

## Uloha v projekte

- kazdy LED kanal ma vlastny driver
- ESP32 ovlada jas cez PWM signal na vstupe `CE`
- trimrom na module sa nastavuje maximalny prud LED kanala
- spolocna zem ESP32 a driverov je nutna

## Typicke parametre modulu

| Parameter | Hodnota |
| --- | --- |
| Riadiaci obvod | CN5711 |
| Typ | konstantnoproudovy LED driver |
| Napajanie | 2.8 az 6 V DC |
| Vystupny prud | nastavitelny, typicky 30 az 1500 mA |
| PWM riadenie | cez `CE`, odporucane do 2 kHz |
| Dropout | typicky okolo 0.37 V pri 1.5 A |
| Ochrana | teplotna regulacia a obmedzenie nadprudu |
| Rozmery modulu | priblizne 18 x 10 mm |

## Piny modulu

| Pin | Popis |
| --- | --- |
| `V1` / `VCC` | kladne napajanie drivera |
| `G` / `GND` | zem, spojit s GND ESP32 |
| `CE` | enable/PWM vstup, logicka 1 zapina vystup |
| `LED` | vystup pre LED |

## Zapojenie s ESP32

```text
ESP32 GPIOx -> CE drivera
ESP32 GND   -> GND drivera
5V zdroj    -> V1/VCC drivera
LED driver  -> vykonova LED
```

Pre RGBW su potrebne 4 kusy modulu: Red, Green, Blue, White.

## Poznamky

- Modul je vhodny pre jednu vykonovu LED alebo paralelne LED vetvy, nie pre viac LED v serii s vyssim napatim ako napajanie modulu.
- Pri vyssich prudach treba riesit odvod tepla z LED aj z drivera.
- Na AliExpresse sa modul predava pod oznacenim `LDO6AJSA`, `LD06AJSA`, pripadne ako `CN5711 LED driver`.

## Zdroje

- DONE.LAND CN5711 / LDO6AJSA popis: https://done.land/components/light/ledcontroller/cn5711/
- HESTORE LD06AJSA produkt a datasheet odkaz: https://www.hestore.eu/en/prod_10048979.html
- AliExpress hladanie: https://www.aliexpress.com/w/wholesale-LD06AJSA.html
