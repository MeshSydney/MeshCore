# MeshSydney MeshCore — Changes vs Upstream `dev`

This fork tracks [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore) `dev` with the following enhancements.

---

## CLI Command Reference

All `set`/`get` commands below are available on all node types (repeater, companion, room server, sensor) unless noted otherwise. Blacklist commands are repeater-only.

### Flood & Abuse Prevention Settings

| Command | Range | Default | Description |
|---|---|---|---|
| `set flood.path.max <val>` | 0–255 | **12** | Max path hops for flood REQ/RESPONSE/PATH packets. Packets with more hops are dropped. 0 = off |
| `get flood.path.max` | — | — | Show current value |
| `set advert.ratelimit <sec>` | 0–3600 | **0** (off) | Minimum seconds between accepting flood adverts (global, not per-sender) |
| `get advert.ratelimit` | — | — | Show current value |
| `set advert.jail <hrs>` | 0–168 | **12** | Hours a sender must slow down before its "strikes" decay. Repeated over-frequent adverts jail the sender (repeater-only) |
| `get advert.jail` | — | — | Show current value |
| `jail` | — | — | List currently jailed senders (pubkey prefix, strike count, average advert interval) — repeater only |

### Blacklist Commands (Repeater Only)

| Command | Description |
|---|---|
| `blacklist path list` | List all path blacklist entries |
| `blacklist path add <hex>[,<hex>,...]` | Add pubkey-hash prefix(es) to path blacklist (1–4 byte hex, max 16 entries) |
| `blacklist path rem <hex>[,<hex>,...]` | Remove path blacklist entry(ies) |
| `blacklist path clear` | Clear all path blacklist entries |
| `blacklist chan list` | List all channel blacklist entries (hex prefixes and `#name` filters) |
| `blacklist chan add <hex\|#name>[,...]` | Add channel hash prefix(es) or `#channel_name` filter(s) (max 16 hex + 8 name entries) |
| `blacklist chan rem <hex\|#name>[,...]` | Remove channel blacklist entry(ies) |
| `blacklist chan clear` | Clear all channel blacklist entries |

**Command chaining** *(repeater firmware only — not implemented in companion radio, room server, or sensor node)*: multiple CLI commands can be sent in a single message separated by semicolons — each command is executed sequentially and responses are concatenated with semicolon delimiters (see [CLI Commands docs](docs/cli_commands.md#command-chaining)).

---

## 1. Radio / LoRa Configuration (`platformio.ini`)
- **Frequency changed** from 869.618 MHz (EU) → **915.075 MHz**
- **Bandwidth changed** from 62.5 kHz → **125 kHz**
- **Spreading factor changed** from SF8 → **SF9**
- `LORA_CR` now set explicitly to **5** (same value as upstream's implicit default)
- Added a shared `[repeater_settings]` PlatformIO config section so Sydney Mesh's repeater defaults (see section 9) don't need to be duplicated across every variant — envs opt in with `extends = <board>, repeater_settings`

---

## 2. Contact Storage — Dynamic/PSRAM Allocation (`src/helpers/BaseChatMesh.*`)
- `contacts[]` and `sort_array[]` changed from **fixed static arrays** to **heap-allocated pointers**
- On ESP32 with PSRAM, `initContacts()` allocates from PSRAM (enabling much larger `MAX_CONTACTS`)
- Falls back to SRAM with a safe limit (`CONTACTS_SRAM_FALLBACK`, default 200) when PSRAM unavailable
- Reserved "anonymous" contact slots were removed — `num_contacts` now starts at 0 instead of `MAX_ANON_CONTACTS`, and `allocateContactSlot()`/`ContactsIterator` were simplified to match
- Added `getMaxContacts()` API; `getNumContacts()` no longer subtracts the (now removed) anon-slot offset
- CLI text payloads embedded in PATH packets (sent by a repeater via `createPathReturn`) are now parsed and dispatched via `onCommandDataRecv`

---

## 3. ESP32 Companion Radio — Filesystem Migration to LittleFS
- The ESP32 companion radio build now uses **LittleFS** instead of **SPIFFS** for its filesystem
- Per-contact/blob files are unified into a **single blob store file** rather than one file per blob under `/bl/`

---

## 4. BLE Throughput & Adaptive Connection Improvements (`src/helpers/esp32/SerialBLEInterface.*`)
- MTU increased to **512 bytes**
- Prefers **2M PHY** (BLE 5.0) for all connections, doubling the air data rate when the peer supports it; falls back to 1M PHY automatically otherwise
- **Adaptive connection interval** via a new `setFastMode(bool)` hook on `BaseSerialInterface`: fast mode (7.5–15 ms interval) during active sync (contacts/channel streaming, `CMD_SYNC_NEXT_MESSAGE`, `CMD_GET_CHANNEL`) vs idle mode (45–90 ms) once sync completes — reduces power use without hurting sync speed
- Connection-parameter and PHY updates are deferred to `checkRecvFrame()` (main loop context) instead of being issued from inside the Bluedroid BLE task callback, which previously could deadlock the BLE stack for several seconds
- Notifications sent every **8 ms** minimum (`BLE_WRITE_MIN_INTERVAL`, was 60 ms)

---

## 5. Radio Wrapper Enhancements (`src/helpers/radiolib/`)
- **`CustomSX1262Wrapper.h`**: Added `RSSI_OFFSET` define (default 0) — applied to both current and last RSSI readings to compensate for external LNA gain (e.g. Heltec V4's GC1109 LNA)
- **`RadioLibWrappers.h`**: `preambleLengthForSF()` threshold raised from `sf <= 8` to **`sf <= 9`** — SF9 now also gets the longer 32-symbol preamble instead of the 16-symbol one

---

## 6. Telemetry — Backup Battery/Power Sensor Fallback (`src/helpers/SensorManager.h`, `EnvironmentSensorManager.*`)
- New `SensorManager::getSelfMilliVolts()` / `addSelfPower()` helpers: when a board's own ADC reports no battery voltage (`getBattMilliVolts() == 0` — e.g. the Station G3 ESP32, which always returns 0), the reported/telemetry "self" voltage now falls back to a board-provided `getBackupBattReadings()` sensor reading instead of just reporting 0
- `EnvironmentSensorManager` implements this fallback using the **INA219** power monitor (enabled by default via `[sensor_base]`); when used as the fallback, voltage/current/power are reported on `TELEM_CHANNEL_SELF` and the INA219's own telemetry channel is skipped in that response to avoid double-reporting the same reading
- Wired into every node type that previously called `board.getBattMilliVolts()` directly: companion radio (battery/storage frame, stats frame, telemetry request, contact-request telemetry, `ui-new`/`ui-tiny` home screen), repeater (`REQ_TYPE_GET_STATUS`, telemetry request), room server (`REQ_TYPE_GET_STATUS`, telemetry request), and sensor node (telemetry request, periodic sensor read)

---

## 7. Companion Radio — Offline Queue, AGC/Interference CLI Settings, UI (`examples/companion_radio/`)
- Offline message queue changed to **dynamic PSRAM allocation** with a **circular buffer** (head-index based) — dequeuing a sent message is now O(1) instead of shifting the whole array
- `agc_reset_interval` and `interference_threshold` prefs are now actually wired up and CLI-settable (previously `interference_threshold` was hardcoded to 0/disabled)
- Device info response: `MAX_CONTACTS` capped correctly at 255 for the protocol byte
- **UI (`ui-orig`)**: status LED heartbeat cycle extended from 4 s → **400 s** (much less frequent flash)
- **UI (`ui-new`), battery display**: battery percentage can now use a proper LiPo discharge-curve lookup (`BATT_CURVE_LIPO_4V2` / `BATT_CURVE_LIPO_4V4`) instead of a linear millivolt mapping, plus a percentage label is now drawn next to the battery icon
- **UI (`ui-new`), display blanking**: pressing Enter on the home screen blanks the display to a minimal battery-percentage view (press any key to wake); both this view and normal home-screen rendering now periodically force a full e-ink refresh to prevent ghosting
- **UI (`ui-new`), GPS prefs**: GPS enable/interval settings stored in `NodePrefs` are now applied to the sensor manager automatically at boot
- **UI (`ui-new`), Morse code compose**: new `MORSE_COMPOSE_ENABLED` build flag adds a Morse-code channel messaging UI (double-click home screen → channel picker → Morse input screen) for screenless/low-UI devices — see section 10 (Heltec Mesh Pocket)

---

## 8. E-Ink Display — Ghosting Prevention & Icon Scaling Fix (`src/helpers/ui/GxEPDDisplay.*`)
- Added `setNextFrameFullRefresh()` to `DisplayDriver`/`GxEPDDisplay` so screens can request a full (deghosting) refresh instead of always using partial-window updates
- `drawXbm()` icon scaling now uses a single uniform scale factor (the smaller of the x/y scale) so icons render square instead of stretched on displays with a non-uniform pixel aspect ratio

---

## 9. Repeater — Blacklisting, Flood Filtering, Advert Jail + Sydney Mesh Defaults (`examples/simple_repeater/`)

### Node/Channel Blacklisting
- **New blacklist system** with two independent lists:
  - **Path blacklist** (`/path_bl`): drops flood packets whose path contains a matching pubkey-hash prefix
  - **Channel blacklist** (`/chan_bl`): drops group-channel packets whose channel hash matches (hex prefix) or whose channel name matches (`#name` filter with decrypt verification)
- Up to **16 hex prefix entries per list** + up to **8 channel name filters** for channel blacklist
- **Channel name filter** (`#channel_name`): when a `#name` entry is added, the repeater derives the channel secret from `sha256(name)` and verifies matches by test-decrypting the packet — this prevents false positives from hash collisions
- Persistent storage on filesystem; loaded on boot, saved on every change
- **CLI commands**: `blacklist path|chan list|add|rem|clear [hex|#name[,...]]`

### Flood Path Length Limiting
- Flood-routed REQ, RESPONSE, and PATH packets are dropped if the number of hops exceeds a configurable limit
- Default: **12 hops** (`set flood.path.max`), 0 = off

### Advert Jail + Global Advert Rate Limit
- **Per-sender advert jail**: tracks up to 128 senders by pubkey prefix; a sender that re-advertises faster than `advert.jail` hours accumulates strikes and is jailed (adverts dropped) once it reaches the strike threshold; strikes decay once the sender slows back down. Timestamp-deduplicated so the same advert arriving via multiple paths isn't double-counted
- **Global flood advert rate limit** (`advert.ratelimit`): a simple minimum-interval gate on all incoming flood adverts, independent of sender
- `jail` CLI command lists currently jailed senders with strike count and average advert interval

### Other Repeater Changes
- **Command chaining**: multiple CLI commands (repeater and serial console) can be sent in a single message separated by semicolons — each is executed sequentially and responses are concatenated with semicolon delimiters
- CLI responses on flood packets use **`createPathReturn`** to piggyback the reply on the PATH packet, falling back to a plain datagram if the response is too large
- Added **CoreSense RTC sync**: `onAdvertRecv` override automatically syncs the repeater's RTC from any node advertising a name containing `"coresense"` (>2 s drift threshold) — keeps timestamps accurate without manual intervention
- `MAX_NEIGHBOURS` raised from 50 → **200** (and from a commented-out 50 → 100 on a couple of disabled envs) across nearly every repeater variant
- **Sydney Mesh common settings are now compile-time defaults** (via `[repeater_settings]`) — applied to any env that `extends = ..., repeater_settings`, wired through the constructor's `#ifdef` blocks:

| Setting | Value | Equivalent CLI command |
|---|---|---|
| AGC reset interval | 500 s | `set agc.reset.interval 500` |
| Multi-ACKs | enabled | `set multi.acks 1` |
| Advert interval | 240 min | `set advert.interval 240` |
| Flood advert interval | 24 h | `set flood.advert.interval 24` |
| TX delay | 2.0 (mobile profile) | `set txdelay 2` |
| Direct TX delay | 2.0 (mobile profile) | `set direct.txdelay 2` |
| Path hash mode | 1 (2 bytes/hop) | `set path.hash.mode 1` |
| Loop detect | minimal | `set loop.detect minimal` |
| Power saving | on | `powersaving on` |
| Guest password | (blank) | `set guest.password` |

---

## 10. Heltec Mesh Pocket — Morse Code Messaging Variant (`variants/mesh_pocket/`)
- New **`MorseScreen`** / **`MorseChannelPicker`** UI (behind `MORSE_COMPOSE_ENABLED`, enabled by default for this variant) lets a screenless-style device compose and send channel messages using the hardware button as a Morse key, and shows incoming channel messages in a simple inbox — see `variants/mesh_pocket/Morse_Compose_Guide.md` for the input pattern
- `getBattMilliVolts()` now averages 8 ADC samples and uses a corrected divider ratio (4.96 vs 4.9) for a more accurate battery reading
- SoftDevice bumped from s140 6.1.1 → **7.3.0**
- Preamble length fixed at 32 symbols (`LORA_PREAMBLE_LEN`) explicitly applied after radio init
- `MAX_NEIGHBOURS` raised to 200, `MAX_CONTACTS` raised to 600 with a 500-contact SRAM fallback

---

## 11. Room Server — CoreSense RTC Sync + Idle Push Mode (`examples/simple_room_server/`)
- Added **`onAdvertRecv` override**: automatically syncs RTC from any node advertising a name containing `"coresense"` (>2 s drift threshold)
- Push polling loop now has an **idle mode** (10 s interval) when a full client round-robin completes with no pending work — reduces unnecessary radio activity

---

## 12. Sensor Node — CLI Path-Return (`examples/simple_sensor/`)
- CLI responses on flood-routed packets now use `createPathReturn` (consistent with other node types), falling back to a plain datagram if the response doesn't fit

---

## 13. Variant Configuration Updates

Nearly all repeater variants also pick up the `MAX_NEIGHBOURS` 50→200 bump described in section 9, and every variant inherits the global LoRa frequency/bandwidth/SF/CR change from section 1. Notable additional per-variant changes:

| Variant | Changes |
|---|---|
| **Heltec V4** | BME680/INA3221 sensor enables (other env sensors disabled); `SX126X_REGISTER_PATCH=1` for improved AGC/RX sensitivity; `RSSI_OFFSET=-17` (GC1109 LNA); `DEFAULT_POWERSAVING_ENABLED=1`; `ALLOWED_REPEAT_FREQ_RANGE` restricted to 902–928 MHz; repeater name → "Heltec V4 Repeater"; **offline message queue: 1024** (companion radio builds); **contacts: 5000** (OLED companion) / **4000** (TFT companion) / 600 (terminal chat / bridge) |
| **Heltec V3** | TX status LED disabled (`P_LORA_TX_LED` commented out); WiFi companion no longer overrides `OFFLINE_QUEUE_SIZE` (falls back to the base default) |
| **Station G2** | `SX126X_RX_BOOSTED_GAIN` now defaults to **1** (was 0 — upstream explicitly recommends 0 for this board due to RF performance in dense/high-noise areas); BLE companion: `BLE_TX_POWER=7`, `MAX_CONTACTS` → 600 |
| **Station G3 ESP32** | `board_build.partitions = default_16MB.csv` set explicitly (the board JSON omits it; without this, PlatformIO silently falls back to a 1.25 MB app partition on this 16 MB-flash board); `ALLOWED_REPEAT_FREQ_RANGE` restricted to 915–928 MHz; **offline message queue: 1024**, **contacts: 5000**, **channels: 100** (companion radio builds); this board's `getBattMilliVolts()` always returns 0, so it relies on the INA219 fallback described in section 6 |
| **RAK4631 / XIAO nRF52** | Repeater envs now `extends = ..., repeater_settings` to pick up the Sydney Mesh defaults |
| **RAK WisMesh Tag** | `MAX_CONTACTS` → 500 (USB companion) / 600 (BLE companion) |
| **T1000-E** | `MAX_CONTACTS` → 500 |
| **XIAO S3 WIO** | `ESP32_CPU_FREQ=80` added |
| **LilyGo T-Echo Lite** | Added a **headless (screenless) "Core" build family** — `LilyGo_T-Echo-Lite-Core` base env plus `_repeater` and `_companion_radio_ble` variants that drop the display/GxEPD2 dependency for cost-reduced or enclosure-only builds |

---

## 14. `.gitignore`
- Added build output directories and local build scripts (`build_and_organize_all.bat/.ps1`, `build_firmware.bat/.ps1`, test build output) to the ignore list

---

## 15. Core Mesh Routing — Next-Hop Reliability (`src/Mesh.*`)
- When a node forwards a direct (path-routed) packet and there's still a further explicit hop left in its path, it now tracks the packet's content hash and listens for that next hop repeating it — an overheard repeat is treated as implicit confirmation of receipt
- Also applied when a companion radio, repeater, or room server **originates** a direct packet (via `sendDirect()`) rather than just relaying one — the sending node listens for the first hop in the path to repeat it, the same as any in-transit repeater would
- If no repeat is heard within a timeout, the packet is resent, **up to 3 retries**, before being dropped
- Tunable via new virtual hooks on `Mesh`: `getNextHopReliabilityEnabled()` (default on), `getNextHopMaxRetries()` (default 3), `getNextHopConfirmTimeout()` (default ~3x estimated airtime + 2 s)
- Does not apply to ACK/MULTIPART packets being relayed *in-transit* by a repeater (they use their own dedicated forwarding/dedup logic), or to TRACE (path grows rather than shrinks per-hop, so its hash isn't stable across hops)
- **Last hop before the destination**: since the destination consumes the packet silently instead of repeating it, there's nothing to overhear there. For `REQ` packets (repeater CLI commands, status/neighbour queries, logins) this gap is closed instead by waiting for the correlated `RESPONSE` the destination sends back — the `RESPONSE`'s `dest_hash` is matched against the original `REQ`'s `src_hash`, so only a genuine reply to that specific request counts as confirmation, not just any traffic
- Known limitation: direct text messages (`TXT_MSG`) are acknowledged with a plain `ACK`, which carries no dest/src hash to correlate against, so their last hop still can't be confirmed this way and will always exhaust its retries. The same applies to the *return* leg of a confirmed `RESPONSE` — its own last hop back to the original sender has no further reply to key off of, so that hop is subject to the same inherent limitation

MeshCore is open-source software released under the MIT License. You are free to use, modify, and distribute it for personal and commercial projects.

## Contributing

Please submit PR's using 'dev' as the base branch!
For minor changes just submit your PR and we'll try to review it, but for anything more 'impactful' please open an Issue first and start a discussion. It is better to sound out what it is you want to achieve first, and try to come to a consensus on what the best approach is, especially when it impacts the structure or architecture of this codebase.

Here are some general principles you should try to adhere to:
* Keep it simple. Please, don't think like a high-level lang programmer. Think embedded, and keep code concise, without any unnecessary layers.
* No dynamic memory allocation, except during setup/begin functions.
* Use the same brace and indenting style that's in the core source modules. (A .clang-format is probably going to be added soon, but please do NOT retroactively re-format existing code. This just creates unnecessary diffs that make finding problems harder)

Help us prioritize! Please react with thumbs-up to issues/PRs you care about most. We look at reaction counts when planning work.

### Running unit tests

To run unit tests, run the following command:

```bash
pio test --environment native --verbose
```

## Road-Map / To-Do

There are a number of fairly major features in the pipeline, with no particular time-frames attached yet. In very rough chronological order:
- [X] Companion radio: UI redesign
- [X] Repeater + Room Server: add ACL's (like Sensor Node has)
- [X] Standardise Bridge mode for repeaters
- [ ] Repeater/Bridge: Standardise the Transport Codes for zoning/filtering
- [X] Core + Repeater: enhanced zero-hop neighbour discovery
- [ ] Core: round-trip manual path support
- [ ] Companion + Apps: support for multiple sub-meshes (and 'off-grid' client repeat mode)
- [ ] Core + Apps: support for LZW message compression
- [ ] Core: dynamic CR (Coding Rate) for weak vs strong hops
- [ ] Core: new framework for hosting multiple virtual nodes on one physical device
- [ ] V2 protocol spec: discussion and consensus around V2 packet protocol, including path hashes, new encryption specs, etc

## 📞 Get Support

- Report bugs and request features on the [GitHub Issues](https://github.com/ripplebiz/MeshCore/issues) page.
- Find additional guides and components on [my site](https://buymeacoffee.com/ripplebiz).
- Join [MeshCore Discord](https://meshcore.gg) to chat with the developers and get help from the community.
