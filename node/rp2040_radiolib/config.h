#ifndef _RADIOLIB_EX_LORAWAN_CONFIG_H
#define _RADIOLIB_EX_LORAWAN_CONFIG_H

#include <RadioLib.h>

// — SX1276 on Adafruit Feather RP2040 —
//    CS = GPIO16, DIO0 = GPIO21, DIO1 = GPIO22, DIO2 = GPIO23
SX1276 radio = new Module(16, 21, 22, 23);

// — LoRaWAN region & sub-band —  
const LoRaWANBand_t Region = US915;
const uint8_t        subBand = 2;

// — OTAA credentials —  
#define RADIOLIB_LORAWAN_JOIN_EUI  0x0000000000000000ULL
#define RADIOLIB_LORAWAN_DEV_EUI   0xEAE5D020DDAF391FULL
#define RADIOLIB_LORAWAN_APP_KEY   \
  0xE4,0x5F,0x09,0x10,0x41,0x56,0x2A,0xAE, \
  0x09,0x4F,0x75,0xD4,0xF4,0x45,0x3A,0xE9
#define RADIOLIB_LORAWAN_NWK_KEY   \
  0xE4,0x5F,0x09,0x10,0x41,0x56,0x2A,0xAE, \
  0x09,0x4F,0x75,0xD4,0xF4,0x45,0x3A,0xE9

uint64_t joinEUI = RADIOLIB_LORAWAN_JOIN_EUI;
uint64_t devEUI  = RADIOLIB_LORAWAN_DEV_EUI;
uint8_t  appKey[] = { RADIOLIB_LORAWAN_APP_KEY };
uint8_t  nwkKey[] = { RADIOLIB_LORAWAN_NWK_KEY };

// — the LoRaWAN node object —  
LoRaWANNode node(&radio, &Region, subBand);

// — helper for error codes —  
String stateDecode(int16_t st) {
  switch (st) {
    case RADIOLIB_ERR_NONE:              return "ERR_NONE";
    case RADIOLIB_LORAWAN_NEW_SESSION:   return "NEW_SESSION";
    case RADIOLIB_ERR_PACKET_TOO_LONG:   return "TOO_LONG";
    case RADIOLIB_ERR_NO_RX_WINDOW:      return "NO_RX_WINDOW";
    case RADIOLIB_ERR_MIC_MISMATCH:      return "MIC_MISMATCH";
    case RADIOLIB_ERR_NETWORK_NOT_JOINED:return "NOT_JOINED";
    // …add more if you like…
  }
  return String(st);
}
void debug(bool fail, const __FlashStringHelper* msg, int16_t code, bool halt=true) {
  if (!fail) return;
  Serial.print(msg);
  Serial.print(F(" → "));
  Serial.println(stateDecode(code));
  if (halt) while(1) yield();
}

#endif // _RADIOLIB_EX_LORAWAN_CONFIG_H
