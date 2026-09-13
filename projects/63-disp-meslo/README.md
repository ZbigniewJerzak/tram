# 63-disp-meslo

Meslo-Schrift- und Symboltest für das Elecrow CrowPanel ESP32-S3 5,79-Zoll
E-Paper HMI, SKU `DIS08792E`.

Das Projekt übernimmt den auf der Hardware bestätigten SSD1683-x2-Treiber,
die Ausrichtung und die Framebufferabbildung aus `62-test-disp-text`.
Es ersetzt die skalierte Adafruit-Standardschrift durch einen kompakten,
monochromen Bitmap-Subset von `MesloLGS NF Regular`.

## Enthaltener Font-Subset

Der generierte Font enthält in den Pixelgrößen 16, 24 und 36:

- druckbares ASCII von U+0020 bis U+007E;
- `Ä Ö Ü ä ö ü ß`;
- fünf Nerd-Font-Symbole: Zug, Bus, Uhr, WLAN und Warnung.

Die Quelldatei ist nicht im Projekt enthalten. `src/MesloFontData.h` wurde
aus einer lokal installierten `MesloLGS NF Regular.ttf` erzeugt. Der
Framebuffer bleibt 1 Bit tief; die Glyphen werden deshalb beim Generieren
ohne Graustufen gerastert.

## Erwartetes Bild

Nach dem vollständigen Löschzyklus erscheinen:

1. ein doppelter Rahmen und die Überschrift `MESLO NERD FONT | 1-BIT SUBSET`;
2. `Straße & Grüße` in 36 Pixel;
3. alle deutschen Sonderzeichen und Ziffern in 24 Pixel;
4. ein deutscher Fahrplansatz in 16 Pixel;
5. die fünf Symbole mit englischen Beschriftungen am unteren Rand.

Das Uhrsymbol liegt mittig auf x=396 und prüft damit gleichzeitig die
Controllernaht. Es wird keine zusätzliche Nahtmarkierung gezeichnet.

## Fontdaten reproduzieren

Das Generatorskript benötigt Python 3 und Pillow:

```bash
python3 projects/63-disp-meslo/tools/generate_meslo_fonts.py \
  "$HOME/Library/Fonts/MesloLGS NF Regular.ttf" \
  projects/63-disp-meslo/src/MesloFontData.h
```

Der für diesen Stand verwendete Font hatte den SHA-256-Wert:

```text
d97946186e97f8d7c0139e8983abf40a1d2d086924f2c5dbf1c29bd8f2c6e57d
```

Lizenz- und Herkunftshinweise stehen in `FONT-NOTICE.md`.

## Build, Upload und Monitor

```bash
pio run -d projects/63-disp-meslo

pio run -d projects/63-disp-meslo \
  -t upload \
  --upload-port /dev/cu.wchusbserial10

pio device monitor \
  -d projects/63-disp-meslo \
  --port /dev/cu.wchusbserial10 \
  --baud 115200
```

Der Gerätename kann sich nach erneutem Anschließen ändern. Vor dem Upload
muss ein bereits laufender serieller Monitor geschlossen werden.

Bei Erfolg endet die serielle Ausgabe mit:

```text
Meslo-Test abgeschlossen; Display ist im Deep Sleep.
```
