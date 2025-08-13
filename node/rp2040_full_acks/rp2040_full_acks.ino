// #include <Arduino.h>
// #include <SPI.h>
// #include <lmic.h>
// #include <hal/hal.h>

// // — FLASH & FAT libraries —
// #include <Adafruit_SPIFlash.h>        // for external QSPI flash
// #include "flash_config.h"             // defines flashTransport
// #include <SdFat.h>                    // Adafruit fork with File32

// // — JSON for row counter —
// #include <ArduinoJson.h>

// // — Hashing —
// #include <CRC32.h>

// // — OTAA keys (replace FILLMEIN) —
// static const u1_t PROGMEM APPEUI[8]= { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
// static const u1_t PROGMEM DEVEUI[8]= { 0x1f,0x39,0xaf,0xdd,0x20,0xd0,0xe5,0xea };
// static const u1_t PROGMEM APPKEY[16]= {
//   0xe4,0x5f,0x09,0x10,0x41,0x56,0x2a,0xae,
//   0x09,0x4f,0x75,0xd4,0xf4,0x45,0x3a,0xe9
// };
// void os_getArtEui(u1_t* buf) { memcpy_P(buf, APPEUI, 8); }
// void os_getDevEui(u1_t* buf) { memcpy_P(buf, DEVEUI, 8); }
// void os_getDevKey(u1_t* buf) { memcpy_P(buf, APPKEY, 16); }

// // — Timing —
// const unsigned TX_INTERVAL = 10;  // seconds between confirmed sends

// // — LoRa I/O pins —
// const lmic_pinmap lmic_pins = { .nss=16, .rxtx=LMIC_UNUSED_PIN, .rst=17, .dio={21,22,23} };

// // — Filenames —
// static const char* JSON_ROW = "/row.json";
// static const char* CSV_FILE = "/data.csv";

// // — Globals for FAT —
// Adafruit_SPIFlash flash(&flashTransport);
// FatVolume sd;

// // — Send job & buffer state —
// static osjob_t sendjob;
// static uint8_t* sendBuf   = nullptr;
// static size_t   sendSize  = 0;
// static size_t   sendOffset= 0;
// static size_t   ackOffset = 0;
// static uint32_t fileHash  = 0;
// static bool     sendingFile = false;
// static bool     sendingHash = false;

// // — JSON helpers —
// uint32_t retrieveRowNum() {
//   if (!sd.exists(JSON_ROW)) return 0;
//   File32 jf = sd.open(JSON_ROW, FILE_READ);
//   StaticJsonDocument<128> doc;
//   if (deserializeJson(doc, jf) != DeserializationError::Ok) {
//     jf.close(); return 0;
//   }
//   jf.close();
//   return doc["row_num"] | 0U;
// }
// void storeRowNum(uint32_t row) {
//   File32 jf = sd.open(JSON_ROW, FILE_WRITE);
//   StaticJsonDocument<128> doc;
//   doc["row_num"] = row;
//   serializeJson(doc, jf);
//   jf.close();
// }

// // — CSV generation —
// void makeCSV() {
//   uint32_t startRow = retrieveRowNum();
//   uint16_t n = random(1000, 10000);
//   File32 f = sd.open(CSV_FILE, FILE_WRITE);
//   f.println("ROW_NUM,TEMP");
//   for (uint16_t i = 0; i < n; i++) {
//     float temp = random(2000,3500)/100.0;
//     f.print(startRow + i);
//     f.print(',');
//     f.println(temp,2);
//   }
//   f.close();
//   storeRowNum(startRow + n);
// }

// void printCSV() {
//   File32 r = sd.open(CSV_FILE, FILE_READ);
//   if (!r) {
//     Serial.println(F("Failed to open CSV for printing"));
//     return;
//   }
//   Serial.println(F("=== CSV CONTENTS ==="));
//   while (r.available()) {
//     String line = r.readStringUntil('\n');
//     Serial.println(line);
//   }
//   Serial.println(F("===================="));
//   r.close();
// }

// // — CRC32 —
// uint32_t computeCRC(const char* path) {
//   File32 f = sd.open(path, FILE_READ);
//   CRC32 crc;
//   while (f.available()) crc.update(f.read());
//   f.close();
//   return crc.finalize();
// }

// // — Prepare send buffer —
// void prepareSend() {
//   File32 f = sd.open(CSV_FILE, FILE_READ);
//   sendSize = f.size();
//   sendBuf = (uint8_t*)malloc(sendSize);
//   f.read(sendBuf, sendSize);
//   f.close();
//   fileHash = computeCRC(CSV_FILE);
//   sendOffset = 0;
//   ackOffset  = 0;
//   sendingFile = true;
//   sendingHash = false;
// }

// // — Trigger OTAA —
// void do_join(osjob_t* j) {
//   if (LMIC.opmode & OP_TXRXPEND) return;
//   LMIC_setTxData2(0, nullptr, 0, 0);
//   Serial.println(F("Join requested"));
// }

// // — Send next chunk or hash (confirmed uplink) —
// #define LORA_MAX_PAYLOAD 51
// // Called whenever LMIC needs you to transmit next frame:
// void do_send(osjob_t* j) {
//   // If a TX is still pending in the radio, wait a bit and retry:
//   if (LMIC.opmode & OP_TXRXPEND) {
//     os_setTimedCallback(&sendjob,
//       os_getTime() + sec2osticks(1), do_send);
//     return;
//   }

//   // Always resume from last ACK’d offset:
//   sendOffset = ackOffset;

//   // Phase 1: send the next CSV chunk
//   if (sendingFile) {
//     size_t chunk = min((size_t)LORA_MAX_PAYLOAD, sendSize - sendOffset);
//     Serial.printf("TX chunk @%u len=%u\n", (unsigned)sendOffset, (unsigned)chunk);
//     // confirmed=1 to get a MAC‐level ACK
//     LMIC_setTxData2(1, sendBuf + sendOffset, chunk, 1);
//     // Note: we do *not* advance ackOffset here—only on EV_TXCOMPLETE
//   }
//   // Phase 2: send the 4-byte CRC
//   else if (sendingHash) {
//     uint8_t hb[4] = {
//       (uint8_t)(fileHash >> 24),
//       (uint8_t)(fileHash >> 16),
//       (uint8_t)(fileHash >>  8),
//       (uint8_t)(fileHash      )
//     };
//     Serial.println("TX CRC hash");
//     LMIC_setTxData2(1, hb, sizeof(hb), 1);
//   }
//   // No self-scheduling here: we wait for EV_TXCOMPLETE
// }

// // LMIC event handler—wired up exactly once in setup():
// void onEvent(ev_t ev) {
//   Serial.print(os_getTime()); Serial.print(": ");

//   switch(ev) {
//     case EV_JOINING:
//       Serial.println(F("EV_JOINING"));
//       break;

//     case EV_JOINED:
//       Serial.println(F("EV_JOINED"));
//       LMIC_setLinkCheckMode(0);
//       // Load CSV into RAM and compute hash:
//       prepareSend();
//       ackOffset = 0;              // start at zero
//       // Kick off first send immediately:
//       do_send(&sendjob);
//       break;

//     case EV_TXCOMPLETE:
//       Serial.println(F("EV_TXCOMPLETE"));
//       // If the frame we just sent was confirmed and got a MAC ACK...
//       if (LMIC.txrxFlags & TXRX_ACK) {
//         // advance ackOffset by the size of that chunk:
//         // (chunk size = sendOffset - ackOffset before TX)
//         size_t lastLen = LMIC.dataLen; // LMIC.dataLen holds what we just sent
//         ackOffset += lastLen;
//         Serial.printf("MAC ACK → ackOffset=%u\n", (unsigned)ackOffset);
//         // If we’ve sent the entire file, move to hash phase next:
//         if (sendingFile && ackOffset >= sendSize) {
//           sendingFile = false;
//           sendingHash = true;
//         }
//       } else {
//         Serial.println(F("No MAC ACK, will retry chunk"));
//       }
//       // Now schedule the next send (chunk or hash) in 1s:
//       if (sendingFile || sendingHash) {
//         os_setTimedCallback(&sendjob,
//           os_getTime() + sec2osticks(1),
//           do_send);
//       } else {
//         // We’re done: free RAM and delete CSV if you like:
//         free(sendBuf);
//         if (sd.exists(CSV_FILE)) {
//           sd.remove(CSV_FILE);
//           Serial.println(F("CSV deleted"));
//         }
//         Serial.println(F("ALL DONE"));
//       }
//       break;

//     case EV_RXCOMPLETE:
//       // gateway downlink arrived (your cumulative ACK override)
//       if (LMIC.dataLen >= 4) {
//         uint32_t dlAck = 0;
//         // extract 4‐byte big-endian offset
//         for (int i = 0; i < 4; i++) {
//           dlAck = (dlAck << 8) | LMIC.frame[LMIC.dataBeg + i];
//         }
//         ackOffset = dlAck;
//         Serial.printf("DL OVERRIDE → ackOffset=%u\n", (unsigned)ackOffset);
//       }
//       break;

//     default:
//       break;
//   }
// }
// // void do_send(osjob_t* j) {
// //   if (LMIC.opmode & OP_TXRXPEND) {
// //     // retry shortly
// //     os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
// //     return;
// //   }
// //   // Phase 1: file chunks
// //   if (sendingFile) {
// //     sendOffset = ackOffset;  // resume from last ack
// //     size_t chunk = min((size_t)LORA_MAX_PAYLOAD, sendSize - sendOffset);
// //     LMIC_setTxData2(1, sendBuf + sendOffset, chunk, 1);
// //     Serial.printf("TX chunk @%u len=%u\n", (unsigned)sendOffset, (unsigned)chunk);
// //   }
// //   // Phase 2: final CRC
// //   else if (sendingHash) {
// //     uint8_t hb[4] = {
// //       (uint8_t)(fileHash>>24), (uint8_t)(fileHash>>16),
// //       (uint8_t)(fileHash>> 8), (uint8_t)(fileHash    )
// //     };
// //     LMIC_setTxData2(1, hb, sizeof(hb), 1);
// //     Serial.println(F("TX hash"));
// //   }
// // }

// // // — LMIC event handler —
// // void onEvent(ev_t ev) {
// //   Serial.print(os_getTime()); Serial.print(F(": "));
// //   switch(ev) {
// //     case EV_JOINING:  Serial.println(F("EV_JOINING")); break;
// //     case EV_JOIN_FAILED: Serial.println(F("EV_JOIN_FAILED")); break;
// //     case EV_JOINED:
// //       Serial.println(F("EV_JOINED"));
// //       LMIC_setLinkCheckMode(0);
// //       prepareSend();
// //       os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
// //       break;
// //     case EV_TXCOMPLETE:
// //       Serial.println(F("EV_TXCOMPLETE"));
// //       if (LMIC.txrxFlags & TXRX_ACK) {
// //         // host ACKs this chunk
// //         ackOffset = sendOffset + LMIC.dataLen;  // sendOffset updated before
// //         Serial.printf("ACKed up to %u\n", (unsigned)ackOffset);
// //         // advance to next phase
// //         if (sendingFile && ackOffset >= sendSize) {
// //           sendingFile = false;
// //           sendingHash = true;
// //         }
// //       } else {
// //         Serial.println(F("No MAC ACK, retrying"));
// //       }
// //       // schedule next
// //       if (sendingFile || sendingHash) {
// //         os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
// //       } else {
// //         // done: delete CSV
// //         free(sendBuf);
// //         if (sd.exists(CSV_FILE)) sd.remove(CSV_FILE);
// //         Serial.println(F("ALL DONE, CSV removed"));
// //       }
// //       break;
// //     case EV_RXCOMPLETE:
// //       // downlink payload (optional cumulative override)
// //       if (LMIC.dataLen > 0) {
// //         uint32_t o=0;
// //         for (int i=0;i<LMIC.dataLen && i<4;i++) {
// //           o = (o<<8) | LMIC.frame[LMIC.dataBeg + i];
// //         }
// //         ackOffset = o;
// //         Serial.printf("DL ACK offset=%u\n", (unsigned)ackOffset);
// //       }
// //       break;
// //     default:
// //       break;
// //   }
// // }

// void setup() {
//   Serial.begin(115200);
//   while (!Serial) delay(10);
//   Serial.println(F("** Feather RP2040 Reliable File TX **"));
//   randomSeed(micros());
//   SPI.begin(); flash.begin();
//   while(!sd.begin(&flash)) { Serial.println(F("Flash FAT init failed")); delay(1000);}  
//   Serial.println(F("Flash FAT OK"));
//   makeCSV();
//   printCSV();
//   os_init(); LMIC_reset();
//   LMIC_setLinkCheckMode(0); LMIC_setDrTxpow(DR_SF7,14); LMIC_selectSubBand(1);
//   do_join(&sendjob);
// }

// void loop() {
//   os_runloop_once();
// }

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

// — OTAA keys —
static const u1_t PROGMEM APPEUI[8] = {0};
static const u1_t PROGMEM DEVEUI[8] = {0x1f,0x39,0xaf,0xdd,0x20,0xd0,0xe5,0xea};
static const u1_t PROGMEM APPKEY[16] = {0xe4,0x5f,0x09,0x10,0x41,0x56,0x2a,0xae,0x09,0x4f,0x75,0xd4,0xf4,0x45,0x3a,0xe9};
void os_getArtEui(u1_t* buf) { memcpy_P(buf, APPEUI, 8); }
void os_getDevEui(u1_t* buf) { memcpy_P(buf, DEVEUI, 8); }
void os_getDevKey(u1_t* buf) { memcpy_P(buf, APPKEY, 16); }

// — Timing —
const unsigned TX_INTERVAL = 15;

// — LoRa pins —
const lmic_pinmap lmic_pins = {16, LMIC_UNUSED_PIN, 17, {21,22,23}};

// — Files —
static const char* JSON_ROW = "/row.json";
static const char* CSV_FILE = "/data.csv";

// — Globals —
Adafruit_SPIFlash flash(&flashTransport);
FatVolume sd;

static osjob_t sendjob;
static uint8_t* sendBuf;
static size_t sendSize, sendOffset, ackOffset;
static uint32_t fileHash;
static bool sendingFile, sendingHash;

// Retrieve/store row
uint32_t retrieveRowNum() {
  if (!sd.exists(JSON_ROW)) return 0;
  File32 f = sd.open(JSON_ROW, FILE_READ);
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc,f)!=DeserializationError::Ok) { f.close(); return 0; }
  f.close();
  return doc["row_num"] | 0;
}
void storeRowNum(uint32_t row) {
  File32 f = sd.open(JSON_ROW, FILE_WRITE);
  StaticJsonDocument<128> doc;
  doc["row_num"] = row;
  serializeJson(doc,f);
  f.close();
}

// Generate CSV
void makeCSV() {
  uint32_t start = retrieveRowNum();
  uint16_t n = random(1000,10000);
  File32 f = sd.open(CSV_FILE, FILE_WRITE);
  f.println("ROW_NUM,TEMP");
  for (uint16_t i=0;i<n;i++) {
    float t = random(2000,3500)/100.0;
    f.print(start+i);
    f.print(','); f.println(t,2);
  }
  f.close();
  storeRowNum(start+n);
}

// Print CSV
void printCSV() {
  File32 f=sd.open(CSV_FILE,FILE_READ);
  if(!f){Serial.println("No CSV");return;}
  Serial.println("=== CSV ===");
  while(f.available()) Serial.println(f.readStringUntil('\n'));
  Serial.println("==========");
  f.close();
}

// CRC32
uint32_t computeCRC(const char* path) {
  File32 f=sd.open(path,FILE_READ);
  CRC32 crc;
  while(f.available()) crc.update(f.read());
  f.close();
  return crc.finalize();
}

// Buffer prep
void prepareSend() {
  File32 f=sd.open(CSV_FILE,FILE_READ);
  sendSize=f.size();
  sendBuf=(uint8_t*)malloc(sendSize);
  f.read(sendBuf,sendSize); f.close();
  fileHash = computeCRC(CSV_FILE);
  ackOffset=0; sendingFile=true; sendingHash=false;
}

// Trigger OTAA
void do_join(osjob_t* j) {
  if(LMIC.opmode&OP_TXRXPEND) return;
  LMIC_setTxData2(0,nullptr,0,0);
  Serial.println("Join requested");
}

#define LORA_MAX_PAYLOAD 51
// Send chunk or hash
void do_send(osjob_t* j) {
  Serial.println("do_send triggered");
  if(LMIC.opmode&OP_TXRXPEND) {
    Serial.println("Radio busy, retry");
    os_setTimedCallback(&sendjob,os_getTime()+sec2osticks(TX_INTERVAL),do_send);
    return;
  }
  sendOffset=ackOffset;
  if(sendingFile) {
    size_t c=min((size_t)LORA_MAX_PAYLOAD,sendSize-sendOffset);
    Serial.printf("Sending @%u len=%u\n",(unsigned)sendOffset,(unsigned)c);
    LMIC_setTxData2(1,sendBuf+sendOffset,c,1);
  } else if(sendingHash) {
    uint8_t hb[4]={ (uint8_t)(fileHash>>24),(uint8_t)(fileHash>>16),(uint8_t)(fileHash>>8),(uint8_t)fileHash };
    Serial.println("Sending CRC");
    LMIC_setTxData2(1,hb,4,1);
  }
}

void onEvent(ev_t ev) {
  Serial.print(os_getTime());Serial.print(": ");
  switch(ev) {
    case EV_JOINING: Serial.println("EV_JOINING"); break;
    case EV_JOINED:
      Serial.println("EV_JOINED"); LMIC_setLinkCheckMode(0);
      prepareSend(); do_send(&sendjob);
      break;
    case EV_TXCOMPLETE:
      Serial.println("EV_TXCOMPLETE");
      if(LMIC.txrxFlags&TXRX_ACK) {
        size_t sent=LMIC.dataLen; ackOffset+=sent;
        Serial.printf("ACK %u\n",(unsigned)ackOffset);
        if(sendingFile&&ackOffset>=sendSize){sendingFile=false; sendingHash=true;}
      } else Serial.println("No MAC ACK");
      if(sendingFile||sendingHash)
        os_setTimedCallback(&sendjob,os_getTime()+sec2osticks(TX_INTERVAL),do_send);
      else {
        free(sendBuf); sd.remove(CSV_FILE); Serial.println("Done");
      }
      break;
    case EV_RXCOMPLETE:
      Serial.println("EV_RXCOMPLETE");
      if(LMIC.dataLen>=4) {
        uint32_t dl=0; for(int i=0;i<4;i++) dl=(dl<<8)|LMIC.frame[LMIC.dataBeg+i];
        ackOffset=dl; Serial.printf("DL ACK %u\n",(unsigned)ackOffset);
      }
      break;
    default: break;
  }
}

void setup() {
  Serial.begin(115200); while(!Serial);
  randomSeed(micros()); SPI.begin(); flash.begin();
  while(!sd.begin(&flash)){Serial.println("FLASH fail");delay(1000);}  
  Serial.println("FLASH OK");
  makeCSV(); printCSV();
  os_init(); LMIC_reset(); LMIC_setLinkCheckMode(0);
  LMIC_setDrTxpow(DR_SF7,14); LMIC_selectSubBand(1);
  do_join(&sendjob);
}
void loop(){ os_runloop_once(); }
