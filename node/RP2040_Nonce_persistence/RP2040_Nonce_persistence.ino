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

