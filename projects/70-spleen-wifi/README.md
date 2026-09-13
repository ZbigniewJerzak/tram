# 70-spleen-wifi

WLAN- und Sensordaten-Test für das Elecrow CrowPanel ESP32-S3 5,79-Zoll
E-Paper HMI. Das Projekt basiert auf `65-font-spleen` und behält dessen
verifizierten SSD1683-x2-Treiber, Displayausrichtung und Spleen-Renderer bei.

## Funktion

Beim Start führt die Firmware genau einen Testlauf aus:

1. 27.200 Byte großen Displaybuffer im PSRAM anlegen.
2. LittleFS einbinden und `/wifi-password.txt` lesen.
3. Mit dem WLAN `birnensaft` verbinden.
4. Die statische Adresse `192.168.187.190/24` verwenden.
5. `http://192.168.187.60/v4/devices/?devtype=Sensor` abrufen.
6. Außenwetter- und Rauchmelderdaten aus der HomePilot-Antwort auswählen.
7. Das Ergebnis mit Spleen auf dem E-Paper darstellen.
8. Display in Deep Sleep und WLAN ausschalten.

Der ESP32 fragt das Hub nur einmal nach jedem Start oder Reset ab. Das ist für
diesen Test absichtlich einfacher und verhindert unnötige E-Paper-Refreshes.

## Hinweis zur statischen IP

In der Anforderung stand `198.168.187.190`. Da das Zielgerät, Gateway und
lokale Netz im Bereich `192.168.187.0/24` liegen, verwendet dieses Projekt
`192.168.187.190`. Mit `198.168.187.190/24` wäre das Hub unter
`192.168.187.60` ohne eine besondere Routerkonfiguration nicht direkt
erreichbar.

Die Netzparameter stehen am Anfang von `src/main.cpp`:

```cpp
const IPAddress STATIC_IP(192, 168, 187, 190);
const IPAddress GATEWAY(192, 168, 187, 1);
const IPAddress SUBNET(255, 255, 255, 0);
const IPAddress DNS_SERVER(192, 168, 187, 1);
```

Falls der Router eine andere Gateway-Adresse verwendet, müssen `GATEWAY` und
`DNS_SERVER` angepasst werden.

## Erkannte Sensordaten

Das HomePilot-Format enthält ein `meters`-Array mit einem `readings`-Objekt pro
Sensor. Die Firmware sucht nicht nach festen Geräte-IDs.

Als Außensensor wird der Eintrag mit den meisten dieser Messwerte ausgewählt:

- `temperature_primary`;
- `wind_speed`;
- `rain_detected`;
- `sun_brightness`.

Rauchmelder werden anhand ihres Namens oder eines Reading-Schlüssels erkannt,
der `smoke`, `rauch`, `fire`, `brand` oder `alarm` enthält. Bei mehreren
Meldern wird zusätzlich das bekannte DuoFern-Rauchmelder-Modell `32001664`
erkannt. Die Oberfläche zeigt die Anzahl, den niedrigsten bekannten
Batteriestand, eine gemeinsame Batteriewarnung und einen Alarm an, sobald
mindestens ein Melder Alarm meldet.

Der reale Hub wurde am 13. September 2026 erfolgreich abgefragt. Die Antwort
bestätigte einen Umweltsensor mit allen vier erwarteten Wetterfeldern und vier
DuoFern-Rauchmelder mit `readings.smoke_detected`. Die Auswahl- und
Aggregationslogik passt damit zum tatsächlich vorhandenen Datenformat.
Unbekannte zukünftige Alarmwerte werden weiterhin ausdrücklich als
`STATUS ?` angezeigt und niemals stillschweigend als sicher interpretiert.

Der verifizierte Testdatensatz ergab:

- Außentemperatur `20.2 °C`;
- Wind `0.0 m/s`;
- kein Regen;
- Helligkeit `6000`;
- vier gültige Rauchmelder ohne Alarm;
- niedrigster Batteriestand `44 %` und keine Batteriewarnung.

## Passwort und LittleFS

Das Passwort wurde wie angefordert aus
`projects/51-bvg-departure/data/wifi-password.txt` nach
`data/wifi-password.txt` kopiert. Die Datei ist in `.gitignore` eingetragen
und darf nicht committet werden.

LittleFS muss mindestens einmal vor der Firmware hochgeladen werden. Beim
Ändern des Passworts ist `uploadfs` erneut erforderlich.

## Build und Upload

Die folgenden Befehle werden aus dem Repository-Unterverzeichnis `projects/`
ausgeführt. Zuerst das Projekt bauen:

```bash
pio run -d ./70-spleen-wifi
```

Dann Passwortdatei und Firmware nacheinander hochladen:

```bash
pio run -d ./70-spleen-wifi \
  -t uploadfs \
  --upload-port /dev/cu.wchusbserial110

pio run -d ./70-spleen-wifi \
  -t upload \
  --upload-port /dev/cu.wchusbserial110
```

Seriellen Monitor öffnen:

```bash
pio device monitor \
  -d ./70-spleen-wifi \
  --port /dev/cu.wchusbserial110 \
  --baud 115200
```

Der Gerätename kann sich nach erneutem Anschließen ändern. Bei Erfolg endet
die Ausgabe mit:

```text
Sensortest abgeschlossen; Display ist im Deep Sleep.
```

## Erwartetes Display

Die linke Karte zeigt Temperatur, Wind, Regen, Helligkeit und Gültigkeit des
Außensensors. Die rechte Karte zeigt den zusammengefassten Zustand der
Rauchmelder. Die untere Zeile zeigt die statischen IP-Adressen von ESP32 und
Hub.

Bei Konfigurations-, WLAN-, HTTP- oder JSON-Problemen wird stattdessen ein
deutliches Fehlerbild dargestellt. Das Passwort wird weder auf dem Display
noch über die serielle Schnittstelle ausgegeben.
