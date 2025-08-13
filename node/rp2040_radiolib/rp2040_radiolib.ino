#include <Arduino.h>
#include <SPI.h>
#include "config.h"             // radio, node, joinEUI/devEUI/appKey/nwkKey, debug(), stateDecode()
#include <Adafruit_SPIFlash.h>
#include <SdFat.h>
#include <ArduinoJson.h>
#include <CRC32.h>
#include "flash_config.h"

// — FLASH & SD for /row.json —
Adafruit_SPIFlash flash(&flashTransport);
FatVolume         sd;
static const char* JSON_ROW = "/row.json";

// — CSV buffer & CRC state —
uint8_t* sendBuf   = nullptr;
size_t   sendSize  = 0;
uint32_t fileHash  = 0;
size_t   ackOffset = 0;
bool     hashSent  = false;

// — timing & chunk size —
const uint32_t TX_INTERVAL_MS = 10UL * 1000UL;  // 10 s
const uint8_t  CHUNK_SZ       = 51;

// — persist next row counter —
uint32_t retrieveRowNum() {
  if (!sd.exists(JSON_ROW)) return 0;
  File32 f(sd.open(JSON_ROW, FILE_READ));
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return 0; }
  f.close();
  return doc["row_num"] | 0U;
}
void storeRowNum(uint32_t row) {
  File32 f(sd.open(JSON_ROW, FILE_WRITE));
  StaticJsonDocument<128> doc;
  doc["row_num"] = row;
  serializeJson(doc, f);
  f.close();
}

// — build a random CSV in RAM and calc its CRC32 —
void prepareCSVAndHash() {
  uint32_t startRow = retrieveRowNum();
  Serial.printf("Starting CSV row: %lu\n", startRow);
  uint16_t n      = random(100, 500);
  size_t   bufSz  = size_t(n)*32 + 32;
  char*    buf    = (char*)malloc(bufSz);
  size_t   pos    = 0;
  pos += snprintf(buf+pos, bufSz-pos, "ROW_NUM,TEMP\n");
  CRC32 crc;
  for (uint16_t i = 0; i < n; i++) {
    float temp = random(3000,4000)/100.0;
    int   len  = snprintf(buf+pos, bufSz-pos, "%u,%.2f\n", startRow+i, temp);
    for (int j = 0; j < len; j++) crc.update((uint8_t)buf[pos+j]);
    pos += len;
  }
  fileHash = crc.finalize();
  sendSize = pos;
  sendBuf  = (uint8_t*)buf;
  storeRowNum(startRow + n);
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // 1) init flash+SD
  SPI.begin();
  flash.begin();
  while (!sd.begin(&flash)) {
    Serial.println("Flash init failed");
    delay(1000);
  }
  Serial.println("Flash OK");

  // 2) prepare CSV
  prepareCSVAndHash();
  ackOffset = 0;

  // 3) init radio
  int16_t st = radio.begin();
  debug(st != RADIOLIB_ERR_NONE, F("radio.begin() failed"), st);

  // 2) configure it *exactly* to the US915-join settings
  //    (SF8 @ 500 kHz, CR4/5)
  radio.setSpreadingFactor(RADIOLIB_LORAWAN_DATA_RATE_SF_8);
  radio.setBandwidth(RADIOLIB_LORAWAN_DATA_RATE_BW_500_KHZ);
  //radio.setCodingRate(      LORA_CR_4_5);

  // 4) configure TX power, etc.
  node.setTxPower(14);
  // 5) OTAA join
  Serial.print("Joining OTAA… ");
  st = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  debug(st != RADIOLIB_ERR_NONE, F("beginOTAA() failed"), st);
  LoRaWANJoinEvent_t joinEvt;
  int err = node.activateOTAA(/*initialDr=*/RADIOLIB_LORAWAN_DATA_RATE_UNUSED, &joinEvt);
  Serial.printf("activateOTAA() → %d, newSession=%d, devNonce=0x%04X\n",
                err, joinEvt.newSession, joinEvt.devNonce);
  Serial.println("joined!");
}

void loop() {
  // done?
  if (!sendBuf) {
    Serial.println("All done!");
    while (1) yield();
  }

  // pick what to send: next chunk or CRC
  size_t  rem   = sendSize - ackOffset;
  uint8_t* up   = nullptr;
  size_t   len  = 0;
  if (rem > 0) {
    len = rem > CHUNK_SZ ? CHUNK_SZ : rem;
    up  = sendBuf + ackOffset;
    Serial.printf("→ TX chunk @%u len=%u\n", (unsigned)ackOffset, (unsigned)len);
  }
  else if (!hashSent) {
    static uint8_t hb[4];
    hb[0] = fileHash >> 24;
    hb[1] = fileHash >> 16;
    hb[2] = fileHash >>  8;
    hb[3] = fileHash >>  0;
    up       = hb;
    len      = 4;
    Serial.printf("→ TX CRC 0x%08lX\n", fileHash);
  }
  else {
    // CRC sent once, but ACK not arrived yet: we still go through sendReceive with zero-length uplink
    Serial.println("→ RX windows for CRC-ACK…");
    up  = nullptr;
    len = 0;
  }

  // buffers + event structs for downlink
  uint8_t        downBuf[4];
  size_t         downLen = sizeof(downBuf);
  LoRaWANEvent_t upEvt, downEvt;

  // 6) send and block for RX1/RX2
  int16_t rc = node.sendReceive(
    up, len,
    /*fPort=*/1,
    /*dataDown=*/downBuf,
    /*lenDown=*/&downLen,
    /*isConfirmed=*/false,
    /*eventUp=*/&upEvt,
    /*eventDown=*/&downEvt
  );

  // error?
  if (rc < 0) {
    Serial.printf("!! sendReceive error %s\n", stateDecode(rc).c_str());
    delay(TX_INTERVAL_MS);
    return;
  }

  // did we get a downlink in either RX window?
  if (rc > 0 && downLen == 4 && downEvt.fPort == 1) {
    // parse 4-byte cumulative ACK
    uint32_t newOff =
      (uint32_t(downBuf[0]) << 24) |
      (uint32_t(downBuf[1]) << 16) |
      (uint32_t(downBuf[2]) <<  8) |
      (uint32_t(downBuf[3]) <<  0);
    Serial.printf("← App-ACK → offset=%u\n", (unsigned)newOff);
    ackOffset = newOff;
    // if we've just sent CRC and it's been ACKed, free the buffer
    if (!hashSent) {
      hashSent = true;
    }
    if (hashSent && ackOffset >= sendSize) {
      free(sendBuf);
      sendBuf = nullptr;
    }
  }
  else if (rc > 0) {
    Serial.println("XX downlink but not a 4-byte ACK");
  }
  else {
    Serial.println("-- no downlink → will retry same chunk");
  }

  // wait before next try
  delay(TX_INTERVAL_MS);
}
