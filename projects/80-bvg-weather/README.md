# 80-bvg-weather

Kombiniertes BVG- und Wetter-Dashboard für das Elecrow CrowPanel ESP32-S3
5,79-Zoll E-Paper HMI. Das Projekt ist eine vollständige Kopie des
funktionierenden Stands `71-weather` und ergänzt auf den zuvor freien linken
zwei Dritteln Tram-Abfahrten an der Haltestelle Erich-Baron-Weg.

## Bildschirmaufteilung

| Bereich | Pixel | Inhalt |
|---|---:|---|
| BVG links | x=0–527 | Erich-Baron-Weg; zwei Abfahrten je Richtung |
| Wetter rechts | x=528–791 | unverändertes Wetterpanel aus Projekt 71 |

Das BVG-Panel besitzt einen Block für jede der beiden Fahrtrichtungen. Die
erste und zweite Abfahrt jeder Richtung erscheinen in Spleen 16×32 mit
erwarteter Uhrzeit, Verspätung, Tram-Piktogramm, Linie und rechtsbündigem
Countdown. Zwischen diesen beiden Zeilen stehen Ziel und aktuelle
Fahrzeughaltestelle in Spleen 8×16, beispielsweise
`S MAHLSDORF (AKTUELL: ROSEGGERSTR.)`. Unerwartet lange Angaben werden
UTF-8-sicher gekürzt und mit `...` abgeschlossen. Die beiden Panels besitzen
getrennte Clipping-Rechtecke, damit kein Text über die gemeinsame Grenze
läuft.

Die Vorschau wird direkt aus den tatsächlich einkompilierten Bitmapdaten in
`src/SpleenFontData.h` erzeugt:

```bash
python3 projects/80-bvg-weather/tools/render_dashboard_preview.py \
  projects/80-bvg-weather/src/SpleenFontData.h \
  projects/80-bvg-weather/docs/dashboard-preview.png
```

## BVG-Daten

Das Modul `src/BvgDepartureClient.*` übernimmt die getestete Datenlogik aus
`52-bvg-departure-eink`, nicht aber dessen inkompatiblen Treiber für das
kleine SSD1680-Display.

Verwendete API:

```text
https://v6.bvg.transport.rest
```

Beim ersten Abruf sucht das Modul nach `Erich-Baron-Weg` und speichert die
gefundene Stop-ID im RAM. Danach werden nur Tram-Abfahrten für die nächsten
60 Minuten abgefragt. Ausgefallene Fahrten werden verworfen; gültige Fahrten
werden nach Fahrtrichtung gruppiert und innerhalb jeder Richtung nach der
erwarteten Zeit sortiert. Es werden höchstens zwei Richtungen mit jeweils
zwei Abfahrten angezeigt. ISO-8601-Zeitstempel einschließlich explizitem
UTC-Offset werden in Unix-Zeit umgerechnet.

Für die jeweils erste Abfahrt einer Richtung ruft das Modul anhand von
`tripId` die Route `/trips/:id` auf. Als aktuelle Haltestelle gilt der letzte
Stopover, dessen reale Ankunfts- oder Abfahrtszeit zum Abrufzeitpunkt erreicht
ist; fehlen Echtzeitwerte, dienen die Planzeiten als Rückfall. Vor dem
Fahrtbeginn erscheint `NOCH NICHT GESTARTET`, bei fehlenden Tripdaten
`UNBEKANNT`. BVG stellt eine geographische Fahrzeugposition nicht zuverlässig
für jede Abfahrt bereit, deshalb ist diese Stopover-Angabe die robustere
Darstellung.

Bei einem vorübergehenden HTTP-Fehler gibt es bis zu drei Versuche mit 0, 2
und 6 Sekunden Wartezeit. Ein Fehler verwirft bereits erfolgreich geladene
Daten nicht; ein `!` in der BVG-Kopfzeile kennzeichnet dann den alten Stand.
Status, Dauer und Versuchsnummer werden seriell protokolliert. Wie im
übernommenen Prototyp verwendet der HTTPS-Client derzeit `setInsecure()`.

## Aktualisierungszyklen

| Zyklus | Intervall | Aktion |
|---|---:|---|
| Gemeinsamer Minutenzyklus | 60 Sekunden | BVG-Abfahrten, Fahrzeughalte, Countdown, HomePilot und Uhrzeit gemeinsam aktualisieren; Vollbild-Fast-Update |
| OpenWeather | 10 Minuten | Prognose aktualisieren und bestätigten vollständigen Löschzyklus ausführen |

Das Projekt verwendet ausdrücklich keinen fensterbasierten Partial Refresh.
Die Minutenzyklen übertragen den gesamten 27.200-Byte-Framebuffer und starten
den auf der Hardware bestätigten schnellen Bildaufbau. Alle zehn Minuten wird
wie in Projekt 71 vollständig gelöscht und neu aufgebaut.

## Wetterdaten

Das rechte Drittel ist unverändert aus Projekt 71 übernommen:

- Datum, Uhrzeit und aktuelle HomePilot-Außentemperatur;
- aktueller Zustand als Noto-Emoji-Piktogramm;
- heutige Tag-/Nachttemperatur, Tief/Hoch, Sonnenzeiten und erster Regen;
- morgige Tag-/Nachttemperatur und erster Regen;
- lokale HomePilot-Daten jede Minute und OpenWeather Forecast 5 höchstens
  alle zehn Minuten.

## Konfiguration

- WLAN: `birnensaft`;
- ESP32: `192.168.187.195/24`;
- Gateway und primärer NTP-Server: `192.168.187.1`;
- DNS: `192.168.187.10`, Fallback `192.168.187.1`;
- HomePilot: `http://192.168.187.60/v4/devices/?devtype=Sensor`;
- OpenWeather: Berlin, 52.482781 / 13.603978.

Diese ignorierten LittleFS-Quelldateien wurden mit dem Projektstand kopiert
und dürfen nicht committet werden:

```text
data/wifi-password.txt
data/openweather.txt
```

## Build, Dateisystem und Upload

Die folgenden Befehle werden aus `projects/` ausgeführt:

```bash
pio run -d ./80-bvg-weather

pio run -d ./80-bvg-weather -t uploadfs \
  --upload-port /dev/cu.wchusbserial110

pio run -d ./80-bvg-weather -t upload \
  --upload-port /dev/cu.wchusbserial110

pio device monitor -d ./80-bvg-weather \
  --port /dev/cu.wchusbserial110 --baud 115200
```

Vor dem Upload den aktuell vorhandenen Gerätenamen mit `pio device list`
prüfen. Der lokale Build bestätigt nur die Übersetzbarkeit; Bildaufbau,
Netzwerkantworten und Kontrast müssen anschließend auf der realen Hardware
getestet werden.
