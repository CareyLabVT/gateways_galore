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

const unsigned TX_INTERVAL = 60;

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

// — Generate CSV & update row counter —
void makeCSV() {
  uint32_t startRow = retrieveRowNum();
  uint16_t n = random(1000, 10000);
  File32 f = sd.open(CSV_FILE, FILE_WRITE);
  f.println("ROW_NUM,TEMP");
  for (uint16_t i = 0; i < n; i++) {
    float temp = random(2000, 3500) / 100.0;  // 20.00–35.00°C
    f.print(startRow + i);
    f.print(',');
    f.println(temp, 2);
  }
  f.close();
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

// — Prepare the LMIC send buffer & hash (no compression) —
void prepareSend() {
  File32 f = sd.open(CSV_FILE, FILE_READ);
  sendSize = f.size();
  sendBuf  = (uint8_t*)malloc(sendSize);
  f.read(sendBuf, sendSize);
  f.close();

  fileHash   = computeCRC(CSV_FILE);
  sendOffset = 0;
  sendingFile = true;
  //sendingHash = false;
}

void do_join(osjob_t* j) {
  if (LMIC.opmode & OP_TXRXPEND) return;
  // a zero-byte send on fPort=0 triggers OTAA
  LMIC_setTxData2(0, nullptr, 0, 0);
  Serial.println(F("Join requested"));
}

// — LMIC send job: fragment CSV, then hash —
#define LORA_MAX_PAYLOAD 51
void do_send(osjob_t* j) {
  if (LMIC.opmode & OP_TXRXPEND) {
      Serial.println(F("OP_TXRXPEND, not sending"));
  } 
  else {
      prepareSend();
      // Prepare upstream data transmission at the next possible time.
      if (sendingFile) {
        Serial.println("Sending file.");
        size_t chunk = min((size_t)LORA_MAX_PAYLOAD, sendSize - sendOffset);
        LMIC_setTxData2(1, sendBuf + sendOffset, chunk, 0);
        sendOffset += chunk;
        if (sendOffset >= sendSize) {
          sendingFile = false;
          sendingHash = true;
        }
      }
      else if (sendingHash) {
        Serial.println("Sending hash.");
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
    Serial.println("All done");
  }
}

void printHex2(unsigned v) {
    v &= 0xff;
    if (v < 16)
        Serial.print('0');
    Serial.print(v, HEX);
}

// — LMIC event handler: after join, kick off send —
// void onEvent(ev_t ev) {
//   Serial.print(os_getTime()); Serial.print(": ");
//   if (ev == EV_JOINED) {
//     Serial.println(F("EV_JOINED"));
//     LMIC_setLinkCheckMode(0);
//     prepareSend();
//     os_setTimedCallback(&sendjob,
//       os_getTime() + sec2osticks(1), do_send);
//   }
// }
void onEvent (ev_t ev) {
    Serial.print(os_getTime());
    Serial.print(": ");
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            Serial.println(F("EV_SCAN_TIMEOUT"));
            break;
        case EV_BEACON_FOUND:
            Serial.println(F("EV_BEACON_FOUND"));
            break;
        case EV_BEACON_MISSED:
            Serial.println(F("EV_BEACON_MISSED"));
            break;
        case EV_BEACON_TRACKED:
            Serial.println(F("EV_BEACON_TRACKED"));
            break;
        case EV_JOINING:
            Serial.println(F("EV_JOINING"));
            break;
        case EV_JOINED:
            Serial.println(F("EV_JOINED"));
            {
              u4_t netid = 0;
              devaddr_t devaddr = 0;
              u1_t nwkKey[16];
              u1_t artKey[16];
              LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
              Serial.print("netid: ");
              Serial.println(netid, DEC);
              Serial.print("devaddr: ");
              Serial.println(devaddr, HEX);
              Serial.print("AppSKey: ");
              for (size_t i=0; i<sizeof(artKey); ++i) {
                if (i != 0)
                  Serial.print("-");
                printHex2(artKey[i]);
              }
              Serial.println("");
              Serial.print("NwkSKey: ");
              for (size_t i=0; i<sizeof(nwkKey); ++i) {
                      if (i != 0)
                              Serial.print("-");
                      printHex2(nwkKey[i]);
              }
              Serial.println();
              LMIC_setLinkCheckMode(0);
              os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
            }
            // Disable link check validation (automatically enabled
            // during join, but because slow data rates change max TX
	    // size, we don't use it in this example.
            LMIC_setLinkCheckMode(0);
            break;
        /*
        || This event is defined but not used in the code. No
        || point in wasting codespace on it.
        ||
        || case EV_RFU1:
        ||     Serial.println(F("EV_RFU1"));
        ||     break;
        */
        case EV_JOIN_FAILED:
            Serial.println(F("EV_JOIN_FAILED"));
            break;
        case EV_REJOIN_FAILED:
            Serial.println(F("EV_REJOIN_FAILED"));
            break;
            break;
        case EV_TXCOMPLETE:
            Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            if (LMIC.txrxFlags & TXRX_ACK)
              Serial.println(F("Received ack"));
            if (LMIC.dataLen) {
              Serial.println(F("Received "));
              Serial.println(LMIC.dataLen);
              Serial.println(F(" bytes of payload"));
            }
            // Schedule next transmission
            os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
            break;
        case EV_LOST_TSYNC:
            Serial.println(F("EV_LOST_TSYNC"));
            break;
        case EV_RESET:
            Serial.println(F("EV_RESET"));
            break;
        case EV_RXCOMPLETE:
            // data received in ping slot
            Serial.println(F("EV_RXCOMPLETE"));
            break;
        case EV_LINK_DEAD:
            Serial.println(F("EV_LINK_DEAD"));
            break;
        case EV_LINK_ALIVE:
            Serial.println(F("EV_LINK_ALIVE"));
            break;
        case EV_TXSTART:
            Serial.println(F("EV_TXSTART"));
            break;
        case EV_TXCANCELED:
            Serial.println(F("EV_TXCANCELED"));
            break;
        case EV_RXSTART:
            /* do not print anything -- it wrecks timing */
            break;
        case EV_JOIN_TXCOMPLETE:
            Serial.println(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
            break;

        default:
            Serial.print(F("Unknown event: "));
            Serial.println((unsigned) ev);
            break;
    }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println(F("** Feather RP2040 LoRa File TX **"));

  randomSeed(micros());

  // — init flash + FAT —
  SPI.begin();
  flash.begin();
  while (!sd.begin(&flash)) {
    Serial.println(F("Flash FAT init failed!"));
    delay(1000);
  }
  Serial.println(F("Flash FAT OK"));

  // — generate CSV —
  makeCSV();
  
  // — LMIC init & OTAA join —
  os_init();
    // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();
  LMIC_setLinkCheckMode(0);
  LMIC_setDrTxpow(DR_SF7,14);
  LMIC_selectSubBand(1);
  do_send(&sendjob);  // start join
  //os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_join);
}

void loop() {
  os_runloop_once();
}