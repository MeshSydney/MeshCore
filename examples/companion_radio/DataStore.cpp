#include <Arduino.h>
#include "DataStore.h"

#if defined(OFFLINE_QUEUE_FLASH)
#include <helpers/BaseSerialInterface.h>   // for MAX_FRAME_SIZE
#endif

DataStore::DataStore(FILESYSTEM& fs, mesh::RTCClock& clock) : _fs(&fs), _fsExtra(nullptr), _clock(&clock),
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    identity_store(fs, "")
#elif defined(RP2040_PLATFORM)
    identity_store(fs, "/identity")
#else
    identity_store(fs, "/identity")
#endif
{
}

#if defined(EXTRAFS) || defined(QSPIFLASH)
DataStore::DataStore(FILESYSTEM& fs, FILESYSTEM& fsExtra, mesh::RTCClock& clock) : _fs(&fs), _fsExtra(&fsExtra), _clock(&clock),
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    identity_store(fs, "")
#elif defined(RP2040_PLATFORM)
    identity_store(fs, "/identity")
#else
    identity_store(fs, "/identity")
#endif
{
}
#endif

static File openWrite(FILESYSTEM* fs, const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove(filename);
  return fs->open(filename, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return fs->open(filename, "w");
#else
  return fs->open(filename, "w", true);
#endif
}

static File openReadWrite(FILESYSTEM* fs, const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return fs->open(filename, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return fs->open(filename, "r+");
#else
  return fs->open(filename, "r+", false);
#endif
}

static File openReadOnly(FILESYSTEM* fs, const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return fs->open(filename, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  return fs->open(filename, "r");
#else
  return fs->open(filename, "r", false);
#endif
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  static uint32_t _ContactsChannelsTotalBlocks = 0;
#endif

void DataStore::begin() {
#if defined(RP2040_PLATFORM)
  identity_store.begin();
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _ContactsChannelsTotalBlocks = _getContactsChannelsFS()->_getFS()->cfg->block_count;
  #if defined(EXTRAFS) || defined(QSPIFLASH)
  migrateToSecondaryFS();
  #endif
#endif
#ifdef CONTACTS_FLASH_INDEX
  reshardContactsFileIfNeeded();   // one-time: convert a pre-sharding flat /contacts3 into per-shard files
#endif
  checkAdvBlobFile();
  loadBlobIndex();   // populate RAM (key,timestamp,len) mirror -- see getBlobByKey()/putBlobByKey()

  // clean up old per-file blob store if present
  if (_fs->exists("/bl")) {
    File dir = _fs->open("/bl");
    if (dir && dir.isDirectory()) {
      File f = dir.openNextFile();
      while (f) {
        char path[80];
        snprintf(path, sizeof(path), "/bl/%s", f.name());
        f.close();
        _fs->remove(path);
        f = dir.openNextFile();
      }
      dir.close();
    }
    _fs->rmdir("/bl");
  }
}

#if defined(ESP32)
  #include <LittleFS.h>
  #include <nvs_flash.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
  #elif defined(EXTRAFS)
    #include <CustomLFS.h>
  #else 
    #include <InternalFileSystem.h>
  #endif
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
int _countLfsBlock(void *p, lfs_block_t block){
      if (block > _ContactsChannelsTotalBlocks) {
        MESH_DEBUG_PRINTLN("ERROR: Block %d exceeds filesystem bounds - CORRUPTION DETECTED!", block);
        return LFS_ERR_CORRUPT;  // return error to abort lfs_traverse() gracefully
    }
  lfs_size_t *size = (lfs_size_t*) p;
  *size += 1;
    return 0;
}

lfs_ssize_t _getLfsUsedBlockCount(FILESYSTEM* fs) {
  lfs_size_t size = 0;
  int err = lfs_traverse(fs->_getFS(), _countLfsBlock, &size);
  if (err) {
    MESH_DEBUG_PRINTLN("ERROR: lfs_traverse() error: %d", err);
    return 0;
  }
  return size;
}
#endif

uint32_t DataStore::getStorageUsedKb() const {
#if defined(ESP32)
  return LittleFS.usedBytes() / 1024;
#elif defined(RP2040_PLATFORM)
  FSInfo info;
  info.usedBytes = 0;
  _fs->info(info);
  return info.usedBytes / 1024;
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  const lfs_config* config = _getContactsChannelsFS()->_getFS()->cfg;
  int usedBlockCount = _getLfsUsedBlockCount(_getContactsChannelsFS());
  int usedBytes = config->block_size * usedBlockCount;
  return usedBytes / 1024;
#else
  return 0;
#endif
}

uint32_t DataStore::getStorageTotalKb() const {
#if defined(ESP32)
  return LittleFS.totalBytes() / 1024;
#elif defined(RP2040_PLATFORM)
  FSInfo info;
  info.totalBytes = 0;
  _fs->info(info);
  return info.totalBytes / 1024;
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  const lfs_config* config = _getContactsChannelsFS()->_getFS()->cfg;
  int totalBytes = config->block_size * config->block_count;
  return totalBytes / 1024;
#else
  return 0;
#endif
}

File DataStore::openRead(const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return _fs->open(filename, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  return _fs->open(filename, "r");
#else
  return _fs->open(filename, "r", false);
#endif
}

File DataStore::openRead(FILESYSTEM* fs, const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return fs->open(filename, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  return fs->open(filename, "r");
#else
  return fs->open(filename, "r", false);
#endif
}

bool DataStore::removeFile(const char* filename) {
  return _fs->remove(filename);
}

bool DataStore::removeFile(FILESYSTEM* fs, const char* filename) {
  return fs->remove(filename);
}

bool DataStore::formatFileSystem() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  if (_fsExtra == nullptr) {
    return _fs->format();
  } else {
    return _fs->format() && _fsExtra->format();
  }
#elif defined(RP2040_PLATFORM)
  return LittleFS.format();
#elif defined(ESP32)
  bool fs_success = ((fs::LittleFSFS *)_fs)->format();
  esp_err_t nvs_err = nvs_flash_erase(); // no need to reinit, will be done by reboot
  return fs_success && (nvs_err == ESP_OK);
#else
  #error "need to implement format()"
#endif
}

bool DataStore::loadMainIdentity(mesh::LocalIdentity &identity) {
  return identity_store.load("_main", identity);
}

bool DataStore::saveMainIdentity(const mesh::LocalIdentity &identity) {
  return identity_store.save("_main", identity);
}

void DataStore::loadPrefs(NodePrefs& prefs) {
  if (_fs->exists("/prefs.json")) {
    File file = openRead(_fs, "/prefs.json");
    if (file) {
      prefs.loadSerial(file);   // new Serial prefs
      file.close();
    }
  } else if (_fs->exists("/new_prefs")) {
    loadPrefsInt("/new_prefs", prefs);
    if (savePrefs(prefs) ) {                // save to new format
      //_fs->remove("/new_prefs"); // remove old
    }
  }
}

void DataStore::loadPrefsInt(const char *filename, NodePrefs& _prefs) {
  File file = openRead(_fs, filename);
  if (file) {
    uint8_t pad[8];

    file.read((uint8_t *)&_prefs.airtime_factor, sizeof(float));                           // 0
    file.read((uint8_t *)_prefs.node_name, sizeof(_prefs.node_name));                      // 4
    file.read(pad, 4);                                                                     // 36
    file.read((uint8_t *)&_prefs.node_lat, sizeof(_prefs.node_lat));                       // 40
    file.read((uint8_t *)&_prefs.node_lon, sizeof(_prefs.node_lon));                       // 48
    file.read((uint8_t *)&_prefs.freq, sizeof(_prefs.freq));                               // 56
    file.read((uint8_t *)&_prefs.sf, sizeof(_prefs.sf));                                   // 60
    file.read((uint8_t *)&_prefs.cr, sizeof(_prefs.cr));                                   // 61
    file.read((uint8_t *)&_prefs._client_repeat, sizeof(_prefs._client_repeat));             // 62
    file.read((uint8_t *)&_prefs.manual_add_contacts, sizeof(_prefs.manual_add_contacts)); // 63
    file.read((uint8_t *)&_prefs.bw, sizeof(_prefs.bw));                                   // 64
    file.read((uint8_t *)&_prefs.tx_power_dbm, sizeof(_prefs.tx_power_dbm));               // 68
    file.read((uint8_t *)&_prefs.telemetry_mode_base, sizeof(_prefs.telemetry_mode_base)); // 69
    file.read((uint8_t *)&_prefs.telemetry_mode_loc, sizeof(_prefs.telemetry_mode_loc));   // 70
    file.read((uint8_t *)&_prefs.telemetry_mode_env, sizeof(_prefs.telemetry_mode_env));   // 71
    file.read((uint8_t *)&_prefs.rx_delay_base, sizeof(_prefs.rx_delay_base));             // 72
    file.read((uint8_t *)&_prefs.advert_loc_policy, sizeof(_prefs.advert_loc_policy));     // 76
    file.read((uint8_t *)&_prefs.multi_acks, sizeof(_prefs.multi_acks));                   // 77
    file.read((uint8_t *)&_prefs.path_hash_mode, sizeof(_prefs.path_hash_mode));           // 78
    file.read(pad, 1);                                                                     // 79
    file.read((uint8_t *)&_prefs.ble_pin, sizeof(_prefs.ble_pin));                         // 80
    file.read((uint8_t *)&_prefs.buzzer_quiet, sizeof(_prefs.buzzer_quiet));               // 84
    file.read((uint8_t *)&_prefs.gps_enabled, sizeof(_prefs.gps_enabled));                 // 85
    file.read((uint8_t *)&_prefs.gps_interval, sizeof(_prefs.gps_interval));               // 86
    file.read((uint8_t *)&_prefs.autoadd_config, sizeof(_prefs.autoadd_config));           // 87
    file.read((uint8_t *)&_prefs.autoadd_max_hops, sizeof(_prefs.autoadd_max_hops));       // 88
    file.read((uint8_t *)&_prefs.rx_boosted_gain, sizeof(_prefs.rx_boosted_gain));         // 89
    file.read((uint8_t *)_prefs.default_scope_name, sizeof(_prefs.default_scope_name));    // 90
    file.read((uint8_t *)_prefs.default_scope_key, sizeof(_prefs.default_scope_key));     // 121

    // migrate old fields
    _prefs.setRepeatEn(_prefs._client_repeat != 0);

    file.close();
  }
}

bool DataStore::savePrefs(NodePrefs& _prefs) {
  File file = openWrite(_fs, "/prefs.json");
  if (file) {
    bool success = _prefs.saveSerial(file);
    file.close();
    return success;
  }
  return false;
}

void DataStore::loadContacts(DataStoreHost* host) {
File file = openRead(_getContactsChannelsFS(), "/contacts3");
    if (file) {
      bool full = false;
      while (!full) {
        ContactInfo c;
        uint8_t pub_key[32];
        uint8_t unused;

        bool success = (file.read(pub_key, 32) == 32);
        success = success && (file.read((uint8_t *)&c.name, 32) == 32);
        success = success && (file.read(&c.type, 1) == 1);
        success = success && (file.read(&c.flags, 1) == 1);
        success = success && (file.read(&unused, 1) == 1);
        success = success && (file.read((uint8_t *)&c.sync_since, 4) == 4); // was 'reserved'
        success = success && (file.read((uint8_t *)&c.out_path_len, 1) == 1);
        success = success && (file.read((uint8_t *)&c.last_advert_timestamp, 4) == 4);
        success = success && (file.read(c.out_path, 64) == 64);
        success = success && (file.read((uint8_t *)&c.lastmod, 4) == 4);
        success = success && (file.read((uint8_t *)&c.gps_lat, 4) == 4);
        success = success && (file.read((uint8_t *)&c.gps_lon, 4) == 4);

        if (!success) break; // EOF

        c.id = mesh::Identity(pub_key);
        if (!host->onContactLoaded(c)) full = true;
      }
      file.close();
    }
}

void DataStore::saveContacts(DataStoreHost* host, bool (*filter)(const ContactInfo& c)) {
  File file = openWrite(_getContactsChannelsFS(), "/contacts3");
  if (file) {
    uint32_t idx = 0;
    ContactInfo c;
    uint8_t unused = 0;

    while (host->getContactForSave(idx, c)) {
      if (filter && !filter(c)) {
        idx++;  // advance to next contact
        continue;
      }
      bool success = (file.write(c.id.pub_key, 32) == 32);
      success = success && (file.write((uint8_t *)&c.name, 32) == 32);
      success = success && (file.write(&c.type, 1) == 1);
      success = success && (file.write(&c.flags, 1) == 1);
      success = success && (file.write(&unused, 1) == 1);
      success = success && (file.write((uint8_t *)&c.sync_since, 4) == 4);
      success = success && (file.write((uint8_t *)&c.out_path_len, 1) == 1);
      success = success && (file.write((uint8_t *)&c.last_advert_timestamp, 4) == 4);
      success = success && (file.write(c.out_path, 64) == 64);
      success = success && (file.write((uint8_t *)&c.lastmod, 4) == 4);
      success = success && (file.write((uint8_t *)&c.gps_lat, 4) == 4);
      success = success && (file.write((uint8_t *)&c.gps_lon, 4) == 4);

      if (!success) break; // write failed

      idx++;  // advance to next contact
    }
    file.close();
  }
}

// fixed on-disk size of one contact record (see loadContacts()/saveContacts() field layout below)
#define CONTACT_RECORD_SIZE   152

// Records per shard file. /contacts3 used to be one single flat file indexed by
// idx*CONTACT_RECORD_SIZE -- fine for small MAX_CONTACTS, but once MAX_CONTACTS grows into the
// thousands, updating even ONE already-written record (e.g. a contact's out_path via
// CMD_ADD_UPDATE_CONTACT/CMD_RESET_PATH -- "setting a path" for a repeater login) risked
// LittleFS's expensive "rewrite every block after this point" CTZ-skip-list cost (see
// writeContactRecord() below) across the WHOLE multi-hundred-KB file. Splitting storage into
// fixed-size shard files ("/contacts3_0", "/contacts3_1", ...) bounds that worst case to just one
// shard's worth of records instead of the entire contacts file.
#define CONTACTS_SHARD_RECORDS   256

static void contactsShardFilename(char buf[24], uint32_t shard) {
  snprintf(buf, 24, "/contacts3_%u", (unsigned) shard);
}


// Packs/unpacks one ContactInfo to/from a single CONTACT_RECORD_SIZE-byte buffer, so
// readContactRecord()/writeContactRecord()/readNextContactRecord() can each do just ONE
// file.read()/file.write() call per record instead of 12 separate field-level calls
// (each call has its own overhead on top of the underlying flash I/O).
static void packContactRecord(uint8_t rec[CONTACT_RECORD_SIZE], const ContactInfo& c) {
  uint8_t unused = 0;
  uint8_t* p = rec;
  memcpy(p, c.id.pub_key, 32); p += 32;
  memcpy(p, &c.name, 32); p += 32;
  memcpy(p, &c.type, 1); p += 1;
  memcpy(p, &c.flags, 1); p += 1;
  memcpy(p, &unused, 1); p += 1;
  memcpy(p, &c.sync_since, 4); p += 4;
  memcpy(p, &c.out_path_len, 1); p += 1;
  memcpy(p, &c.last_advert_timestamp, 4); p += 4;
  memcpy(p, c.out_path, 64); p += 64;
  memcpy(p, &c.lastmod, 4); p += 4;
  memcpy(p, &c.gps_lat, 4); p += 4;
  memcpy(p, &c.gps_lon, 4); p += 4;
}

static void unpackContactRecord(const uint8_t rec[CONTACT_RECORD_SIZE], ContactInfo& c) {
  uint8_t pub_key[32];
  const uint8_t* p = rec;
  memcpy(pub_key, p, 32); p += 32;
  memcpy(&c.name, p, 32); p += 32;
  memcpy(&c.type, p, 1); p += 1;
  memcpy(&c.flags, p, 1); p += 1;
  p += 1;   // unused
  memcpy(&c.sync_since, p, 4); p += 4;
  memcpy(&c.out_path_len, p, 1); p += 1;
  memcpy(&c.last_advert_timestamp, p, 4); p += 4;
  memcpy(c.out_path, p, 64); p += 64;
  memcpy(&c.lastmod, p, 4); p += 4;
  memcpy(&c.gps_lat, p, 4); p += 4;
  memcpy(&c.gps_lon, p, 4); p += 4;

  c.id = mesh::Identity(pub_key);
  c.shared_secret_valid = false;
}

bool DataStore::readContactRecord(uint32_t idx, ContactInfo& c) {
  uint32_t shard = idx / CONTACTS_SHARD_RECORDS;
  uint32_t slot  = idx % CONTACTS_SHARD_RECORDS;
  char filename[24];
  contactsShardFilename(filename, shard);

#ifdef CONTACTS_FLASH_INDEX
  if (_contactsLookupActive) {
    if (_contactsLookupFile == nullptr || _contactsLookupShard != (int)shard) {
      // candidate lives in a different shard than the one currently held open -- switch to it
      if (_contactsLookupFile != nullptr) {
        _contactsLookupFile->close();
        delete _contactsLookupFile;
        _contactsLookupFile = nullptr;
      }
      File f = openRead(_getContactsChannelsFS(), filename);
      if (!f) { _contactsLookupShard = -1; return false; }
      _contactsLookupFile = new File(f);
      _contactsLookupShard = (int)shard;
    }
    uint8_t rec[CONTACT_RECORD_SIZE];
    bool success = _contactsLookupFile->seek(slot * CONTACT_RECORD_SIZE)
                && (_contactsLookupFile->read(rec, CONTACT_RECORD_SIZE) == CONTACT_RECORD_SIZE);
    if (success) unpackContactRecord(rec, c);
    return success;
  }
#endif
  File file = openRead(_getContactsChannelsFS(), filename);
  if (!file) return false;

  bool success = file.seek(slot * CONTACT_RECORD_SIZE);
  if (success) {
    uint8_t rec[CONTACT_RECORD_SIZE];
    success = (file.read(rec, CONTACT_RECORD_SIZE) == CONTACT_RECORD_SIZE);
    if (success) unpackContactRecord(rec, c);
  }
  file.close();
  return success;
}

// NOTE: deliberately does NOT pre-extend a shard to its full CONTACTS_SHARD_RECORDS capacity up
// front. LittleFS's CTZ skip-list makes appends to the current end of a file cheap, but rewriting
// a block BEFORE the current end requires rewriting every block after it too (their skip-list
// pointers all point back to specific block addresses, which change once a preceding block is
// rewritten) -- an O(blocks-after) cost. Growing each shard file lazily, only as far as the
// highest slot actually written so far, keeps normal (increasing-index) contact adds a cheap
// tail-only append; only genuine updates/tombstone-slot-reuse at an already-written index still
// pay the seek-into-the-middle cost -- but since storage is now split into CONTACTS_SHARD_RECORDS-
// sized shard files (see CONTACTS_SHARD_RECORDS above), that cost is bounded to at most one
// shard's worth of records instead of the whole contacts file.
bool DataStore::writeContactRecord(uint32_t idx, const ContactInfo& c) {
  uint32_t shard = idx / CONTACTS_SHARD_RECORDS;
  uint32_t slot  = idx % CONTACTS_SHARD_RECORDS;
  char filename[24];
  contactsShardFilename(filename, shard);

  File file = openReadWrite(_getContactsChannelsFS(), filename);
  if (!file) return false;

  uint32_t want_pos = slot * CONTACT_RECORD_SIZE;
  uint32_t cur_size = file.size();

  uint8_t rec[CONTACT_RECORD_SIZE];
  packContactRecord(rec, c);

  bool success;
  if (want_pos >= cur_size) {
    // tail (or past it) -- cheap append, zero-filling any gap first
    uint8_t zeroes[CONTACT_RECORD_SIZE];
    memset(zeroes, 0, sizeof(zeroes));
    success = file.seek(cur_size);
    for (uint32_t pos = cur_size; success && pos < want_pos; pos += CONTACT_RECORD_SIZE) {
      success = (file.write(zeroes, CONTACT_RECORD_SIZE) == CONTACT_RECORD_SIZE);
    }
    success = success && (file.write(rec, CONTACT_RECORD_SIZE) == CONTACT_RECORD_SIZE);
  } else {
    // updating an already-written slot -- bounded to at most this one shard's worth of records
    success = file.seek(want_pos) && (file.write(rec, CONTACT_RECORD_SIZE) == CONTACT_RECORD_SIZE);
  }
  file.close();
  return success;
}

// Shrinks a file down to want_size bytes. Adafruit_LittleFS (NRF52/STM32) exposes File::truncate()
// directly; the ESP32/RP2040 fs::File wrapper does not, so on those platforms this emulates it by
// reading the bytes to keep, then removing and rewriting the file with just that much data.
static bool truncateFile(FILESYSTEM* fs, const char* filename, uint32_t want_size) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  File file = openReadWrite(fs, filename);
  if (!file) return false;
  bool ok = file.truncate(want_size);
  file.close();
  return ok;
#else
  if (want_size == 0) return fs->remove(filename);

  uint8_t* buf = new uint8_t[want_size];
  bool ok;
  {
    File file = openReadOnly(fs, filename);
    ok = file && (file.read(buf, want_size) == want_size);
    if (file) file.close();
  }
  if (ok) {
    File file = openWrite(fs, filename);
    ok = file && (file.write(buf, want_size) == want_size);
    if (file) file.close();
  }
  delete[] buf;
  return ok;
#endif
}

// One-time self-heal, called once at boot (after the contacts scan has determined how many
// records are really in use). Older firmware used to pre-extend /contacts3 to the full
// MAX_CONTACTS capacity via ensureContactsCapacity() -- devices that were ever flashed with that
// code still have that oversized file sitting on disk. Since writeContactRecord() now judges
// "cheap append vs expensive middle-of-file write" purely by comparing against file.size(), a
// leftover oversized file makes EVERY write look like it's "before the end", permanently
// re-triggering the exact slowness this was meant to fix. Shrink the file back down to just the
// records actually in use so future writes are judged correctly. No-op once already right-sized.
// Now shard-aware: any shard file entirely beyond used_records is just deleted outright (cheaper
// and simpler than truncating it to empty), the one shard straddling the boundary is truncated to
// its still-in-use records, and shards entirely before the boundary are left untouched.
bool DataStore::truncateContactsFileIfNeeded(uint32_t used_records) {
  FILESYSTEM* fs = _getContactsChannelsFS();
  uint32_t used_shard = used_records / CONTACTS_SHARD_RECORDS;
  uint32_t used_slot = used_records % CONTACTS_SHARD_RECORDS;

  bool ok = true;
  for (uint32_t shard = used_shard; ; shard++) {
    char filename[24];
    contactsShardFilename(filename, shard);
    if (!fs->exists(filename)) break;   // no shard files were ever created beyond this point

    if (shard == used_shard) {
      File file = openReadOnly(fs, filename);
      uint32_t cur_size = file ? file.size() : 0;
      if (file) file.close();

      uint32_t want_size = used_slot * CONTACT_RECORD_SIZE;
      if (cur_size > want_size) ok = truncateFile(fs, filename, want_size) && ok;
    } else {
      ok = removeFile(fs, filename) && ok;
    }
  }
  return ok;
}

#ifdef CONTACTS_FLASH_INDEX
// Batch-scan API, used ONLY by BaseChatMesh::loadContactsFlashIndex() at boot. Opens each shard
// file once as the scan reaches it (instead of once per record like readContactRecord() does),
// then reads sequentially with no further seeking within a shard -- avoids the per-record seek
// cost that made boot time scale badly with MAX_CONTACTS once /contacts3 grew large.
bool DataStore::beginContactsScan(uint32_t start_idx) {
  endContactsScan();   // safety: close any previous (unbalanced) scan handle first
  _contactsScanIdx = start_idx;
  return true;   // actual shard file is opened lazily by readNextContactRecord()
}

bool DataStore::readNextContactRecord(ContactInfo& c) {
  uint32_t shard = _contactsScanIdx / CONTACTS_SHARD_RECORDS;
  uint32_t slot  = _contactsScanIdx % CONTACTS_SHARD_RECORDS;

  if (_contactsScanFile == nullptr || _contactsScanShard != (int)shard) {
    if (_contactsScanFile != nullptr) {
      _contactsScanFile->close();
      delete _contactsScanFile;
      _contactsScanFile = nullptr;
    }
    char filename[24];
    contactsShardFilename(filename, shard);
    File f = openRead(_getContactsChannelsFS(), filename);
    if (!f) return false;   // this shard was never created -- no more records beyond this point
    if (!f.seek(slot * CONTACT_RECORD_SIZE)) {
      f.close();
      return false;
    }
    _contactsScanFile = new File(f);
    _contactsScanShard = (int)shard;
  }

  uint8_t rec[CONTACT_RECORD_SIZE];
  if (_contactsScanFile->read(rec, CONTACT_RECORD_SIZE) != CONTACT_RECORD_SIZE) return false;

  unpackContactRecord(rec, c);
  _contactsScanIdx++;
  return true;
}

void DataStore::endContactsScan() {
  if (_contactsScanFile != nullptr) {
    _contactsScanFile->close();
    delete _contactsScanFile;
    _contactsScanFile = nullptr;
  }
  _contactsScanShard = -1;
}

// See readContactRecord() -- once active, it transparently opens/reuses shard handles via this
// state instead of opening a shard fresh each call. Used around the searchPeersByHash() candidate
// loop (a single incoming login/direct message can need to try several same-hash-prefix
// candidates before finding the one that actually decrypts).
void DataStore::beginContactLookup() {
  endContactLookup();   // safety: close any previous (unbalanced) lookup handle first
  _contactsLookupActive = true;
}

void DataStore::endContactLookup() {
  if (_contactsLookupFile != nullptr) {
    _contactsLookupFile->close();
    delete _contactsLookupFile;
    _contactsLookupFile = nullptr;
  }
  _contactsLookupShard = -1;
  _contactsLookupActive = false;
}
#endif

// One-time migration for devices upgrading from pre-sharding firmware: if a legacy flat
// /contacts3 file still exists (single file, idx*CONTACT_RECORD_SIZE offsets), split it into
// CONTACTS_SHARD_RECORDS-sized /contacts3_N shard files and remove the old file. No-op (single
// fs->exists() check) once a device has already been migrated, or on a fresh device that never
// had one.
void DataStore::reshardContactsFileIfNeeded() {
  FILESYSTEM* fs = _getContactsChannelsFS();
  if (!fs->exists("/contacts3")) return;

  File oldFile = openRead(fs, "/contacts3");
  if (oldFile) {
    uint32_t idx = 0;
    int cur_shard = -1;
    File* shardFile = nullptr;
    uint8_t rec[CONTACT_RECORD_SIZE];
    while (oldFile.read(rec, CONTACT_RECORD_SIZE) == CONTACT_RECORD_SIZE) {
      uint32_t shard = idx / CONTACTS_SHARD_RECORDS;
      if ((int)shard != cur_shard) {
        if (shardFile != nullptr) {
          shardFile->close();
          delete shardFile;
          shardFile = nullptr;
        }
        char filename[24];
        contactsShardFilename(filename, shard);
        File f = openReadWrite(fs, filename);
        if (f) shardFile = new File(f);
        cur_shard = (int)shard;
      }
      if (shardFile != nullptr) {
        uint32_t slot = idx % CONTACTS_SHARD_RECORDS;
        shardFile->seek(slot * CONTACT_RECORD_SIZE);
        shardFile->write(rec, CONTACT_RECORD_SIZE);
      }
      idx++;
    }
    if (shardFile != nullptr) {
      shardFile->close();
      delete shardFile;
    }
    oldFile.close();
  }
  fs->remove("/contacts3");
}


#ifdef OFFLINE_QUEUE_FLASH
// fixed on-disk size of one offline-queue frame record: 1 length byte + MAX_FRAME_SIZE body bytes
#define OFFLINEQ_RECORD_SIZE   (MAX_FRAME_SIZE + 1)

// Queued messages don't survive a reboot -- MyMesh::initOfflineQueue() always resets
// offline_queue_head/len to 0 at boot regardless of what's physically still on flash. Older
// firmware used to pre-extend /offlineq to the full OFFLINE_QUEUE_SIZE capacity here instead,
// which made EVERY subsequent write look like a "write before file end" to LittleFS (same
// expensive CTZ-skip-list rewrite cost described on writeContactRecord()). Since nothing needs
// to persist across boots, just wipe it here and let writeOfflineQueueRecord() grow it back
// lazily, tail-only -- this also self-heals any oversized leftover file from that older firmware.
void DataStore::resetOfflineQueue() {
  File file = openWrite(_getContactsChannelsFS(), "/offlineq");
  if (file) file.close();
}

bool DataStore::readOfflineQueueRecord(uint32_t idx, uint8_t* dest, uint8_t& len) {
  File file = openRead(_getContactsChannelsFS(), "/offlineq");
  if (!file) return false;

  bool success = file.seek(idx * OFFLINEQ_RECORD_SIZE);
  success = success && (file.read(&len, 1) == 1);
  success = success && (file.read(dest, MAX_FRAME_SIZE) == MAX_FRAME_SIZE);
  file.close();
  return success;
}

// Mirrors writeContactRecord()'s lazy tail-only growth (see that function's note for why
// pre-extending up front is expensive). Slot reuse after the circular buffer wraps around
// (offline_queue_head/tail modulo OFFLINE_QUEUE_SIZE) still pays the seek-into-the-middle cost --
// unavoidable, same caveat as contacts -- but that only happens after OFFLINE_QUEUE_SIZE messages
// have been queued within a single boot session, not on every write like the old pre-extend did.
bool DataStore::writeOfflineQueueRecord(uint32_t idx, const uint8_t* src, uint8_t len) {
  File file = openReadWrite(_getContactsChannelsFS(), "/offlineq");
  if (!file) return false;

  uint32_t want_pos = idx * OFFLINEQ_RECORD_SIZE;
  uint32_t cur_size = file.size();

  bool success;
  if (want_pos >= cur_size) {
    // tail (or past it) -- cheap append, zero-filling any gap first
    uint8_t zeroes[OFFLINEQ_RECORD_SIZE];
    memset(zeroes, 0, sizeof(zeroes));
    success = file.seek(cur_size);
    for (uint32_t pos = cur_size; success && pos < want_pos; pos += OFFLINEQ_RECORD_SIZE) {
      success = (file.write(zeroes, OFFLINEQ_RECORD_SIZE) == OFFLINEQ_RECORD_SIZE);
    }
    success = success && (file.write(&len, 1) == 1);
    success = success && (file.write(src, MAX_FRAME_SIZE) == MAX_FRAME_SIZE);
  } else {
    // updating an already-written slot (buffer wrapped past this point) -- unavoidably expensive
    success = file.seek(want_pos);
    success = success && (file.write(&len, 1) == 1);
    success = success && (file.write(src, MAX_FRAME_SIZE) == MAX_FRAME_SIZE);
  }
  file.close();
  return success;
}
#endif

void DataStore::loadChannels(DataStoreHost* host) {
    File file = openRead(_getContactsChannelsFS(), "/channels2");
    if (file) {
      bool full = false;
      uint8_t channel_idx = 0;
      while (!full) {
        ChannelDetails ch;
        uint8_t unused[4];

        bool success = (file.read(unused, 4) == 4);
        success = success && (file.read((uint8_t *)ch.name, 32) == 32);
        success = success && (file.read((uint8_t *)ch.channel.secret, 32) == 32);

        if (!success) break; // EOF

        if (host->onChannelLoaded(channel_idx, ch)) {
          channel_idx++;
        } else {
          full = true;
        }
      }
      file.close();
    }
}

void DataStore::saveChannels(DataStoreHost* host) {
  File file = openWrite(_getContactsChannelsFS(), "/channels2");
  if (file) {
    uint8_t channel_idx = 0;
    ChannelDetails ch;
    uint8_t unused[4];
    memset(unused, 0, 4);

    while (host->getChannelForSave(channel_idx, ch)) {
      bool success = (file.write(unused, 4) == 4);
      success = success && (file.write((uint8_t *)ch.name, 32) == 32);
      success = success && (file.write((uint8_t *)ch.channel.secret, 32) == 32);

      if (!success) break; // write failed
      channel_idx++;
    }
    file.close();
  }
}

#define MAX_ADVERT_PKT_LEN   (2 + 32 + PUB_KEY_SIZE + 4 + SIGNATURE_SIZE + MAX_ADVERT_DATA_SIZE)

struct BlobRec {
  uint32_t timestamp;
  uint8_t  key[7];
  uint8_t  len;
  uint8_t  data[MAX_ADVERT_PKT_LEN];
};

void DataStore::checkAdvBlobFile() {
  if (!_getContactsChannelsFS()->exists("/adv_blobs")) {
    File file = openWrite(_getContactsChannelsFS(), "/adv_blobs");
    if (file) {
      BlobRec zeroes;
      memset(&zeroes, 0, sizeof(zeroes));
      for (int i = 0; i < MAX_BLOBRECS; i++) {     // pre-allocate to fixed size
        file.write((uint8_t *) &zeroes, sizeof(zeroes));
      }
      file.close();
    }
  }
}

// Reads just the (key, timestamp, len) header fields of every /adv_blobs record into RAM, once at
// boot -- lets getBlobByKey()/putBlobByKey() locate the target record index without re-scanning
// the whole file (up to MAX_BLOBRECS separate flash reads) on every single call.
void DataStore::loadBlobIndex() {
  memset(_blob_index, 0, sizeof(_blob_index));
  File file = openRead(_getContactsChannelsFS(), "/adv_blobs");
  if (file) {
    for (int i = 0; i < MAX_BLOBRECS; i++) {
      BlobRec tmp;
      if (file.read((uint8_t *) &tmp, sizeof(tmp)) != sizeof(tmp)) break;
      memcpy(_blob_index[i].key, tmp.key, sizeof(tmp.key));
      _blob_index[i].timestamp = tmp.timestamp;
      _blob_index[i].len = tmp.len;
    }
    file.close();
  }
}

void DataStore::migrateToSecondaryFS() {
  // migrate old adv_blobs, contacts3 and channels2 files to secondary FS if they don't already exist
  if (!_fsExtra->exists("/adv_blobs")) {
    if (_fs->exists("/adv_blobs")) {
    File oldAdvBlobs = openRead(_fs, "/adv_blobs");
    File newAdvBlobs = openWrite(_fsExtra, "/adv_blobs");

    if (oldAdvBlobs && newAdvBlobs) {
      BlobRec rec;
      size_t count = 0;

      // Copy 20 BlobRecs from old to new
      while (count < 20 && oldAdvBlobs.read((uint8_t *)&rec, sizeof(rec)) == sizeof(rec)) {
        newAdvBlobs.seek(count * sizeof(BlobRec));
        newAdvBlobs.write((uint8_t *)&rec, sizeof(rec));
        count++;
      }
    }
    if (oldAdvBlobs) oldAdvBlobs.close();
    if (newAdvBlobs) newAdvBlobs.close();
    _fs->remove("/adv_blobs");
    }
  }
  if (!_fsExtra->exists("/contacts3")) {
    if (_fs->exists("/contacts3")) {
      File oldFile = openRead(_fs, "/contacts3");
      File newFile = openWrite(_fsExtra, "/contacts3");

      if (oldFile && newFile) {
        uint8_t buf[64];
        int n;
        while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
          newFile.write(buf, n);
        }
      }
      if (oldFile) oldFile.close();
      if (newFile) newFile.close();
      _fs->remove("/contacts3");
    }
  }
  if (!_fsExtra->exists("/channels2")) {
    if (_fs->exists("/channels2")) {
      File oldFile = openRead(_fs, "/channels2");
      File newFile = openWrite(_fsExtra, "/channels2");

      if (oldFile && newFile) {
        uint8_t buf[64];
        int n;
        while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
          newFile.write(buf, n);
        }
      }
      if (oldFile) oldFile.close();
      if (newFile) newFile.close();
      _fs->remove("/channels2");
    }
  }
  // cleanup nodes which have been testing the extra fs, copy _main.id and new_prefs back to primary
  if (_fsExtra->exists("/_main.id")) {
      if (_fs->exists("/_main.id")) {_fs->remove("/_main.id");}
      File oldFile = openRead(_fsExtra, "/_main.id");
      File newFile = openWrite(_fs, "/_main.id");

      if (oldFile && newFile) {
        uint8_t buf[64];
        int n;
        while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
          newFile.write(buf, n);
        }
      }
      if (oldFile) oldFile.close();
      if (newFile) newFile.close();
      _fsExtra->remove("/_main.id");
  }
  if (_fsExtra->exists("/new_prefs")) {
    if (_fs->exists("/new_prefs")) {_fs->remove("/new_prefs");}
      File oldFile = openRead(_fsExtra, "/new_prefs");
      File newFile = openWrite(_fs, "/new_prefs");

      if (oldFile && newFile) {
        uint8_t buf[64];
        int n;
        while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
          newFile.write(buf, n);
        }
      }
      if (oldFile) oldFile.close();
      if (newFile) newFile.close();
      _fsExtra->remove("/new_prefs");
  }
  // remove files from where they should not be anymore
  if (_fs->exists("/adv_blobs")) {
    _fs->remove("/adv_blobs");
  }
  if (_fs->exists("/contacts3")) {
    _fs->remove("/contacts3");
  }
  if (_fs->exists("/channels2")) {
    _fs->remove("/channels2");
  }
  if (_fsExtra->exists("/_main.id")) {
    _fsExtra->remove("/_main.id");
  }
  if (_fsExtra->exists("/new_prefs")) {
    _fsExtra->remove("/new_prefs");
  }
}

uint8_t DataStore::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
  int found_i = -1;
  for (int i = 0; i < MAX_BLOBRECS; i++) {
    if (_blob_index[i].len > 0 && memcmp(key, _blob_index[i].key, sizeof(_blob_index[i].key)) == 0) {
      found_i = i;
      break;
    }
  }
  if (found_i < 0) return 0;  // not found -- no flash access needed at all

  uint8_t len = 0;  // 0 = not found
  File file = openRead(_getContactsChannelsFS(), "/adv_blobs");
  if (file) {
    BlobRec tmp;
    if (file.seek(found_i * sizeof(BlobRec)) && file.read((uint8_t *) &tmp, sizeof(tmp)) == sizeof(tmp)) {
      len = tmp.len;
      memcpy(dest_buf, tmp.data, len);
    }
    file.close();
  }
  return len;
}

bool DataStore::putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], uint8_t len) {
  if (len < PUB_KEY_SIZE+4+SIGNATURE_SIZE || len > MAX_ADVERT_PKT_LEN) return false;
  checkAdvBlobFile();

  // search RAM index for matching key OR evict by oldest timestamp -- no flash access needed
  int target_i = 0;
  uint32_t min_timestamp = 0xFFFFFFFF;
  for (int i = 0; i < MAX_BLOBRECS; i++) {
    if (_blob_index[i].len > 0 && memcmp(key, _blob_index[i].key, sizeof(_blob_index[i].key)) == 0) {
      target_i = i;
      break;
    }
    if (_blob_index[i].timestamp < min_timestamp) {
      min_timestamp = _blob_index[i].timestamp;
      target_i = i;
    }
  }

  BlobRec tmp;
  memcpy(tmp.key, key, sizeof(tmp.key));  // just record 7 byte prefix of key
  memcpy(tmp.data, src_buf, len);
  tmp.len = len;
  tmp.timestamp = _clock->getCurrentTime();

  File file = openReadWrite(_getContactsChannelsFS(), "/adv_blobs");
  bool success = false;
  if (file) {
    success = file.seek(target_i * sizeof(BlobRec)) && (file.write((uint8_t *) &tmp, sizeof(tmp)) == sizeof(tmp));
    file.close();
  }
  if (success) {
    memcpy(_blob_index[target_i].key, tmp.key, sizeof(tmp.key));
    _blob_index[target_i].timestamp = tmp.timestamp;
    _blob_index[target_i].len = tmp.len;
  }
  return success;
}
bool DataStore::deleteBlobByKey(const uint8_t key[], int key_len) {
  return true;
}
