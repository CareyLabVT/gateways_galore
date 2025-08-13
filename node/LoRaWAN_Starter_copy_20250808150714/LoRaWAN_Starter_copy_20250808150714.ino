// // ==== Forward decls so Arduino's auto-prototype doesn't break things ====
// struct PersistBlob;
// static bool loadPersist(PersistBlob& pb);
// static bool savePersist();
// static void fsDiag();
// // =======================================================================

// #include <RadioLib.h>
// #include <Adafruit_SPIFlash.h>
// #include <SdFat.h>

// #include "flash_config.h"   // your transport selection (RP2040 internal flash)
// #include "config.h"         // your radio/node objects, keys, region, debug(), uplinkIntervalSeconds

// // ---------------- user knobs ----------------
// static const uint32_t SERIAL_BAUD        = 115200;
// static const uint8_t  SAVE_EVERY_N_UL    = 4;      // save session every N uplinks
// static const uint8_t  MAC_PING_EVERY     = 0;      // 0=off; else send LinkCheck/DeviceTime every N uplinks
// static const uint32_t UPLINK_PORT        = 1;
// static const uint32_t UPLINK_INTERVAL_S  = uplinkIntervalSeconds;
// static const uint8_t  CID_LINK_CHECK_REQ  = 0x02;
// static const uint8_t  CID_DEVICE_TIME_REQ = 0x0D;
// // --------------------------------------------

// // SdFat path: prefer no leading slash
// static const char* kSessionPath = "lorawan_session.bin";

// // Adafruit SPIFlash + FAT FS
// Adafruit_SPIFlash flash(&flashTransport);
// FatFileSystem     fatfs;
// File32              fsFile;

// // quick FNV-1a for integrity
// static uint32_t fnv1a32(const uint8_t* d, size_t n) {
//   uint32_t h = 0x811C9DC5ul;
//   while (n--) { h ^= *d++; h *= 16777619ul; }
//   return h;
// }

// // Persisted blob (contains RadioLib buffers as-is)
// #pragma pack(push, 1)
// struct PersistBlob {
//   uint32_t magic;     // 'LWS1'
//   uint16_t version;   // 0x0001
//   uint16_t reserved;
//   uint8_t  nonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
//   uint8_t  session[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
//   uint32_t checksum;  // FNV-1a of all fields above
// };
// #pragma pack(pop)

// static uint32_t uplinkCount = 0;

// // ---------- FS helpers ----------
// static bool fsBegin() {
//   if (!flash.begin()) {
//     Serial.println(F("FS: flash.begin() FAILED"));
//     return false;
//   }
//   if (!fatfs.begin(&flash)) {
//     Serial.println(F("FS: FAT mount FAILED (format with Adafruit_SPIFlash examples)"));
//     return false;
//   }
//   return true;
// }

// static void fsDiag() {
//   if (!fsBegin()) {
//     Serial.println(F("FS: mount failed, skipping diag."));
//     return;
//   }
//   Serial.println(F("FS: root listing:"));
//   File32 root = fatfs.open("/");
//   if (root) {
//     for (File32 f = root.openNextFile(); f; f = root.openNextFile()) {
//       Serial.print(F("  "));
//       Serial.print(f.name());
//       Serial.print(F("  size="));
//       Serial.println((uint32_t)f.size());
//       f.close();
//     }
//     root.close();
//   }
//   if (fatfs.exists(kSessionPath)) {
//     File32 s = fatfs.open(kSessionPath, O_READ);
//     if (s) {
//       Serial.print(F("FS: ")); Serial.print(kSessionPath);
//       Serial.print(F(" exists, size=")); Serial.println((uint32_t)s.size());
//       s.close();
//     }
//   } else {
//     Serial.print(F("FS: ")); Serial.print(kSessionPath);
//     Serial.println(F(" does NOT exist"));
//   }
// }

// static bool loadPersist(PersistBlob& pb) {
//   if (!fsBegin()) return false;
//   if (!fatfs.exists(kSessionPath)) return false;

//   fsFile = fatfs.open(kSessionPath, O_READ);
//   if (!fsFile) return false;

//   const int32_t need = (int32_t)sizeof(PersistBlob);
//   const int32_t sz   = (int32_t)fsFile.size();
//   if (sz < need) { fsFile.close(); return false; }

//   // Read the LAST record (handles prior append-style saves)
//   int32_t off = sz - need;
//   if (!fsFile.seekSet((uint32_t)off)) { fsFile.close(); return false; }

//   int32_t rd = fsFile.read((uint8_t*)&pb, need);
//   fsFile.close();
//   if (rd != need) return false;

//   if (pb.magic != 0x3153574Cul /* 'LWS1' */ || pb.version != 0x0001) {
//     Serial.println(F("FS: bad magic/version"));
//     return false;
//   }
//   uint32_t expect = fnv1a32((const uint8_t*)&pb, sizeof(PersistBlob) - sizeof(pb.checksum));
//   if (expect != pb.checksum) {
//     Serial.println(F("FS: checksum mismatch"));
//     return false;
//   }
//   return true;
// }

// static bool savePersist() {
//   if (!fsBegin()) return false;

//   PersistBlob pb{};
//   pb.magic   = 0x3153574Cul;  // 'LWS1'
//   pb.version = 0x0001;

//   memcpy(pb.nonces,  node.getBufferNonces(),  RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
//   memcpy(pb.session, node.getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);

//   pb.checksum = fnv1a32((const uint8_t*)&pb, sizeof(PersistBlob) - sizeof(pb.checksum));

//   // TRUNCATE so file is exactly one record
//   fsFile = fatfs.open(kSessionPath, O_CREAT | O_WRITE | O_TRUNC);
//   if (!fsFile) { Serial.println(F("FS: open(O_TRUNC) failed")); return false; }
//   int32_t wr = fsFile.write((const uint8_t*)&pb, sizeof(PersistBlob));
//   fsFile.flush();
//   fsFile.close();
//   if (wr != (int32_t)sizeof(PersistBlob)) { Serial.println(F("FS: short write")); return false; }

//   // Verify
//   fsFile = fatfs.open(kSessionPath, O_READ);
//   if (!fsFile) { Serial.println(F("FS: reopen for verify failed")); return false; }
//   PersistBlob verify{};
//   int32_t rd = fsFile.read((uint8_t*)&verify, sizeof(PersistBlob));
//   fsFile.close();
//   if (rd != (int32_t)sizeof(PersistBlob)) { Serial.println(F("FS: verify short read")); return false; }
//   uint32_t expect = fnv1a32((const uint8_t*)&verify, sizeof(PersistBlob) - sizeof(verify.checksum));
//   if (expect != verify.checksum) { Serial.println(F("FS: verify checksum mismatch")); return false; }

//   Serial.println(F("FS: session file written & verified (truncated)."));
//   return true;
// }

// // ---- minimal event log ----
// static void printEvent(const char* tag, const LoRaWANEvent_t& e) {
//   Serial.print(tag);
//   Serial.print(F(": dir="));  Serial.print(e.dir);
//   Serial.print(F(" conf="));  Serial.print(e.confirmed);
//   Serial.print(F(" fCnt="));  Serial.print(e.fCnt);
//   Serial.print(F(" port="));  Serial.print(e.fPort);
//   Serial.print(F(" DR="));    Serial.print(e.datarate);
//   Serial.print(F(" freq="));  Serial.print(e.freq, 3);
//   Serial.print(F(" rssi="));  Serial.println(e.power);
// }

// void setup() {
//   Serial.begin(SERIAL_BAUD);
//   while (!Serial) {}
//   delay(600);
//   Serial.println(F("\nSetup..."));

//   // Filesystem diag
//   fsDiag();

//   // Radio init
//   int16_t st = radio.begin();
//   debug(st != RADIOLIB_ERR_NONE, F("Radio init failed"), st, true);

//   // Slight RX timing margin
//   node.scanGuard = 30;  // ms
//   Serial.print(F("scanGuard=")); Serial.println(node.scanGuard);

//   // 1) Provide credentials FIRST (this resets internal state!)
//   st = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
//   debug(st != RADIOLIB_ERR_NONE, F("LoRaWAN node init failed"), st, true);
//   node.setADR(true);

//   // 2) NOW restore persisted buffers (so beginOTAA doesn't wipe them)
//   PersistBlob pb{};
//   if (loadPersist(pb)) {
//     Serial.println(F("Persisted session FOUND; restoring buffers AFTER beginOTAA()."));
//     node.setBufferNonces(pb.nonces);
//     node.setBufferSession(pb.session);
//   } else {
//     Serial.println(F("No persisted session (or bad blob); cold start."));
//   }

//   // 3) Activate: handle RadioLib "success" return codes properly
//   LoRaWANJoinEvent_t je{};
//   st = node.activateOTAA(RADIOLIB_LORAWAN_DATA_RATE_UNUSED, &je);
//   if (st == RADIOLIB_LORAWAN_SESSION_RESTORED) {
//     Serial.println(F("Session restored (no join performed)."));
//   } else if (st == RADIOLIB_LORAWAN_NEW_SESSION) {
//     Serial.print(F("Joined (new session). devNonce="));
//     Serial.print(je.devNonce);
//     Serial.print(F(" joinNonce="));
//     Serial.println(je.joinNonce);
//   } else {
//     debug(true, F("activateOTAA failed"), st, true);
//   }

//   // 4) Persist current state so FCnt/params survive resets
//   if (savePersist()) {
//     Serial.println(F("Session persisted to flash."));
//   } else {
//     Serial.println(F("WARN: failed to persist session to flash."));
//   }

//   Serial.println(F("Ready!\n"));
// }

// void loop() {
//   ++uplinkCount;

//   uint8_t v1 = radio.random(100);
//   uint16_t v2 = radio.random(2000);
//   uint8_t payload[3] = { v1, highByte(v2), lowByte(v2) };

//   if (MAC_PING_EVERY && (uplinkCount % MAC_PING_EVERY) == 0) {
//     node.sendMacCommandReq(CID_LINK_CHECK_REQ);
//     node.sendMacCommandReq(CID_DEVICE_TIME_REQ);
//     Serial.println(F("Queued MAC: LinkCheckReq + DeviceTimeReq"));
//   }

//   LoRaWANEvent_t evUp{}, evDn{};
//   uint8_t  dlBuf[64] = {0};
//   size_t   dlLen = sizeof(dlBuf);

//   int16_t win = node.sendReceive(payload, sizeof(payload),
//                                  UPLINK_PORT,
//                                  dlBuf, &dlLen,
//                                  /*confirmed=*/false,
//                                  &evUp, &evDn);

//   if (win < RADIOLIB_ERR_NONE) {
//     debug(true, F("sendReceive failed"), win, false);
//   }

//   printEvent("UP", evUp);

//   if (win > 0) {
//     Serial.print(F("Downlink in Rx")); Serial.println(win);
//     printEvent("DN", evDn);
//   } else {
//     Serial.println(F("No downlink received"));
//   }

//   Serial.print(F("FCntUp="));     Serial.print(node.getFCntUp());
//   Serial.print(F(" NFCntDown=")); Serial.print(node.getNFCntDown());
//   Serial.print(F(" AFCntDown=")); Serial.println(node.getAFCntDown());

//   if ((uplinkCount % SAVE_EVERY_N_UL) == 0) {
//     if (savePersist()) Serial.println(F("Session persisted."));
//   }

//   Serial.print(F("Next uplink in ")); Serial.print(UPLINK_INTERVAL_S); Serial.println(F(" s\n"));
//   delay(UPLINK_INTERVAL_S * 1000UL);
// }

// --- forward decls (Arduino auto-prototype guard) ---
static bool fsBegin();
static bool loadBlob(const char* path, uint8_t* dst, size_t len);
static bool saveBlob(const char* path, const uint8_t* src, size_t len);
static int16_t lwActivate();     // just like your ESP32 lwActivate()
// ----------------------------------------------------

#include <RadioLib.h>
#include <Adafruit_SPIFlash.h>
#include <SdFat.h>

#include "flash_config.h"   // your transport config
#include "config.h"         // radio, node, keys, region, debug(), uplinkIntervalSeconds

// Files (no leading slash for SdFat)
static const char* PATH_NONCES  = "lw_nonces.bin";
static const char* PATH_SESSION = "lw_session.bin";

// Flash + FAT
Adafruit_SPIFlash flash(&flashTransport);
FatFileSystem     fatfs;

static bool fsBegin() {
  if (!flash.begin())  { Serial.println(F("flash.begin() failed"));  return false; }
  if (!fatfs.begin(&flash)) { Serial.println(F("FAT mount failed")); return false; }
  return true;
}

static bool loadBlob(const char* path, uint8_t* dst, size_t len) {
  if (!fsBegin()) return false;
  File32 f = fatfs.open(path, O_READ);
  if (!f) return false;
  if ((size_t)f.size() != len) { f.close(); return false; }
  int32_t rd = f.read(dst, len);
  f.close();
  return rd == (int32_t)len;
}

static bool saveBlob(const char* path, const uint8_t* src, size_t len) {
  if (!fsBegin()) return false;
  File32 f = fatfs.open(path, O_CREAT | O_WRITE | O_TRUNC);
  if (!f) return false;
  int32_t wr = f.write(src, len);
  f.flush();
  f.close();
  return wr == (int32_t)len;
}

// Return: RADIOLIB_LORAWAN_NEW_SESSION or RADIOLIB_LORAWAN_SESSION_RESTORED
static int16_t lwActivate() {
  int16_t st;

  // RadioLib: provide credentials FIRST (this resets internal state)
  st = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  debug(st != RADIOLIB_ERR_NONE, F("LoRaWAN init failed"), st, true);

  // Try to restore nonces from flash
  bool restoredSomething = false;
  {
    uint8_t buf[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
    if (loadBlob(PATH_NONCES, buf, sizeof(buf))) {
      st = node.setBufferNonces(buf);
      debug(st != RADIOLIB_ERR_NONE, F("setBufferNonces failed"), st, false);
      restoredSomething |= (st == RADIOLIB_ERR_NONE);
    }
  }

  // Activate: either restores the session or performs a new join
  LoRaWANJoinEvent_t je{};
  st = node.activateOTAA(RADIOLIB_LORAWAN_DATA_RATE_UNUSED, &je);

  if (st == RADIOLIB_LORAWAN_NEW_SESSION) {
    Serial.println(F("Joined (new session). Saving nonces..."));

    // Save *nonces* right after join so DevNonce is preserved across resets
    uint8_t* n = node.getBufferNonces();
    if (!saveBlob(PATH_NONCES, n, RADIOLIB_LORAWAN_NONCES_BUF_SIZE)) {
      Serial.println(F("WARN: saving nonces failed"));
    }
    return st;
  }

  // Anything else is a real error
  debug(true, F("activateOTAA failed"), st, true);
  return st;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  delay(800);

  int16_t st = radio.begin();
  debug(st != RADIOLIB_ERR_NONE, F("Radio init failed"), st, true);

  node.setADR(true);          // optional; leave on if you use ADR
  node.scanGuard = 30;        // small RX timing cushion

  // Activate (restore-or-join)
  st = lwActivate();          // returns NEW_SESSION or SESSION_RESTORED
  (void)st;

  Serial.println(F("Ready!"));
}

void loop() {
  // --- your app payload ---
  uint8_t v1 = radio.random(100);
  uint16_t v2 = radio.random(2000);
  uint8_t payload[3] = { v1, highByte(v2), lowByte(v2) };

  // Send (unconfirmed, wait for downlink if any)
  int16_t win = node.sendReceive(payload, sizeof(payload));
  if (win < RADIOLIB_ERR_NONE) {
    debug(true, F("sendReceive failed"), win, false);
  }

  Serial.print(F("FCntUp=")); Serial.println(node.getFCntUp());
  delay(uplinkIntervalSeconds * 1000UL);
}

