# MeshSydney MeshCore — Changes vs Upstream `dev`

This fork tracks [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore) `dev` with the following enhancements.

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
- **Burst send mode**: up to 4 frames sent per 15 ms window (vs single frame per 60 ms), improving companion app sync speed
- `isWriteBusy()` now reflects queue fill level rather than a fixed timer

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
- CLI responses on flood-routed packets now use **`createPathReturn`** (more efficient path piggyback)
- **UI**: status LED heartbeat cycle extended from 4 s → **400 s** (much less frequent flash)

---

## 8. Repeater — Node/Channel Blacklisting + Sydney Mesh Defaults (`examples/simple_repeater/`)
- **New blacklist system** with two independent lists:
  - **Path blacklist** (`/path_bl`): drops flood packets whose path contains a matching pubkey-hash prefix
  - **Channel blacklist** (`/chan_bl`): drops group-channel packets whose channel hash matches
- Up to **16 entries per list**, 1–4 byte hex prefixes (matching all LoRa hash sizes)
- Persistent storage on filesystem; loaded on boot, saved on every change
- **CLI commands**: `blacklist path|chan list|add|rem|clear [hex[,hex,...]]`
- CLI reply delay reduced: 600 ms → **300 ms**
- Retry responses now send `"(retry)"` instead of silent empty string
- Replay-attack path now sends an explicit `"(ERR: timestamp)"` error back to the client
- CLI responses on flood packets use **`createPathReturn`**
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
| **Heltec V4** | BME680/INA3221 sensor enables; `SX126X_REGISTER_PATCH=1` for improved AGC; `RSSI_OFFSET=-17` (GC1109 LNA); `DEFAULT_POWERSAVING_ENABLED=1`; repeater name → "Heltec V4 Repeater"; MAX_CONTACTS 600/5000/4000; OFFLINE_QUEUE_SIZE 1024 |
| **Heltec V3** | TX LED disabled; WiFi companion MAX_CONTACTS/OFFLINE_QUEUE_SIZE adjustments |
| **Station G2** | `SX126X_RX_BOOSTED_GAIN=1` (with warning); BLE variant: `BLE_TX_POWER=7`; MAX_CONTACTS 600 |
| **RAK4631** | Repeater now inherits from `[repeater_settings]` |
| **RAK WisMesh Tag** | MAX_CONTACTS → 500 |
| **T1000-E** | MAX_CONTACTS → 500 |
| **XIAO nRF52** | Repeater now inherits from `[repeater_settings]` |
| **XIAO S3 WIO** | `ESP32_CPU_FREQ=80` added |

---

## 12. `.gitignore`
- Added build output directories and local build scripts to ignore list

MeshCore is open-source software released under the MIT License. You are free to use, modify, and distribute it for personal and commercial projects.

## Contributing

Please submit PR's using 'dev' as the base branch!
For minor changes just submit your PR and we'll try to review it, but for anything more 'impactful' please open an Issue first and start a discussion. Is better to sound out what it is you want to achieve first, and try to come to a consensus on what the best approach is, especially when it impacts the structure or architecture of this codebase.

Here are some general principals you should try to adhere to:
* Keep it simple. Please, don't think like a high-level lang programmer. Think embedded, and keep code concise, without any unnecessary layers.
* No dynamic memory allocation, except during setup/begin functions.
* Use the same brace and indenting style that's in the core source modules. (A .clang-format is prob going to be added soon, but please do NOT retroactively re-format existing code. This just creates unnecessary diffs that make finding problems harder)

Help us prioritize! Please react with thumbs-up to issues/PRs you care about most. We look at reaction counts when planning work.

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
- Join [MeshCore Discord](https://discord.gg/BMwCtwHj5V) to chat with the developers and get help from the community.
