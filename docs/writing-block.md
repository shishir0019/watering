# ESP32 Automatic Watering System
## Project Requirements & Technical Specification

**Project Type:** Embedded / IoT-ready irrigation controller  
**Hardware Platform:** ESP32 + DS3231 RTC + up to 4 relays  
**Programming Language:** C (firmware), HTML/CSS/JavaScript (website)  
**Framework:** ESP-IDF  
**Version:** 2.0  
**Operating Mode:** Scheduled watering for up to 4 pumps, power-loss tolerant, password-protected local website

---

## 1. Project Overview

The ESP32 Automatic Watering System controls up to 4 water pumps from one ESP32. Each pump has its own daily watering schedules (for example, every day at 06:00 for 30 seconds).

The ESP32 hosts a small website on the home Wi-Fi network. From an Android phone on the same router, the owner logs in with a password, sees all pumps, opens a pump's detail page, manages its schedules, and starts or stops it manually.

The system must survive frequent power interruptions (load shedding). A battery-backed DS3231 real-time clock (RTC) keeps time while power is off, and all settings, schedules, and run history are stored in flash. Scheduled watering never depends on Wi-Fi: if the router is off, pumps still run on schedule.

The system does not require internet access, a cloud server, a database, sensors, or an installed mobile app.

### Version history

| Area | 1.0 | 1.1 | 1.2 | 2.0 |
|---|---|---|---|---|
| Pumps | 1 | 1 | 1 | Up to 4 |
| Schedule | Fixed 8 h interval | Fixed 8 h interval | Fixed 8 h interval | Daily time + duration, per pump, editable |
| Power loss | Timer restarts | Schedule continues | Same as 1.1 | Missed runs caught up |
| Phone access | None | None | Status, log, manual (PIN) | Login, pumps, schedules, manual |

## 2. Project Objectives

- Water up to 4 areas automatically at owner-defined times.
- Keep watering correct despite frequent power cuts.
- Let the owner manage pumps and schedules from an Android phone.
- Protect the controls with a password.
- Run each pump for a controlled duration and stop it automatically.
- Keep the implementation simple, reliable, and easy to maintain.

## 3. Functional Requirements

### Website

| ID | Requirement | Description |
|---|---|---|
| FR-01 | Login | All pages and API calls require login with the admin password. |
| FR-02 | Session | A successful login creates a session that lasts 30 days and survives ESP32 restarts. |
| FR-03 | Logout | The owner can log out; the session is deleted. |
| FR-04 | Change password | The owner can change the password after entering the current one. |
| FR-05 | Login protection | After 5 wrong passwords, login is blocked for 1 minute. |
| FR-06 | Pump list | Show all configured pumps with name, state (running / idle), time left if running, next scheduled run, and last run. |
| FR-07 | Pump detail page | Show one pump's status, manual controls, schedules, and recent log. |
| FR-08 | Add schedule | Add a schedule to a pump from its detail page: start time, duration, weekdays, enabled. |
| FR-09 | Edit / delete schedule | Edit, enable/disable, or delete an existing schedule. |
| FR-10 | Manual start | Start a pump from its detail page for a chosen duration (5–600 s, default 30 s). |
| FR-11 | Manual stop | Stop a running pump immediately from its detail page. |
| FR-12 | Rename pump | Change a pump's display name (for example, "Front garden"). |
| FR-13 | Live status | Pump list and detail page refresh every 3 seconds. |

### Pump control and scheduling

| ID | Requirement | Description |
|---|---|---|
| FR-14 | Multiple pumps | Support 1–4 pumps, each on its own relay. |
| FR-15 | Scheduled runs | Start a pump at each enabled schedule's time on the selected weekdays and stop it after the duration. |
| FR-16 | Schedule limits | Up to 8 schedules per pump. |
| FR-17 | One pump at a time | By default only one pump runs at a time. Scheduled runs that overlap are queued and run one after another. |
| FR-18 | Maximum run time | No single run may exceed 600 seconds. |
| FR-19 | Safe startup | All relays are initialized to pump-OFF. |
| FR-20 | Wi-Fi independence | Scheduled watering works with Wi-Fi or the router unavailable. |

### Power-loss tolerance

| ID | Requirement | Description |
|---|---|---|
| FR-21 | Persistent data | Pumps, schedules, last run times, password, and sessions are stored in flash. |
| FR-22 | Power-loss timekeeping | The DS3231 RTC keeps real time while power is off. |
| FR-23 | Missed run catch-up | If a pump's schedule was missed during an outage within the last 4 hours, run it **once** after power returns. |
| FR-24 | Interrupted run recovery | If power fails while a pump is running, repeat that run once after power returns. |
| FR-25 | Power settle delay | Wait 2 minutes after startup before any catch-up or retry run. |
| FR-26 | Time sync | Set the RTC from the phone's clock when logged in, and from SNTP if internet is available. |
| FR-27 | Invalid time | If the RTC time is lost, pause schedules, show a warning on every page, and resume after the time is synced. Manual control still works. |

## 4. Schedules

### Schedule fields

| Field | Values | Example |
|---|---|---|
| Start time | `HH:MM`, 24-hour, local time | `06:00` |
| Duration | 5–600 seconds | `30` |
| Weekdays | Any of Mon–Sun (at least one) | Every day |
| Enabled | On / Off | On |

- Times are local time. The time zone is set at build time (default Bangladesh, UTC+6, no daylight saving).
- A schedule runs once per selected day at its start time.
- Two schedules on the same pump at the same time are not allowed.
- Validation errors are shown on the form; invalid schedules are never saved.

### Default configuration (first boot)

| Pump | Name | Schedules |
|---|---|---|
| 1 | Pump 1 | 06:00, 14:00, 22:00 — 30 s — every day |
| 2–4 | Pump 2–4 | None (disabled until configured) |

This matches the earlier "every 8 hours for 30 seconds" behavior.

### Run sources

| Source | Trigger |
|---|---|
| `schedule` | A schedule's start time is reached |
| `manual` | Started from the website |
| `catchup` | A schedule was missed during an outage |
| `retry` | A run was interrupted by a power cut |

### Run rules

- If a pump is already running when one of its schedules is due, the schedule is skipped and logged.
- If another pump is running (one-at-a-time mode), scheduled, catch-up, and retry runs wait in a queue and start when it finishes.
- Manual start is rejected while another pump is running; the website shows which pump is running.
- Manual stop also removes that pump's waiting runs from the queue.
- Manual runs do not change the schedules.

## 5. Hardware Requirements

| Component | Purpose |
|---|---|
| ESP32 development board | Runs the control program and website |
| DS3231 RTC module with CR2032 coin cell | Keeps time during power outages |
| 1–4 channel relay module | Switches each pump ON and OFF |
| 1–4 water pumps | Supply water to each irrigation line |
| Suitable power supply | Powers the ESP32 and pumps |
| Water tubing | Delivers water to each target area |
| Electrical enclosure | Protects electronics from moisture |
| Wi-Fi router (existing) | Connects the ESP32 and the phone |
| Android phone with Chrome | Opens the website |

The power supply must handle the starting current of the pumps that can run at the same time (one pump by default). The enclosure must not block the Wi-Fi signal (avoid fully metal enclosures).

### Pin configuration

| Signal | GPIO |
|---|---|
| Relay 1 (Pump 1) | GPIO 26 |
| Relay 2 (Pump 2) | GPIO 27 |
| Relay 3 (Pump 3) | GPIO 25 |
| Relay 4 (Pump 4) | GPIO 33 |
| RTC SDA | GPIO 21 |
| RTC SCL | GPIO 22 |

- These pins are not boot-strapping pins and do not conflict with Wi-Fi.
- Relay logic is configurable as active-LOW or active-HIGH (same for all relays).
- For active-LOW relays, add an external pull-up on each relay pin so relays stay OFF during boot.
- Relay inputs must be compatible with 3.3V GPIO logic; use a driver if needed.
- Power the DS3231 at 3.3V.

## 6. Website

### Access

- Reserve a fixed IP for the ESP32 on the router (DHCP reservation), for example `192.168.1.50`.
- On the phone, open `http://192.168.1.50` in Chrome and choose **Add to Home screen**.
- The website is a single page (hash-based navigation), embedded in the firmware, with no external files or CDN.

### Pages

| Page | Path | Content |
|---|---|---|
| Login | `#/login` | Password field, login button, error / lockout message |
| Pumps | `#/pumps` | One card per pump |
| Pump detail | `#/pump/{id}` | Status, manual control, schedules, recent log |
| Settings | `#/settings` | Change password, clock status, system log, logout |

### Login

```text
┌──────────────────────────────┐
│     Watering Controller      │
│                              │
│  Password [ ••••••••      ]  │
│  [          LOG IN       ]   │
│                              │
│  Wrong password (3 left)     │
└──────────────────────────────┘
```

### Pumps

```text
┌──────────────────────────────┐
│  Pumps                   ⚙   │
│ ┌──────────────────────────┐ │
│ │ Front garden    ● RUNNING│ │
│ │ 18 s left                │ │
│ │ Next: Today 14:00        │ │
│ └──────────────────────────┘ │
│ ┌──────────────────────────┐ │
│ │ Roof plants       ○ Idle │ │
│ │ Last: Today 06:00 (30 s) │ │
│ │ Next: Today 18:00        │ │
│ └──────────────────────────┘ │
└──────────────────────────────┘
```

Tapping a card opens the pump detail page.

### Pump detail

```text
┌──────────────────────────────┐
│ ← Front garden          ✎   │
│ ● RUNNING  (18 s left)       │
│ Last run: Today 06:00 (30 s) │
│ Next run: Today 14:00        │
│                              │
│ Manual                       │
│ Duration [ 30 ] s            │
│ [   START   ]  [   STOP   ]  │
│                              │
│ Schedules              [ + ] │
│ 06:00  30 s  Every day  [■]  │
│ 14:00  30 s  Every day  [■]  │
│ 22:00  45 s  Mon,Wed,Fri [□] │
│                              │
│ Log                          │
│ 06:00:30 Stopped (30 s)      │
│ 06:00:00 Started (schedule)  │
└──────────────────────────────┘
```

- **[■]/[□]** toggles a schedule on or off.
- Tapping a schedule opens the edit form; **[ + ]** opens an empty form.
- **START** is disabled while this pump runs; **STOP** is disabled while it is idle.

### Schedule form

```text
┌──────────────────────────────┐
│ Add schedule                 │
│ Start time  [ 06:00 ]        │
│ Duration    [ 30 ] s         │
│ Days  [M][T][W][T][F][S][S]  │
│ Enabled     [■]              │
│                              │
│ [ DELETE ]  [CANCEL] [SAVE]  │
└──────────────────────────────┘
```

**DELETE** appears only when editing and asks for confirmation.

### General behavior

- If the session expires or the ESP32 restarts with an invalid session, the website returns to the login page.
- If the ESP32 is unreachable, pages show "Offline" instead of stale data.
- If the clock is invalid, a red banner shows "Clock not set — schedules paused".
- Buttons are disabled while a request is in progress.
- The layout is designed for phone screens.

## 7. REST API

All endpoints except `POST /api/login` require a valid session cookie; otherwise they return `401`. Request and response bodies are JSON.

### Authentication

| Method | Path | Body | Response |
|---|---|---|---|
| POST | `/api/login` | `{"password": "..."}` | `200` + session cookie, `401` wrong password, `429` locked out |
| POST | `/api/logout` | — | `200` |
| POST | `/api/password` | `{"current": "...", "new": "..."}` | `200`, `400` too short, `401` wrong current password |

### Pumps

| Method | Path | Body | Response |
|---|---|---|---|
| GET | `/api/pumps` | — | List of pumps with status |
| GET | `/api/pumps/{id}` | — | Pump status and its schedules |
| PATCH | `/api/pumps/{id}` | `{"name": "Front garden"}` | `200`, `400` invalid name |
| POST | `/api/pumps/{id}/start` | `{"seconds": 30}` | `200`, `400` invalid, `409` a pump is running |
| POST | `/api/pumps/{id}/stop` | — | `200` (also if already stopped) |

### Schedules

| Method | Path | Body | Response |
|---|---|---|---|
| POST | `/api/pumps/{id}/schedules` | Schedule | `201` with new `id`, `400` invalid, `409` limit or duplicate time |
| PUT | `/api/pumps/{id}/schedules/{sid}` | Schedule | `200`, `400`, `404`, `409` duplicate time |
| DELETE | `/api/pumps/{id}/schedules/{sid}` | — | `200`, `404` |

### Logs and system

| Method | Path | Body / query | Response |
|---|---|---|---|
| GET | `/api/logs` | `?pump={id}&limit=50` (max 200; omit `pump` for all) | Events, newest first |
| GET | `/api/system` | — | Clock, time zone, uptime, Wi-Fi signal, firmware version |
| POST | `/api/time` | `{"unix": 1760860800}` | `200` |

### Examples

**`GET /api/pumps/1`**

```json
{
  "id": 1,
  "name": "Front garden",
  "running": true,
  "remaining_s": 18,
  "source": "schedule",
  "queued": false,
  "last_run": { "start": 1760839200, "seconds": 30, "source": "schedule" },
  "next_run": 1760868000,
  "schedules": [
    { "id": 1, "time": "06:00", "seconds": 30, "days": [1,2,3,4,5,6,7], "enabled": true },
    { "id": 2, "time": "14:00", "seconds": 30, "days": [1,2,3,4,5,6,7], "enabled": true }
  ]
}
```

`days`: 1 = Monday … 7 = Sunday. All times are Unix seconds (UTC); the website shows them in the phone's local time.

## 8. Security

- **Password storage:** salted SHA-256 hash (mbedTLS) in NVS. The plain password is never stored.
- **Initial password:** set at build time in menuconfig. The website shows a reminder to change it until it is changed.
- **Password rules:** at least 8 characters.
- **Sessions:** random 128-bit token in an `HttpOnly`, `SameSite=Strict` cookie, valid 30 days. Up to 5 sessions are stored in NVS (as hashes), so logins survive power cuts. The oldest session is removed when a sixth login occurs. Changing the password logs out all other sessions.
- **Brute force:** 5 wrong passwords → login blocked for 1 minute (all clients).
- **CSRF:** `SameSite=Strict` cookies, and state-changing requests must use `Content-Type: application/json`.
- **Network:** the website is for the local network only. **Do not port-forward** the ESP32 on the router.
- **Limitation:** HTTP is not encrypted. Someone with access to the same Wi-Fi and network-sniffing tools could capture the password. Use a strong Wi-Fi password (WPA2/WPA3). HTTPS is out of scope.
- **Password reset:** if the password is forgotten, reflash the firmware with "reset password" enabled in menuconfig, or erase NVS (this also erases schedules).

## 9. Startup and Power-Loss Behavior

### Startup sequence

1. Initialize all relay GPIOs to pump-OFF.
2. Initialize NVS, log storage, and the RTC.
3. Validate the RTC (see below). Log `BOOT`.
4. Load pumps, schedules, last runs, and in-progress flags.
5. Start Wi-Fi and the web server in the background (non-blocking).
6. Start the controller task.
7. Wait the 2-minute settle delay, then queue retry and catch-up runs.
8. Run schedules normally.

### Catch-up rules

For each enabled pump, after the settle delay:

1. If the pump's `in_progress` flag is set, queue one `retry` run with the interrupted run's duration.
2. Otherwise, find the most recent scheduled start time in the last 4 hours. If the pump has not run since then, queue one `catchup` run with that schedule's duration.
3. Older missed runs are skipped and logged as `MISSED`.

| Scenario | Behavior |
|---|---|
| Outage ends before any schedule | No extra runs. |
| One schedule missed (within 4 h) | Runs once after the settle delay. |
| Several schedules missed | Only the most recent one within 4 h runs, once. |
| Schedule missed more than 4 h ago | Skipped and logged; next schedule runs normally. |
| Power fails during a run | The run is repeated once after the settle delay. |
| Power flickers repeatedly | The settle delay restarts each boot; no catch-up until power is stable for 2 minutes. |
| A schedule is due during the settle delay | It is queued and runs after the settle delay. |
| ESP32 powered, router off | Schedules run normally; website unavailable until the router returns. |

### RTC validation

The RTC time is invalid if:

- The DS3231 oscillator-stop flag (OSF) is set (coin cell dead or missing), or
- The RTC does not respond on I2C, or
- The RTC time is earlier than the firmware build date.

When the time is invalid:

1. Schedules and catch-up are paused. Manual control still works.
2. `RTC_FAULT` is logged and a banner is shown on every page.
3. When the owner logs in, the website sends the phone's time; the ESP32 sets the RTC (or its internal clock if the RTC is missing), clears OSF, and resumes schedules.
4. Catch-up rules are applied once after the time becomes valid.

### Time synchronization

- After login and every 10 minutes while the website is open, the website sends the phone's time. The ESP32 updates the clock if the difference is more than 30 seconds.
- If the router has internet, the ESP32 also syncs with `pool.ntp.org`.
- A time change does not trigger runs for schedule times that were skipped by the change.
- `TIME_SET` is logged with the difference.

## 10. Software Requirements

### Development environment

- ESP-IDF, C, FreeRTOS
- ESP-IDF GPIO and I2C drivers
- NVS (`nvs_flash`)
- Wi-Fi station mode and `esp_netif`
- `esp_http_server` (with wildcard URI matching)
- `esp_sntp`
- mbedTLS (password hashing, random tokens)
- cJSON (included with ESP-IDF)
- LittleFS for the event log
- Plain HTML, CSS, and JavaScript for the website (single file, embedded in the firmware)

### Project structure

```text
automatic-watering/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig
└── main/
    ├── CMakeLists.txt
    ├── Kconfig.projbuild
    ├── main.c
    ├── relay.c / relay.h
    ├── rtc_ds3231.c / rtc_ds3231.h
    ├── clock.c / clock.h
    ├── store.c / store.h
    ├── scheduler.c / scheduler.h
    ├── controller.c / controller.h
    ├── event_log.c / event_log.h
    ├── auth.c / auth.h
    ├── wifi.c / wifi.h
    ├── web_server.c / web_server.h
    ├── api.c / api.h
    └── web/
        └── index.html
```

| Module | Responsibility |
|---|---|
| `relay` | Relay GPIO setup and ON/OFF per pump, configurable polarity |
| `rtc_ds3231` | DS3231 driver: read/write time, OSF flag |
| `clock` | Current time, validity, time zone, SNTP and phone sync |
| `store` | NVS: pumps, schedules, last runs, in-progress flags |
| `scheduler` | Decide which schedules are due; catch-up calculation |
| `controller` | Single task that owns all relays: run queue, timers, manual commands |
| `event_log` | Ring file of events in LittleFS |
| `auth` | Password hash, login lockout, sessions |
| `wifi` | Station connection and automatic reconnect |
| `web_server` | HTTP server, static website, session check |
| `api` | REST endpoint handlers and JSON |

Only the `controller` task switches relays. Web handlers send commands to it through a FreeRTOS queue. The controller loop wakes at least once per second to update timers and check schedules.

### Configuration (menuconfig)

| Option | Default |
|---|---|
| Wi-Fi SSID / password | — |
| Initial admin password | — (required) |
| Reset password on boot | Off |
| Number of pumps | 1 |
| Relay active level | LOW |
| Max pumps running at once | 1 |
| Time zone (POSIX TZ) | `<+06>-6` |
| Settle delay (s) | 120 |
| Catch-up window (h) | 4 |
| Max run time (s) | 600 |

### Persistent data

**NVS**

| Key | Content |
|---|---|
| `pump_{n}` | Name |
| `sched_{n}` | Up to 8 schedules (packed: id, minute of day, seconds, weekday mask, enabled) |
| `last_{n}` | Last run start time, seconds, source |
| `busy_{n}` | In-progress flag and requested seconds |
| `auth_pw` | Salt + password hash |
| `auth_sess` | Up to 5 session token hashes with expiry |

**Event log (LittleFS)** — fixed-size records in a 500-record ring file.

| Event | Pump | Value |
|---|---|---|
| `BOOT` | — | — |
| `PUMP_START` | yes | Source |
| `PUMP_END` | yes | Seconds run |
| `PUMP_STOPPED` | yes | Seconds run (manual stop) |
| `SCHEDULE_SKIPPED` | yes | Reason (already running) |
| `MISSED` | yes | Schedule time |
| `SCHEDULE_CHANGED` | yes | Added / edited / deleted |
| `LOGIN_OK` / `LOGIN_FAIL` | — | — |
| `PASSWORD_CHANGED` | — | — |
| `RTC_FAULT` | — | — |
| `TIME_SET` | — | Difference in seconds |

Flash writes are event-driven (a few dozen per day), which is negligible for wear.

## 11. System Architecture

```text
                        Home Wi-Fi Router
                       /                 \
  Android phone (Chrome)                   ESP32
  http://192.168.1.50        ┌──────────────────────────────┐
           ──── HTTP ───────>│ web_server ─ auth ─ api      │
                             │                 │ queue       │
 DS3231 RTC <──I2C──────────>│ clock ── scheduler ─> controller ──> store / event_log
 (coin cell)                 └────────────────────────┬─────┘
                                                      │ GPIO 26 / 27 / 25 / 33
                                                      v
                                           Relay 1  Relay 2  Relay 3  Relay 4
                                              │        │        │        │
                                           Pump 1   Pump 2   Pump 3   Pump 4
```

The ESP32 controls the relays. The relays switch the pumps' power circuits. The ESP32 must never supply pump current directly through a GPIO pin.

## 12. Error Handling and Safety

- Keep all pumps OFF during initialization.
- Enable the ESP32 brownout detector.
- Never run a pump longer than the configured maximum (600 s), regardless of the request.
- Run only the allowed number of pumps at once (default 1).
- A Wi-Fi, web server, or log failure must never stop or block scheduled watering.
- Enable the task watchdog for the controller task; on reset, all relays start OFF.
- Use relays rated for each pump's operating and starting current.
- Use appropriate overcurrent protection.
- Protect the electronics from water, humidity, and condensation.
- Use suitable isolation and an enclosed switching arrangement for mains-powered pumps.
- Verify relay polarity before operating the pumps.
- Replace the RTC coin cell according to its rated life (typically several years).

**Known limitations:** No detection of an empty tank, blocked pipe, dry-running pump, or failed relay. Pumps cannot run during a power outage. The website works only on the home Wi-Fi.

## 13. Testing and Acceptance Criteria

### Login and website

| Test ID | Test | Expected result |
|---|---|---|
| TC-01 | Open the website without logging in | Login page is shown; API calls return `401`. |
| TC-02 | Log in with the correct password | Pump list is shown. |
| TC-03 | Enter a wrong password 5 times | Login is blocked for 1 minute (`429`). |
| TC-04 | Restart the ESP32, reopen the website | Still logged in. |
| TC-05 | Log out | Login page is shown; old cookie returns `401`. |
| TC-06 | Change the password | New password works; old password fails; other sessions are logged out. |
| TC-07 | View the pump list | All configured pumps show correct state, last run, and next run. |
| TC-08 | Rename a pump | New name appears on the list and detail page. |

### Manual control

| Test ID | Test | Expected result |
|---|---|---|
| TC-09 | Start a pump for 30 s | Pump runs 30 s and stops; `PUMP_START`/`PUMP_END` logged. |
| TC-10 | Stop a running pump | Pump stops within 1 second; `PUMP_STOPPED` logged. |
| TC-11 | Start a pump while another is running | Rejected (`409`) with the running pump's name shown. |
| TC-12 | Request 700 s via the API | Rejected (`400`); pump does not run. |

### Schedules

| Test ID | Test | Expected result |
|---|---|---|
| TC-13 | Add a schedule 2 minutes ahead | Pump runs at that time for the set duration. |
| TC-14 | Disable that schedule | Pump does not run at that time. |
| TC-15 | Schedule on selected weekdays only | Runs only on those days. |
| TC-16 | Add a 9th schedule or a duplicate time | Rejected with a clear message. |
| TC-17 | Edit and delete schedules | Changes are applied and survive a restart. |
| TC-18 | Two pumps scheduled at the same time | They run one after another. |
| TC-19 | Turn the router off | Schedules still run. |

### Power loss and clock

| Test ID | Test | Expected result |
|---|---|---|
| TC-20 | Cut power across one schedule (< 4 h) | After power returns, that run happens once after the settle delay. |
| TC-21 | Cut power across several schedules | Only the latest missed run happens, once. |
| TC-22 | Cut power for more than 4 h past a schedule | No catch-up; `MISSED` logged. |
| TC-23 | Cut power during a run | Run is repeated once after the settle delay. |
| TC-24 | Toggle power rapidly | No catch-up until power is stable for 2 minutes. |
| TC-25 | Remove the coin cell and cut power | Banner shown; schedules paused; manual works; after login, time is set and schedules resume. |
| TC-26 | Set the phone time 1 hour different from the RTC, open the website | Clock corrected; `TIME_SET` logged; no extra runs. |
| TC-27 | Cut power, restore, open a pump's log | `BOOT` and catch-up events appear with correct times. |

Timing tests should account for scheduling and relay delays. Power-loss tests can use a shortened settle delay and catch-up window.

## 14. Implementation Plan

### Phase 1 — Hardware
- Prepare the ESP32, DS3231, relay module, pumps, and power supply.
- Wire relays to GPIO 26, 27, 25, 33 (with pull-ups for active-LOW) and the RTC to GPIO 21/22.
- Verify all pumps are OFF before connecting pump power.

### Phase 2 — Core firmware
- Project setup: Kconfig, partition table (NVS + LittleFS).
- Relay, RTC, clock, store, and event log modules.
- Scheduler and controller with run queue, catch-up, and retry.
- Brownout detector and task watchdog.

### Phase 3 — Web server and API
- Wi-Fi with automatic reconnect.
- HTTP server, auth (password, sessions, lockout).
- REST API for pumps, schedules, logs, time, and system.

### Phase 4 — Website
- Login, pump list, pump detail, schedule form, and settings pages.
- Status polling, offline and clock-warning states.
- Gzip and embed in the firmware.

### Phase 5 — Testing
- Run TC-01 to TC-27 with shortened timings, then with real timings.

### Phase 6 — Deployment
- Install the enclosure within good Wi-Fi range; secure pumps and tubing.
- Reserve the ESP32's IP on the router; add the website to the phone's home screen.
- Change the initial password.
- Run through real load-shedding periods and review the logs.

## 15. Out of Scope for 2.0

- Access from outside the home network (internet / cloud)
- HTTPS
- Multiple user accounts
- Push notifications
- Native Android app (APK)
- More than 4 pumps or multiple ESP32 controllers
- Interval-based schedules ("every N hours")
- Wi-Fi setup from the phone (provisioning)
- Over-the-air (OTA) firmware updates
- Battery backup / UPS
- Soil moisture, rain, or water-level sensors
- Nutrient dosing, MQTT

## 16. Future Enhancements

- OTA firmware updates from the settings page.
- Wi-Fi provisioning from the phone.
- Remote access through MQTT or a cloud service.
- Push notifications (run finished, clock fault, power restored).
- "Skip next run" and "pause all schedules" (vacation / rain).
- Water-level detection and dry-run protection.
- Export log as CSV.

## 17. Definition of Done

- All pumps start OFF.
- The owner can log in, view pumps, open a pump's detail page, manage schedules, and start/stop it manually from an Android phone.
- Schedules run at the correct local time and duration on the selected days.
- Only the allowed number of pumps run at once.
- Missed and interrupted runs are recovered once after power returns.
- Schedules run with the router off.
- Settings, schedules, and logins survive power cuts.
- All acceptance tests (TC-01 to TC-27) pass.
- The installation meets applicable electrical safety requirements.

---

**Final deliverable:** An ESP32 watering controller for up to 4 pumps, written in C using ESP-IDF, with per-pump daily schedules, power-loss recovery backed by an RTC and flash storage, and a password-protected local website for viewing pumps, managing schedules, and manual control.
