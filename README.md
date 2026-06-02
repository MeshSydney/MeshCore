# MeshSydney MeshCore — Changes vs Upstream `dev`

This fork tracks [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore) `dev` with the following enhancements.

---

## CLI Command Reference

All `set`/`get` commands below are available on all node types (repeater, companion, room server, sensor). Blacklist commands are repeater-only.

### Flood & Abuse Prevention Settings

| Command | Range | Default | Description |
|---|---|---|---|
| `set flood.req.max <val>` | 0–255 | **6** | Max flood requests per sender pair before blocking. 0 = off. Counter decrements by 1 each interval (see below), so a node can send 6 in the first interval and 1 per interval after |
| `get flood.req.max` | — | — | Show current value |
| `set flood.req.interval <min>` | 1–255 | **60** | Minutes between flood request counter decrements |
| `get flood.req.interval` | — | — | Show current value |
| `set flood.path.max <val>` | 0–255 | **12** | Max path hops for flood REQ/RESPONSE/PATH packets. Packets with more hops are dropped. 0 = off |
| `get flood.path.max` | — | — | Show current value |
| `set advert.ratelimit <sec>` | 0–3600 | **0** (off) | Minimum seconds between accepting flood adverts |
| `get advert.ratelimit` | — | — | Show current value |
| `set advert.jail <hrs>` | 0–168 | **12** | Hours to jail a sender that exceeds the advert rate limit. 0 = off |
| `get advert.jail` | — | — | Show current value |

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

**Command chaining**: multiple CLI commands can be sent in a single message separated by semicolons — each command is executed sequentially and responses are concatenated with semicolon delimiters.

---

## 1. Radio / LoRa Configuration (`platformio.ini`)
- **Frequency changed** from 869.618 MHz (EU) → **916.575 MHz** (AU/US region)
- **Spreading factor changed** from SF8 → **SF7**
- Added `LORA_CR=8` (coding rate)
- Added shared `[repeater_settings]` PlatformIO config section to avoid duplicating repeater build flags across variants

---

## 2. Dispatcher — Send Retry Logic (`src/Dispatcher.cpp`)
- CAD fail max duration doubled: 4 s → **8 s**
- Failed sends are now **re-queued and retried** instead of being dropped
- Fixed debug log label (`checkSend` instead of `loop`)

---

## 3. Contact Storage — Dynamic/PSRAM Allocation (`src/helpers/BaseChatMesh.*`)
- `contacts[]` and `sort_array[]` changed from **fixed static arrays** to **heap-allocated pointers**
- On ESP32 with PSRAM, `initContacts()` allocates from PSRAM (enabling much larger `MAX_CONTACTS`)
- Falls back to SRAM with a safe limit (`CONTACTS_SRAM_FALLBACK`, default 200) when PSRAM unavailable
- Added `getMaxContacts()` API
- CLI text payloads embedded in PATH packets are now parsed and dispatched via `onCommandDataRecv`

---

## 4. BLE Throughput Improvements (`src/helpers/esp32/SerialBLEInterface.*`)
- MTU increased to **512 bytes**
- Frame queue doubled: 4 → **8 slots**
- **Adaptive connection interval**: fast mode (7.5–15 ms) during sync vs idle mode (45–90 ms) when inactive — notifications sent every **8 ms** minimum (`BLE_WRITE_MIN_INTERVAL`), improving companion app sync speed
- `isWriteBusy()` now reflects the 8 ms send interval rather than a fixed timer

---

## 5. Radio Wrapper Enhancements (`src/helpers/radiolib/`)
- **`CustomSX1262Wrapper.h`**: Added `RSSI_OFFSET` define (default 0) — applied to both current and last RSSI readings to compensate for external LNA gain (e.g. Heltec V4's GC1109 LNA)
- **`RadioLibWrappers.h/cpp`**: Dynamic preamble length at boot and after send — uses "long" preamble for low SF (where airtime impact is minimal) and "short" for high SF; configurable via `setPreambleLengths()`
- **`CustomLR1110Wrapper.h`**: Same SF-aware preamble logic applied post-send

---

## 6. BME680 Sensor Telemetry Fix (`src/helpers/sensors/EnvironmentSensorManager.cpp`)
- BME680 data moved to **telemetry channel 2** (`TELEM_CHANNEL_SELF+1`) so channel 1 remains free for MCU temperature
- Gas resistance now reported in **kΩ** (÷1000) to fit within the signed 16-bit Cayenne LPP range

---

## 7. Companion Radio — PSRAM Offline Queue + AGC Support (`examples/companion_radio/`)
- Offline queue changed to **dynamic allocation** (`initOfflineQueue()`) — uses PSRAM on ESP32 for large queues
- Added `AGC_RESET_INTERVAL` support (default 500 s), passed to radio wrapper
- Device info response: `MAX_CONTACTS` capped correctly at 255 for the protocol byte
- **UI** (`ui-orig`): status LED heartbeat cycle extended from 4 s → **400 s** (much less frequent flash)

---

## 8. Repeater — Blacklisting, Flood Filtering + Sydney Mesh Defaults (`examples/simple_repeater/`)

### Node/Channel Blacklisting
- **New blacklist system** with two independent lists:
  - **Path blacklist** (`/path_bl`): drops flood packets whose path contains a matching pubkey-hash prefix
  - **Channel blacklist** (`/chan_bl`): drops group-channel packets whose channel hash matches (hex prefix) or whose channel name matches (`#name` filter with decrypt verification)
- Up to **16 hex prefix entries per list** + up to **8 channel name filters** for channel blacklist
- **Channel name filter** (`#channel_name`): when a `#name` entry is added, the repeater derives the channel secret from `sha256(name)` and verifies matches by test-decrypting the packet — this prevents false positives from hash collisions
- Persistent storage on filesystem; loaded on boot, saved on every change
- **CLI commands**: `blacklist path|chan list|add|rem|clear [hex|#name[,...]]`

### Flood Request Rate Limiting
- **Per-sender rate limiting** for flood-routed REQ, TXT_MSG, RESPONSE, and PATH packets
- Tracks up to **64 sender pairs** (identified by the 2-byte dest_hash + src_hash prefix)
- Default: **6 requests** allowed before blocking (`set flood.req.max`), counter decrements by 1 every **60 minutes** (`set flood.req.interval`) — so a node can send 6 in the first interval and 1 per interval after, or 4 in the first interval and 3 the next
- Setting `flood.req.max` to 0 disables rate limiting
- Oldest entries are evicted when the table is full

### Flood Path Length Limiting
- Flood-routed REQ, RESPONSE, and PATH packets are dropped if the number of hops exceeds a configurable limit
- Default: **12 hops** (`set flood.path.max`), 0 = off

### Advert Jail
- Per-sender flood advert jail with **timestamp deduplication** — same advert arriving via different paths is not counted against the rate limit
- Advert count capped at threshold to prevent unbounded growth
- Jailed entries filtered from CLI replies

### Other Repeater Changes
- **Command chaining**: multiple CLI commands can be sent in a single message separated by semicolons — each command is executed sequentially and responses are concatenated with semicolon delimiters (see [CLI Commands docs](docs/cli_commands.md#command-chaining))
- CLI reply delay reduced: 600 ms → **300 ms**
- Retry responses now send `"(retry)"` instead of silent empty string
- Replay-attack path now sends an explicit `"(ERR: timestamp)"` error back to the client
- CLI responses on flood packets use **`createPathReturn`**
- Added **CoreSense RTC sync**: `onAdvertRecv` override automatically syncs the repeater's RTC from any node advertising a name containing `"coresense"` (>2 s drift threshold) — keeps timestamps accurate without manual intervention
- **Sydney Mesh common settings are now compile-time defaults** (via `[repeater_settings]`) — applied to any env that `extends = repeater_settings`, wired through the constructor `#ifdef` blocks:

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

## 9. Room Server — CoreSense RTC Sync + Idle Push Mode (`examples/simple_room_server/`)
- Added **`onAdvertRecv` override**: automatically syncs RTC from any node advertising a name containing `"coresense"` (>2 s drift threshold)
- Push polling loop now has an **idle mode** (10 s interval) when a full client round-robin completes with no pending work — reduces unnecessary radio activity

---

## 10. Sensor Node — CLI Path-Return (`examples/simple_sensor/`)
- CLI responses on flood-routed packets now use `createPathReturn` (consistent with other node types)

---

## 11. Variant Configuration Updates

| Variant | Changes |
|---|---|
| **Heltec V4** | BME680/INA3221 sensor enables; `SX126X_REGISTER_PATCH=1` for improved AGC; `RSSI_OFFSET=-17` (GC1109 LNA); `DEFAULT_POWERSAVING_ENABLED=1`; repeater name → "Heltec V4 Repeater"; **offline message queue: 1024** (companion radio builds); **contacts: 5000** (OLED companion) / **4000** (TFT companion) / 600 (terminal chat) |
| **Heltec V3** | TX LED disabled; WiFi companion MAX_CONTACTS/OFFLINE_QUEUE_SIZE adjustments |
| **Station G2** | `SX126X_RX_BOOSTED_GAIN=1` (with warning); BLE variant: `BLE_TX_POWER=7`; MAX_CONTACTS 600 |
| **RAK4631** | Repeater now inherits from `[repeater_settings]` |
| **RAK WisMesh Tag** | MAX_CONTACTS → 500 |
| **T1000-E** | MAX_CONTACTS → 500 |
| **XIAO nRF52** | Repeater now inherits from `[repeater_settings]` |
| **XIAO S3 WIO** | `ESP32_CPU_FREQ=80` added |
| **LilyGo T-Echo Lite** | **Battery**: Added `PIN_VBAT_MEAS_EN` (P0.31) — gated voltage divider must be enabled before ADC read; `pinMode(PIN_VBAT_READ, INPUT)` reclaims P0.02; 8-sample averaging with LilyGo's exact voltage formula. **I2C**: Corrected SDA/SCL from P0.04/P0.02 → P1.04/P1.02 (per LilyGo `IIC_1_SDA`/`IIC_1_SCL`). **GPS**: Corrected all five pin assignments (TX, RX, EN, Standby, PPS) to match LilyGo's `t_echo_lite_config.h`. **SPI**: Fixed `SPI_INTERFACES_COUNT` from `_PINNUM(0,2)` → `(2)`. **Headless builds**: Added `LilyGo_T-Echo-Lite-Core` base config and `_repeater` / `_companion_radio_ble` environments for screenless variants (strips display flags, source files, and GxEPD2 dependency). |

---

## 12. `.gitignore`
- Added build output directories and local build scripts to ignore list

MeshCore is open-source software released under the MIT License. You are free to use, modify, and distribute it for personal and commercial projects.

## Contributing

Please submit PR's using 'dev' as the base branch!
For minor changes just submit your PR and we'll try to review it, but for anything more 'impactful' please open an Issue first and start a discussion. Is better to sound out what it is you want to achieve first, and try to come to a consensus on what the best approach is, especially when it impacts the structure or architecture of this codebase.

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
