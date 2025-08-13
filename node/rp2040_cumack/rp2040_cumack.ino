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

// — LoRa pins for Feather RP2040 —
const lmic_pinmap lmic_pins = {
  .nss  = 16,
  .rxtx = LMIC_UNUSED_PIN,
  .rst  = 17,
  .dio  = {21,22,23},
};

// — OTAA keys (fill these in!) —
static const u1_t PROGMEM APPEUI[8] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
static const u1_t PROGMEM DEVEUI[8] = { 0x1f,0x39,0xaf,0xdd,0x20,0xd0,0xe5,0xea };
static const u1_t PROGMEM APPKEY[16]= {
  0xe4,0x5f,0x09,0x10,0x41,0x56,0x2a,0xae,
  0x09,0x4f,0x75,0xd4,0xf4,0x45,0x3a,0xe9
};
void os_getArtEui(u1_t* buf) { memcpy_P(buf, APPEUI, 8); }
void os_getDevEui(u1_t* buf) { memcpy_P(buf, DEVEUI, 8); }
void os_getDevKey(u1_t* buf) { memcpy_P(buf, APPKEY, 16); }

Adafruit_SPIFlash flash(&flashTransport);
FatVolume sd;

// scheduling job
static osjob_t sendjob;

// in-RAM CSV + CRC
static uint8_t* sendBuf = nullptr;
static size_t   sendSize = 0;
static uint32_t fileCRC  = 0;

// how many bytes the server has cumulatively ACK’d
static size_t ackOffset = 0;

// for retransmission timing
static const unsigned TX_INTERVAL  = 10;  // seconds
static const uint8_t  UPLINK_PORT  = 1;
static const uint8_t  ACK_PORT     = 2;
static const size_t   MAX_PAYLOAD  = 51;

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

// send the next chunk or the final CRC
void do_send(osjob_t* j) {
  if (LMIC.opmode & OP_TXRXPEND) {
    // radio busy, retry in 1s
    os_setTimedCallback(j, os_getTime() + sec2osticks(1), do_send);
    return;
  }
  if (!sendBuf) return;

  size_t rem = sendSize - ackOffset;
  if (rem > 0) {
    size_t chunk = min(rem, MAX_PAYLOAD);
    LMIC_setTxData2(UPLINK_PORT, sendBuf + ackOffset, chunk, /*confirmed=*/0);
    Serial.printf("→ Uplink chunk @%u len=%u\n", (unsigned)ackOffset, (unsigned)chunk);
  }
  else {
    // send final 4-byte CRC on ACK_PORT
    uint8_t crcBytes[4] = {
      uint8_t(fileCRC>>24), uint8_t(fileCRC>>16),
      uint8_t(fileCRC>> 8), uint8_t(fileCRC    )
    };
    LMIC_setTxData2(ACK_PORT, crcBytes, 4, 0);
    Serial.println("→ Uplink CRC frame");
  }
}

// LMIC event callback
void onEvent(ev_t ev) {
  Serial.printf("EV=%d\n", ev);
  switch(ev) {

    case EV_JOINED:
      // we've joined, build our CSV & CRC
      prepareCSVAndCRC();
      ackOffset = 0;
      do_send(&sendjob);
      break;

    case EV_TXCOMPLETE:
      // schedule a retry in case no app-level ACK comes back
      os_setTimedCallback(&sendjob,
        os_getTime() + sec2osticks(TX_INTERVAL),
        do_send);
      break;

    case EV_RXCOMPLETE:
      // did we get an application-level ACK on port=ACK_PORT?
      if ((LMIC.txrxFlags & TXRX_PORT) &&
          LMIC.frame[LMIC.dataBeg-1] == ACK_PORT &&
          LMIC.dataLen == 4)
      {
        uint32_t newAck = 0;
        for (int i = 0; i < 4; i++) {
          newAck = (newAck<<8) | LMIC.frame[LMIC.dataBeg + i];
        }
        if (newAck > ackOffset) {
          ackOffset = newAck;
          Serial.printf("← App-ACK offset=%u\n", (unsigned)ackOffset);
          // if not done, send next chunk immediately
          if (ackOffset < sendSize) {
            do_send(&sendjob);
          }
          else {
            Serial.println("✔ All data received by server");
            free(sendBuf);
            sendBuf = nullptr;
          }
        }
      }
      break;

    case EV_JOIN_FAILED:
    case EV_REJOIN_FAILED:
      // re‐try join in 10 s
      os_setTimedCallback(&sendjob,
        os_getTime() + sec2osticks(10),
        do_send);
      break;

    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  while(!Serial);

  SPI.begin();
  os_init();
  LMIC_reset();

  // set clock error tolerance
  LMIC_setClockError(MAX_CLOCK_ERROR/20);
  LMIC_setLinkCheckMode(0);
  LMIC_setAdrMode(1);
  LMIC_setDrTxpow(DR_SF7,14);
  LMIC_selectSubBand(1);

  // kick off OTAA
  LMIC_startJoining();
}

void loop() {
  os_runloop_once();
}
