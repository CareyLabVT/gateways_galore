#include <Arduino.h>
#include <SPI.h>
#include <lmic.h>
#include <hal/hal.h>

// — FLASH & FAT libraries —
#include <Adafruit_SPIFlash.h>        // for external QSPI flash
#include "flash_config.h"             // defines flashTransport
#include <SdFat.h>                    // the Adafruit fork

// — JSON for row counter —
#include <ArduinoJson.h>

// — Hashing —
#include <CRC32.h>                    // from Library Manager

// — OTAA keys (replace FILLMEIN) —
static const u1_t PROGMEM APPEUI[8]= { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
static const u1_t PROGMEM DEVEUI[8]= { 0x1f,0x39,0xaf,0xdd,0x20,0xd0,0xe5,0xea };
static const u1_t PROGMEM APPKEY[16]= {
  0xe4,0x5f,0x09,0x10,0x41,0x56,0x2a,0xae,
  0x09,0x4f,0x75,0xd4,0xf4,0x45,0x3a,0xe9
};
void os_getArtEui(u1_t* buf) { memcpy_P(buf, APPEUI, 8); }
void os_getDevEui(u1_t* buf) { memcpy_P(buf, DEVEUI, 8); }
void os_getDevKey(u1_t* buf) { memcpy_P(buf, APPKEY, 16); }

const unsigned TX_INTERVAL = 10;

// — LoRa I/O pins for Feather RP2040+RFM95 —
const lmic_pinmap lmic_pins = {
  .nss  = 16,
  .rxtx = LMIC_UNUSED_PIN,
  .rst  = 17,
  .dio  = { 21, 22, 23 }
};

// — File & JSON names —
static const char* JSON_ROW = "/row.json";
static const char* CSV_FILE = "/data.csv";

// — Globals for FAT —
Adafruit_SPIFlash flash(&flashTransport);
FatVolume sd;

// — Globals for send job —
static osjob_t sendjob;
static uint8_t* sendBuf    = nullptr;
static size_t   sendSize   = 0;
static size_t   sendOffset = 0;
static uint32_t fileHash   = 0;
static bool     sendingFile = false;
static bool     sendingHash = false;

// — Helpers: JSON row counter —
uint32_t retrieveRowNum() {
  if (!sd.exists(JSON_ROW)) return 0;
  File32 jf = sd.open(JSON_ROW, FILE_READ);
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, jf) != DeserializationError::Ok) {
    jf.close();
    return 0;
  }
  jf.close();
  return doc["row_num"] | 0U;
}

void storeRowNum(uint32_t row) {
  File32 jf = sd.open(JSON_ROW, FILE_WRITE);
  StaticJsonDocument<128> doc;
  doc["row_num"] = row;
  serializeJson(doc, jf);
  jf.close();
}

// — Print CSV contents to Serial —
void printCSV() {
  File32 r = sd.open(CSV_FILE, FILE_READ);
  if (!r) {
    Serial.println(F("Failed to open CSV for printing"));
    return;
  }
  Serial.println(F("=== CSV CONTENTS ==="));
  while (r.available()) {
    String line = r.readStringUntil('\n');
    Serial.println(line);
  }
  Serial.println(F("===================="));
  r.close();
}

// — Generate CSV & update row counter —
void makeCSV() {
  uint32_t startRow = retrieveRowNum();
  uint16_t n = random(1000, 10000);
  File32 f = sd.open(CSV_FILE, FILE_WRITE);
  f.println("ROW_NUM,TEMP");
  for (uint16_t i = 0; i < n; i++) {
    float temp = random(2000, 3500) / 100.0;
    f.print(startRow + i);
    f.print(',');
    f.println(temp, 2);
  }
  f.close();
  // store next start (one past last generated)
  storeRowNum(startRow + n);
}

// — Compute CRC32 of the CSV file —
uint32_t computeCRC(const char* path) {
  File32 f = sd.open(path, FILE_READ);
  CRC32 crc;
  while (f.available()) {
    crc.update(f.read());
  }
  f.close();
  return crc.finalize();
}

// — Prepare send buffer & hash (no compression) —
void prepareSend() {
  File32 f = sd.open(CSV_FILE, FILE_READ);
  sendSize = f.size();
  sendBuf  = (uint8_t*)malloc(sendSize);
  f.read(sendBuf, sendSize);
  f.close();
  fileHash   = computeCRC(CSV_FILE);
  sendOffset = 0;
  sendingFile = true;
  sendingHash = false;
}

// — LMIC join job —
void do_join(osjob_t* j) {
  if (LMIC.opmode & OP_TXRXPEND) return;
  LMIC_setTxData2(0, nullptr, 0, 0);
  Serial.println(F("Join requested"));
}

// — LMIC send job: send CSV then hash, then delete CSV —
#define LORA_MAX_PAYLOAD 51
// — LMIC send job: send CSV then hash, then delete CSV —
#define LORA_MAX_PAYLOAD 51
void do_send(osjob_t* j) {
  // If a previous TX is still pending, defer
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("OP_TXRXPEND, deferring"));
    os_setTimedCallback(&sendjob,
      os_getTime() + sec2osticks(TX_INTERVAL), do_send);
    return;
  }

  // Send file chunks
  if (sendingFile) {
    Serial.println(F("Sending next file chunk."));
    size_t chunk = min((size_t)LORA_MAX_PAYLOAD, sendSize - sendOffset);
    LMIC_setTxData2(1, sendBuf + sendOffset, chunk, 0);
    sendOffset += chunk;
    if (sendOffset >= sendSize) {
      sendingFile = false;
      sendingHash = true;
    }
    Serial.println(F("Packet queued"));
    // schedule next chunk or hash
    os_setTimedCallback(&sendjob,
      os_getTime() + sec2osticks(TX_INTERVAL), do_send);
  }
  // Send hash after file done
  else if (sendingHash) {
    Serial.println(F("Sending hash."));
    uint8_t hb[4] = {
      (uint8_t)(fileHash >> 24), (uint8_t)(fileHash >> 16),
      (uint8_t)(fileHash >>  8), (uint8_t)(fileHash      )
    };
    LMIC_setTxData2(1, hb, sizeof(hb), 0);
    sendingHash = false;
    Serial.println(F("Packet queued"));
    os_setTimedCallback(&sendjob,
      os_getTime() + sec2osticks(TX_INTERVAL), do_send);
  }
  // First invocation: prepare buffer
  if (!sendingFile && !sendingHash) {
    prepareSend();
  }

  // Send file chunks
  if (sendingFile) {
    Serial.println(F("Sending file chunk."));
    size_t chunk = min((size_t)LORA_MAX_PAYLOAD, sendSize - sendOffset);
    LMIC_setTxData2(1, sendBuf + sendOffset, chunk, 0);
    sendOffset += chunk;
    if (sendOffset >= sendSize) {
      sendingFile = false;
      sendingHash = true;
    }
    Serial.println(F("Packet queued"));
    // schedule next chunk or hash
    os_setTimedCallback(&sendjob,
      os_getTime() + sec2osticks(TX_INTERVAL), do_send);
  }
  // Send hash after file done
  else if (sendingHash) {
    Serial.println(F("Sending hash."));
    uint8_t hb[4] = {
      (uint8_t)(fileHash >> 24),
      (uint8_t)(fileHash >> 16),
      (uint8_t)(fileHash >>  8),
      (uint8_t)(fileHash      )
    };
    LMIC_setTxData2(1, hb, sizeof(hb), 0);
    sendingHash = false;
    Serial.println(F("Packet queued"));
    os_setTimedCallback(&sendjob,
      os_getTime() + sec2osticks(TX_INTERVAL), do_send);
  }
  else {
    if (!sendingFile && !sendingHash) {
      prepareSend();
    }
    if (sendingFile) {
      Serial.println(F("Sending file."));
      size_t chunk = min((size_t)LORA_MAX_PAYLOAD, sendSize - sendOffset);
      LMIC_setTxData2(1, sendBuf + sendOffset, chunk, 0);
      sendOffset += chunk;
      if (sendOffset >= sendSize) {
        sendingFile = false;
        sendingHash = true;
      }
    }
    else if (sendingHash) {
      Serial.println(F("Sending hash."));
      uint8_t hb[4] = {
        (uint8_t)(fileHash >> 24),
        (uint8_t)(fileHash >> 16),
        (uint8_t)(fileHash >>  8),
        (uint8_t)(fileHash      )
      };
      LMIC_setTxData2(1, hb, sizeof(hb), 0);
      sendingHash = false;
    }
    Serial.println(F("Packet queued"));
  }
  if (sendingFile || sendingHash) {
    os_setTimedCallback(&sendjob,
      os_getTime() + sec2osticks(TX_INTERVAL), do_send);
  } else {
    free(sendBuf);
    Serial.println(F("All done"));
    // delete CSV after complete transmit
    if (sd.exists(CSV_FILE)) {
      sd.remove(CSV_FILE);
      Serial.println(F("CSV deleted"));
    }
  }
}

// — LMIC event handler —
void onEvent(ev_t ev) {
  Serial.print(os_getTime()); Serial.print(F(": "));  
  switch(ev) {
    case EV_JOINING:  Serial.println(F("EV_JOINING")); break;
    case EV_JOINED:
      prepareSend();
      Serial.println(F("EV_JOINED"));
      LMIC_setLinkCheckMode(0);
      os_setTimedCallback(&sendjob,
        os_getTime() + sec2osticks(TX_INTERVAL), do_send);
      break;
    case EV_TXCOMPLETE:
      Serial.println(F("EV_TXCOMPLETE"));
      break;
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println(F("** Feather RP2040 LoRa File TX **"));

  randomSeed(micros());

  // init flash + FAT
  SPI.begin();
  flash.begin();
  while (!sd.begin(&flash)) {
    Serial.println(F("Flash FAT init failed!"));
    delay(1000);
  }
  Serial.println(F("Flash FAT OK"));

  // generate & print CSV
  makeCSV();
  printCSV();

  // LMIC init & OTAA join
  os_init(); LMIC_reset();
  LMIC_setLinkCheckMode(0);
  LMIC_setDrTxpow(DR_SF7,14);
  LMIC_selectSubBand(1);
  do_join(&sendjob);
}

void loop() {
  os_runloop_once();
}
