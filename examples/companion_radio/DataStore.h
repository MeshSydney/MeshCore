#pragma once

#include <helpers/IdentityStore.h>
#include <helpers/ContactInfo.h>
#include <helpers/ChannelDetails.h>
#include "NodePrefs.h"

#if defined(EXTRAFS) || defined(QSPIFLASH) || defined(ESP32)
  #define MAX_BLOBRECS 100
#else
  #define MAX_BLOBRECS 20
#endif

class DataStoreHost {
public:
  virtual bool onContactLoaded(const ContactInfo& contact) =0;
  virtual bool getContactForSave(uint32_t idx, ContactInfo& contact) =0;
  virtual bool onChannelLoaded(uint8_t channel_idx, const ChannelDetails& ch) =0;
  virtual bool getChannelForSave(uint8_t channel_idx, ChannelDetails& ch) =0;
};

class DataStore {
  FILESYSTEM* _fs;
  FILESYSTEM* _fsExtra;
  mesh::RTCClock* _clock;
  IdentityStore identity_store;
#ifdef CONTACTS_FLASH_INDEX
  File* _contactsScanFile = nullptr;   // held open between beginContactsScan()/endContactsScan()
  int _contactsScanShard = -1;         // which /contacts3_N shard _contactsScanFile currently holds open
  uint32_t _contactsScanIdx = 0;       // next logical contact index due to be read
  File* _contactsLookupFile = nullptr; // held open between beginContactLookup()/endContactLookup()
  int _contactsLookupShard = -1;       // which /contacts3_N shard _contactsLookupFile currently holds open
  bool _contactsLookupActive = false;  // true between beginContactLookup()/endContactLookup()
#endif
  void reshardContactsFileIfNeeded();  // one-time: split a legacy flat /contacts3 into per-shard files

  // RAM mirror of /adv_blobs' (key, timestamp, len) fields -- lets getBlobByKey()/putBlobByKey()
  // find the matching (or oldest, for eviction) record's index without scanning the whole file.
  struct BlobIndexEntry {
    uint8_t key[7];
    uint32_t timestamp;
    uint8_t len;   // 0 = empty/free slot
  };
  BlobIndexEntry _blob_index[MAX_BLOBRECS];
  void loadBlobIndex();

  void loadPrefsInt(const char *filename, NodePrefs& prefs);
  void checkAdvBlobFile();

public:
  DataStore(FILESYSTEM& fs, mesh::RTCClock& clock);
  DataStore(FILESYSTEM& fs, FILESYSTEM& fsExtra, mesh::RTCClock& clock);
  void begin();
  bool formatFileSystem();
  FILESYSTEM* getPrimaryFS() const { return _fs; }
  FILESYSTEM* getSecondaryFS() const { return _fsExtra; }
  bool loadMainIdentity(mesh::LocalIdentity &identity);
  bool saveMainIdentity(const mesh::LocalIdentity &identity);
  void loadPrefs(NodePrefs& prefs);
  bool savePrefs(NodePrefs& prefs);
  void loadContacts(DataStoreHost* host);
  void saveContacts(DataStoreHost* host, bool (*filter)(const ContactInfo& c) = NULL);
  bool readContactRecord(uint32_t idx, ContactInfo& dest);
  bool writeContactRecord(uint32_t idx, const ContactInfo& src);
  bool truncateContactsFileIfNeeded(uint32_t used_records);
#ifdef CONTACTS_FLASH_INDEX
  // batch-scan API used by BaseChatMesh::loadContactsFlashIndex() -- opens /contacts3 once for
  // the whole boot scan instead of once per record (see loadContactsFlashIndex() for why).
  bool beginContactsScan(uint32_t start_idx);
  bool readNextContactRecord(ContactInfo& dest);
  void endContactsScan();

  // Batch-lookup hooks, used ONLY around the searchPeersByHash() candidate-trying loop in
  // Mesh::onRecvPacket() -- keeps /contacts3 open across the (up to MAX_SEARCH_RESULTS) candidates
  // tried for a single received packet, instead of readContactRecord() opening/closing it for each.
  void beginContactLookup();
  void endContactLookup();
#endif
#ifdef OFFLINE_QUEUE_FLASH
  void resetOfflineQueue();
  bool readOfflineQueueRecord(uint32_t idx, uint8_t* dest, uint8_t& len);
  bool writeOfflineQueueRecord(uint32_t idx, const uint8_t* src, uint8_t len);
#endif
  void loadChannels(DataStoreHost* host);
  void saveChannels(DataStoreHost* host);
  void migrateToSecondaryFS();
  uint8_t getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]);
  bool putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], uint8_t len);
  bool deleteBlobByKey(const uint8_t key[], int key_len);
  File openRead(const char* filename);
  File openRead(FILESYSTEM* fs, const char* filename);
  bool removeFile(const char* filename);
  bool removeFile(FILESYSTEM* fs, const char* filename);
  uint32_t getStorageUsedKb() const;
  uint32_t getStorageTotalKb() const;

private:
  FILESYSTEM* _getContactsChannelsFS() const { if (_fsExtra) return _fsExtra; return _fs;};
};
