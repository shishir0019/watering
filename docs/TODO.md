# TODO — ESP32 Watering Controller 2.0

Spec: [writing-block.md](writing-block.md). FR = functional requirement, TC = test case.
Status: firmware and website written; builds with ESP-IDF v5.3.2. Not yet tested on hardware.

## Phase 1 — Hardware

- [ ] Buy parts from [HARDWARE.md](HARDWARE.md)
- [ ] Fix DS3231 battery charging circuit (remove resistor) or use LIR2032
- [ ] Size power supply for pump starting current (1 pump at a time)
- [ ] Wire relays: GPIO 26 / 27 / 25 / 33 (pull-ups if active-LOW)
- [ ] Wire DS3231: SDA GPIO 21, SCL GPIO 22, 3.3V
- [ ] Confirm relay polarity and all relays OFF at boot (before pump power)
- [ ] Reserve ESP32 IP on router (DHCP reservation)

## Phase 2 — Project setup

- [x] ESP-IDF project structure (§10)
- [x] `Kconfig.projbuild` with all options
- [x] `partitions.csv`: NVS + LittleFS log partition
- [x] `sdkconfig.defaults`: brownout detector, task watchdog, HTTP header sizes
- [x] Firmware builds (`idf.py build`)

## Phase 3 — Core firmware

- [x] `relay`: init all OFF, on/off per pump, polarity (FR-14, FR-19)
- [x] `rtc_ds3231`: read/write time, OSF check/clear
- [x] `clock`: validity check, TZ, phone sync, SNTP (FR-22, FR-26, FR-27)
- [x] `store`: NVS pumps, schedules, last runs, in-progress flags; first-boot defaults (FR-21)
- [x] `event_log`: 500-record ring file in LittleFS
- [x] `scheduler`: due check, next run, catch-up search (FR-15, FR-23) — host unit tests pass
- [x] `controller`: run queue, max run cap, one-at-a-time, settle delay, retry, catch-up, clock-invalid pause (FR-17, FR-18, FR-24, FR-25, FR-27)
- [x] `main.c`: startup sequence (§9)
- [ ] Verify scheduled runs on hardware via serial log with short timings

## Phase 4 — Wi-Fi, auth, API

- [x] `wifi`: station mode, auto-reconnect, non-blocking (FR-20)
- [x] `auth`: salted hash, initial/reset password, sessions in NVS, lockout (FR-01 to FR-05)
- [x] `web_server` + `api`: all endpoints in §7, session check, JSON content-type check
- [ ] Test all endpoints with curl on hardware

## Phase 5 — Website (`main/web/index.html`)

- [x] Login, pumps list, pump detail, schedule form, rename, settings pages
- [x] Polling every 3 s, offline / clock-invalid / default-password banners
- [x] Phone time sync after login and every 10 min
- [ ] Check layout in Android Chrome; Add to Home screen

## Phase 6 — Testing (§13)

- [ ] TC-01–08 Login and website
- [ ] TC-09–12 Manual control
- [ ] TC-13–19 Schedules
- [ ] TC-20–27 Power loss and clock
- [ ] Re-run key tests with real timings

## Phase 7 — Deployment

- [ ] Install in enclosure with good Wi-Fi signal; secure pumps/tubing
- [ ] Electrical safety check (overcurrent protection, isolation)
- [ ] Change initial password
- [ ] Configure real schedules per pump
- [ ] Monitor logs through a week of load shedding
