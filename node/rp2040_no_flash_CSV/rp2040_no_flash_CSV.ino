#include <Arduino.h>
#include <SPI.h>
#include <lmic.h>
#include <hal/hal.h>

// — FLASH & FAT libraries —
#include <Adafruit_SPIFlash.h>
#include "flash_config.h"
#include <SdFat.h>

// — JSON for row counter —
#include <ArduinoJson.h>

// — Hashing —
#include <CRC32.h>

#define LMIC_DEBUG_LEVEL 1
#define LMIC_X_DEBUG_LEVEL 1
#define LMIC_ENABLE_user_events 1

// — OTAA keys (replace placeholders) —
static const u1_t PROGMEM APPEUI[8]= { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
static const u1_t PROGMEM DEVEUI[8]= { 0x1f,0x39,0xaf,0xdd,0x20,0xd0,0xe5,0xea };
static const u1_t PROGMEM APPKEY[16]= {
  0xe4,0x5f,0x09,0x10,0x41,0x56,0x2a,0xae,
  0x09,0x4f,0x75,0xd4,0xf4,0x45,0x3a,0xe9
};
void os_getArtEui(u1_t* buf) { memcpy_P(buf, APPEUI, 8); }
void os_getDevEui(u1_t* buf) { memcpy_P(buf, DEVEUI, 8); }
void os_getDevKey(u1_t* buf) { memcpy_P(buf, APPKEY, 16); }

// — LoRa I/O pins —
const lmic_pinmap lmic_pins = {
  .nss = 16,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = 17,
  .dio = {21, 22, 23}
};

// — Globals —
Adafruit_SPIFlash flash(&flashTransport);
FatVolume sd;

static const char* JSON_ROW = "/row.json";

static osjob_t sendjob;
static uint8_t* sendBuf = nullptr;
static size_t sendSize = 0;
//static size_t sendOffset = 0;
static uint32_t fileHash = 0;
static size_t ackOffset = 0;
static bool sendingHash = false;
static size_t lastSentLen = 0;

static int TX_INTERVAL = 15;

// Retrieve last row number from JSON in flash
uint32_t retrieveRowNum() {
  if (!sd.exists(JSON_ROW)) return 0;
  File32 f = sd.open(JSON_ROW, FILE_READ);
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok) {
    f.close(); return 0;
  }
  f.close();
  return doc["row_num"] | 0U;
}

// Store next start row
void storeRowNum(uint32_t row) {
  File32 f = sd.open(JSON_ROW, FILE_WRITE);
  StaticJsonDocument<128> doc;
  doc["row_num"] = row;
  serializeJson(doc, f);
  f.close();
}

// Prepare CSV in-memory and compute CRC
void prepareCSVAndHash() {
  uint32_t startRow = retrieveRowNum();
  Serial.printf("Starting CSV row: %lu\n", startRow);  
  uint16_t n = random(100, 500);
  // estimate buffer size: 32 bytes per line
  size_t bufSize = size_t(n) * 32 + 32;
  char* buf = (char*)malloc(bufSize);
  size_t pos = 0;
  pos += snprintf(buf + pos, bufSize - pos, "ROW_NUM,TEMP\n");
  CRC32 crc;
  for (uint16_t i = 0; i < n; i++) {
    float temp = random(3000, 4000) / 100.0;
    int len = snprintf(buf + pos, bufSize - pos, "%u,%.2f\n", startRow + i, temp);
    pos += len;
    // update CRC per byte
    for (int j = 0; j < len; j++) crc.update((uint8_t)buf[pos - len + j]);
  }
  // finalize
  fileHash = crc.finalize();
  sendSize = pos;
  sendBuf = (uint8_t*)buf;
  // store next start row
  storeRowNum(startRow + n);
}

// OTAA join
void do_join(osjob_t* j) {
  if (LMIC.opmode & OP_TXRXPEND) return;
  LMIC_setTxData2(0, nullptr, 0, 0);
  return;
  //Serial.println(F("Join requested"));
}

#define LORA_MAX_PAYLOAD 51
// Send next chunk
void do_send(osjob_t* j) {
  if (LMIC.opmode & OP_TXRXPEND) {
    //os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
    return;
  }
  if (!sendBuf) 
    return;
  size_t rem = sendSize - ackOffset;
  if (rem > 0) {
    size_t chunk = min(rem, (size_t)LORA_MAX_PAYLOAD);
    lastSentLen = chunk;
    LMIC_setTxData2_strict(1, sendBuf + ackOffset, chunk, /*confirmed=*/1);
    return;
    //Serial.printf("Sent chunk @%u len=%u\n", (unsigned)ackOffset, (unsigned)chunk);
  }
  else
  {
    uint8_t hb[4] = {
      (uint8_t)(fileHash >> 24), (uint8_t)(fileHash >> 16),
      (uint8_t)(fileHash >> 8), (uint8_t)fileHash
    };
    LMIC_setTxData2_strict(1, hb, sizeof(hb), 1);
    return;
    //Serial.printf("Sent hash 0x%08X\n", fileHash);
  }
}

void onEvent(ev_t ev) {
  switch (ev) {
    case EV_JOINING: 
      Serial.print(os_getTime()); Serial.print(F(": "));
      Serial.println(F("EV_JOINING")); 
      break;

    case EV_JOINED:
      Serial.print(os_getTime()); Serial.print(F(": "));
      Serial.println(F("EV_JOINED"));
      LMIC_setLinkCheckMode(0);
      do_send(&sendjob);
      break;

    case EV_TXCOMPLETE:
      // Was this a confirmed uplink that got its MAC‐ACK?
      //Serial.printf("TXRX_ACK: %x LMICtxrxFlags: %x\n", TXRX_ACK, LMIC.txrxFlags);
      if (TXRX_ACK & LMIC.txrxFlags) {
        //size_t len = LMIC.dataLen;
        if (ackOffset >= sendSize) {
          sendingHash = true;
        }
        if (!sendingHash) {
          ackOffset += lastSentLen;
          // Serial.print(os_getTime()); Serial.print(F(": "));
          // Serial.printf("MAC ACK → offset=%u\n", (unsigned)ackOffset);
        }
        else {
          // Serial.print(os_getTime()); Serial.print(F(": "));
          // Serial.println(F("CRC ACK’d → complete"));
          free(sendBuf);
          sendBuf = nullptr;
          break;
        }
      } 
      else {
        // Serial.print(os_getTime()); Serial.print(F(": "));
        // Serial.println(F("No MAC ACK → retry"));
      }
      // schedule next chunk or CRC
      os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
      //Serial.print(os_getTime()); Serial.print(F(": "));
      break;

    case EV_RXCOMPLETE:
      // Your Flask‐sent cumulative ACK arrives here as a downlink
      //if (LMIC.dataLen >= 4) {
      //Serial.println(F(">>> EV_RXCOMPLETE fired!"));
      if (LMIC.dataLen > 0) {
        uint32_t dl = 0;
        for (int i = 0; i < 4; i++) {
          dl = (dl << 8) | LMIC.frame[LMIC.dataBeg + i];
        }
        ackOffset=dl; 
        //Serial.printf("DL ACK %u\n",(unsigned)ackOffset);
        // schedule next chunk now that we know the server got up to dl:
        os_setTimedCallback(&sendjob,
          os_getTime() + sec2osticks(TX_INTERVAL),
          do_send);
      }
      break;
    case EV_JOIN_FAILED:
    case EV_REJOIN_FAILED:
      // Serial.print(os_getTime()); Serial.print(F(": "));  
      // Serial.println(F("JOIN failed, retry in 5s"));
      os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_join);
      break;
    case EV_RXSTART:
      // Serial.printf("%lu: EV_RXSTART (listening for downlink)\n", os_getTime());
      break;
    default: 
      break;
  }
  // Serial.print(os_getTime()); Serial.print(F(": "));
  // Serial.printf("%d\n", (int)ev);
}

void setup() {
  Serial.begin(115200);
  while (!Serial);
  SPI.begin(); 
  flash.begin();
  while (!sd.begin(&flash)) {
    Serial.println(F("Flash init failed")); delay(1000);
  }
  Serial.println(F("Flash OK"));
  os_init(); 
  LMIC_reset();

  LMIC.dn2Dr   = DR_SF8;       // 2nd RX window datarate
  LMIC.dn2Freq = 923300000UL;  // 2nd RX window frequency
  LMIC.rxDelay = 1;
  LMIC.rx1DrOffset = 0;
  LMIC_setClockError(MAX_CLOCK_ERROR*5/100);
  //LMIC_setLinkCheckMode(0);
  //LMIC_setAdrMode(0);
  LMIC_setDrTxpow(DR_SF7, 14);
  LMIC_selectSubBand(2);

  prepareCSVAndHash();
  ackOffset = 0;
  sendingHash = false;

  do_join(&sendjob);
}

void loop() {
  os_runloop_once();
}
