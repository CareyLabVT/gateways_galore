#include <Arduino.h>
#include <SPI.h>
#include <lmic.h>
#include <hal/hal.h>

// — OTAA keys (replace with your own) —
static const u1_t PROGMEM APPEUI[8]= { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
static const u1_t PROGMEM DEVEUI[8]= { 0x1f,0x39,0xaf,0xdd,0x20,0xd0,0xe5,0xea };
static const u1_t PROGMEM APPKEY[16]= {
  0xe4,0x5f,0x09,0x10,0x41,0x56,0x2a,0xae,
  0x09,0x4f,0x75,0xd4,0xf4,0x45,0x3a,0xe9
};
void os_getArtEui(u1_t* buf) { memcpy_P(buf, APPEUI, 8); }
void os_getDevEui(u1_t* buf) { memcpy_P(buf, DEVEUI, 8); }
void os_getDevKey(u1_t* buf) { memcpy_P(buf, APPKEY, 16); }

// — Pin mapping for Feather RP2040 + RFM95 —
const lmic_pinmap lmic_pins = {
  .nss   = 16,
  .rxtx  = LMIC_UNUSED_PIN,
  .rst   = 17,
  .dio   = { 21, 22, 23 },
};

static osjob_t sendjob;

void onEvent(ev_t ev) {
  Serial.print(os_getTime());
  Serial.print(F(": "));
  switch(ev) {
    case EV_SCAN_TIMEOUT:   Serial.println(F("EV_SCAN_TIMEOUT"));   break;
    case EV_BEACON_FOUND:   Serial.println(F("EV_BEACON_FOUND"));   break;
    case EV_BEACON_MISSED:  Serial.println(F("EV_BEACON_MISSED"));  break;
    case EV_BEACON_TRACKED: Serial.println(F("EV_BEACON_TRACKED")); break;
    case EV_JOINING:        Serial.println(F("EV_JOINING"));        break;
    case EV_JOINED:
      Serial.println(F("EV_JOINED — sending confirmed \"Hi\""));
      // Disable link checks, send one confirmed uplink on fPort=1:
      LMIC_setLinkCheckMode(0);
      LMIC_setDrTxpow(DR_SF7,14);
      // payload “Hi”
      {
        static const char msg[] = "Hello world!";
        LMIC_setTxData2(1, (unsigned char*)msg, sizeof(msg)-1,  /*confirmed*/1);
      }
      break;
    case EV_JOIN_FAILED:    Serial.println(F("EV_JOIN_FAILED"));    break;
    case EV_REJOIN_FAILED:  Serial.println(F("EV_REJOIN_FAILED"));  break;
    case EV_TXSTART:        Serial.println(F("EV_TXSTART"));        break;
    case EV_TXCOMPLETE:
      Serial.println(F("EV_TXCOMPLETE"));
      if (LMIC.txrxFlags & TXRX_ACK) {
        Serial.println(F("  → MAC‐ACK received!"));
      } else {
        Serial.println(F("  → No MAC‐ACK"));
      }
      break;
    case EV_RXCOMPLETE:     Serial.println(F("EV_RXCOMPLETE (downlink)")); break;
    default:
      Serial.print(F("EVENT "));
      Serial.println((unsigned)ev);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("Starting minimal confirmed‐uplink test"));

  SPI.begin();
  os_init();
  LMIC_reset();

  // set up RX windows and DR
  LMIC_setClockError(MAX_CLOCK_ERROR * 5 / 100);
  LMIC_setLinkCheckMode(0);
  LMIC_setAdrMode(0);

  // start OTAA join
  LMIC_startJoining();
}

void loop() {
  os_runloop_once();
}