# TODO — BVG Departure Display Refactoring

## Priority: High — Constants & Hardcoded Strings

### [ ] TODO-001: Extract `STOP_NAME` constant usage in URL
**Location**: `findStopId()` line ~336
```cpp
// Current (hardcoded):
"/locations?query=Erich-Baron-Weg"

// Should be:
"/locations?query=" + String(STOP_NAME)
```

### [ ] TODO-002: Extract `LANGUAGE` constant
**Location**: Used in `findStopId()` and `fetchDepartures()` (3x)
```cpp
// Add to configuration section:
constexpr char LANGUAGE[] = "de";
```

### [ ] TODO-003: Extract product filter string
**Location**: `fetchDepartures()` lines ~543-545
```cpp
// Current (duplicated in URL and filter):
"&tram=true&suburban=false&subway=false&bus=false"
"&ferry=false&express=false&regional=false"

// Add:
constexpr char TRAM_PRODUCTS[] = 
    "tram=true&suburban=false&subway=false&bus=false"
    "&ferry=false&express=false&regional=false";
```

### [ ] TODO-004: Extract display text strings
**Location**: Various places in `drawDepartureScreen()` and `drawBootLog()`
```cpp
// Add to configuration section:
constexpr char TXT_BOOT_HEADER[] = "BVG Abfahrten";  // was "BVG Tram 62 - Start"
constexpr char TXT_TRAM_DEPARTURES[] = "Tram-Abfahrten";
constexpr char TXT_NO_DEPARTURES[] = "Keine Abfahrten.";
constexpr char TXT_FEHLER[] = "FEHLER: ";
constexpr char TXT_WIFI[] = "WiFi ";
constexpr char TXT_UPDATE[] = "Update ";
constexpr char TXT_DIRECTION[] = "-> ";
constexpr char TXT_MINUTES[] = " Min.)";
```

---

## Priority: Medium — Code Duplication

### [ ] TODO-010: Extract common API parameters
**Location**: `findStopId()` and `fetchDepartures()`
```cpp
// Add:
constexpr char COMMON_API_PARAMS[] = 
    "&remarks=false&linesOfStops=false&language=de&pretty=false";
```

### [ ] TODO-011: Extract JSON filter for departures
**Location**: `fetchDepartures()` lines ~548-556
The filter definition could be a static const to avoid recreation on each call.

### [ ] TODO-012: Extract WiFi status strings
**Location**: `wifiStatusText()` lines ~316-326
These strings are only used internally but could be const for consistency.

---

## Priority: Medium — Logic Issues

### [ ] TODO-020: `parseApiTime()` timezone handling
**Location**: `parseApiTime()` lines ~508-522
The function parses the timestamp but ignores the timezone offset (e.g., `+02:00`). The parsed time will be in local time, but `mktime()` assumes the input is already local time.

**Note**: This works correctly if the API returns UTC times, but the code doesn't explicitly handle the timezone offset in the timestamp.

### [ ] TODO-021: `delayMinutes` rounding edge case
**Location**: `fetchDepartures()` lines ~580-582
```cpp
departure.delayMinutes = delaySeconds >= 0
    ? (delaySeconds + 30) / 60
    : (delaySeconds - 30) / 60;
```
For negative delays (early arrivals), the rounding may produce unexpected results. Consider using `abs()` or explicit rounding.

### [ ] TODO-022: `BOOT_LOG_LINES` magic number
**Location**: `drawBootLog()` line ~196
The value `40` in `clipped(bootLog[i], 40)` should use a constant derived from `BOOT_LOG_LINES` or be explicitly defined.

---

## Priority: Low — Code Quality

### [ ] TODO-030: `WIFI_SSID` is hardcoded
**Location**: Line ~42
```cpp
constexpr char WIFI_SSID[] = "birnensaft";
```
Consider moving to `data/wifi-config.json` or similar for easier configuration.

### [ ] TODO-031: `STOP_NAME` vs `stopDisplayName` naming
**Location**: Global variables
- `STOP_NAME` = constant (hardcoded search term)
- `stopDisplayName` = runtime variable (API-returned name)

The distinction is good, but could be clearer with naming like `STOP_SEARCH_TERM` and `stopDisplayName`.

### [ ] TODO-032: `cachedDepartures[2]` array size
**Location**: Global variable
The magic number `2` appears in multiple places. Consider:
```cpp
constexpr int MAX_DISPLAY_DEPARTURES = 2;
Departure cachedDepartures[MAX_DISPLAY_DEPARTURES];
```

### [ ] TODO-033: `FULL_REFRESH_AFTER_PARTIAL` naming
**Location**: Line ~52
Consider `PARTIAL_REFRESHES_BEFORE_FULL` for clarity.

---

## Priority: Low — Display Layout

### [ ] TODO-040: Header text overlap
**Location**: `drawDepartureScreen()` lines ~245-248
```cpp
display.setCursor(75, 9);   // stopDisplayName
display.setCursor(75, 18);  // "Tram-Abfahrten"
```
Both lines start at x=75. If `stopDisplayName` is short, the text may overlap. Consider adjusting based on text length.

### [ ] TODO-041: Footer position hardcoded
**Location**: `drawDepartureScreen()` line ~284
```cpp
display.setCursor(4, 119);
```
The y-position `119` is hardcoded. Consider deriving from `display.height()`.

### [ ] TODO-042: `drawBootLog()` header text
**Location**: Line ~196
```cpp
display.print("BVG Tram 62 - Start");
```
This hardcodes "Tram 62". Should use a constant or `stopDisplayName`.

---

## Completed Refactoring (for reference)

- [x] Global display driver configuration using CrowPanel pins
- [x] `initializeDisplay()` function for display setup
- [x] `hhmm()` helper for time formatting
- [x] `clipped()` helper for text truncation with German umlaut handling
- [x] `asciiGerman()` for Umlaut replacement
- [x] `resetReasonText()` for human-readable reset reasons
- [x] Boot log with circular buffer
- [x] Partial refresh with periodic full refresh

---

## Suggested Implementation Order

1. Extract all constants (TODO-001 to TODO-004)
2. Extract common API parameters (TODO-010)
3. Fix display layout issues (TODO-040 to TODO-042)
4. Address logic issues (TODO-020 to TODO-022)
5. Code quality improvements (TODO-030 to TODO-033)