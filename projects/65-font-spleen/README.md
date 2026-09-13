# 65-font-spleen

Spleen-Bitmapfont-Test für das Elecrow CrowPanel ESP32-S3 5,79-Zoll
E-Paper HMI, SKU `DIS08792E`.

Das Projekt verwendet die drei nativen Spleen-Bitmapgrößen `8x16`, `12x24`
und `16x32`. Die Glyphen werden nicht skaliert, geglättet oder nachträglich
geschwellt: Ein Pixel in der BDF-Quelle entspricht genau einem Pixel auf dem
1-Bit-Display. Dadurch bleiben Strichstärke und Rhythmus des Terminalfonts
erhalten.

## Inhalt des Testbilds

- Spleen in allen drei Größen;
- deutsche Zeichen `ÄÖÜ äöü ß`;
- native Pfeil-, Box-Drawing- und Powerline-Glyphen von Spleen;
- sechs handgezeichnete Statussymbole: Tram, Zug, Bus, Uhr, WLAN und Warnung;
- doppelter Rahmen und unveränderte, verifizierte Controllernaht-Abbildung.

Das Tram-Symbol ist bewusst als eigenes 1-Bit-Piktogramm implementiert. Es
gehört nicht zum Spleen-Font. Spleen ist ein Terminalfont und enthält zwar
technische Symbole, aber keine Verkehrsmittel-Piktogramme.

## Herkunft und Reproduzierbarkeit

Die unveränderten BDF-Quellen und die BSD-2-Clause-Lizenz liegen unter
`fonts/upstream/`. Sie stammen aus Spleen 2.2.0, Upstream-Commit:

```text
57f9219328c9f5873085320fe8bc8f7dd34b8791
```

Der bereits eingecheckte Header lässt sich ohne Pillow oder weitere
Python-Pakete neu erzeugen:

```bash
python3 projects/65-font-spleen/tools/generate_spleen_fonts.py \
  projects/65-font-spleen/fonts/upstream \
  projects/65-font-spleen/src/SpleenFontData.h
```

Das Skript übernimmt druckbares ASCII, deutsche Zeichen sowie ausgewählte
Pfeil-, Box-Drawing- und Powerline-Glyphen. Es prüft mindestens den für die
Textbeispiele erforderlichen Zeichensatz und dokumentiert die SHA-256-Werte
der drei Quelldateien im erzeugten Header.

## Build, Upload und Monitor

```bash
pio run -d projects/65-font-spleen

pio run -d projects/65-font-spleen \
  -t upload \
  --upload-port /dev/cu.wchusbserial10

pio device monitor \
  -d projects/65-font-spleen \
  --port /dev/cu.wchusbserial10 \
  --baud 115200
```

Der Gerätename kann sich nach erneutem Anschließen ändern. Vor dem Upload
muss ein bereits laufender serieller Monitor geschlossen werden. Bei Erfolg
endet die Ausgabe mit:

```text
Spleen-Test abgeschlossen; Display ist im Deep Sleep.
```
