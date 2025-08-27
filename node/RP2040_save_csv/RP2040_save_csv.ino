// === CONFIG YOU MAY TWEAK ===
static const uint16_t  CSV_ROWS_MIN   = 1000;   // min rows per cycle
static const uint16_t CSV_ROWS_SPAN  = 4001;   // random(4001) -> 0..4000, +1000 => 1000..5000
static const uint8_t  CHUNK_SIZE     = 200;    // target fragment size (shrinks if needed)
static const uint32_t SEND_GAP_MS    = 200;    // pacing between uplinks
static const uint32_t SLEEP_MS       = 60000;  // "sleep" between cycles (blocking delay)

// LoRaWAN app-ports
static const uint8_t  PORT_CTRL_UP   = 70;   // START, END
static const uint8_t  PORT_DATA_UP   = 71;   // FRAG
static const uint8_t  PORT_CTRL_DN   = 72;   // ACKs, RESULT

static const uint8_t  RESULT_MAX_TICKS = 12;     // cap ticks while waiting RESULT
static const uint32_t RESULT_WAIT_MS   = 30000;  // time cap while waiting RESULT

static uint8_t  resultTickCount = 0;
static uint32_t resultStartMs   = 0;

// Message types
enum Msg : uint8_t {
  MSG_START      = 0x01,
  MSG_FRAG       = 0x02,
  MSG_END        = 0x03,
  MSG_ACK_START  = 0x81,
  MSG_ACK_FRAG   = 0x82,
  MSG_RESULT     = 0x83
};

// --- forward decls (Arduino auto-prototype guard) ---
static bool fsBegin();
static bool loadBlob(const char* path, uint8_t* dst, size_t len);
static bool saveBlob(const char* path, const uint8_t* src, size_t len);
static int16_t lwActivate();  // nonces-only, fresh join each boot
// ----------------------------------------------------

#include <RadioLib.h>
#include <Adafruit_SPIFlash.h>
#include <SdFat.h>

#include "flash_config.h"
#include "config.h"

// Pin to clear nonces on reset (hold to GND during reset)
#ifndef CLEAR_PIN
  #define CLEAR_PIN A2          // Feather RP2040: A2 = GPIO28
#endif
#define CLEAR_ACTIVE   LOW
#define CLEAR_HOLD_MS  500

// Files (no leading slash for SdFat)
static const char* PATH_NONCES  = "lw_nonces.bin";
static const char* PATH_LASTROW = "last_row.bin";   // NEW: persisted last_transmitted_row

// Flash + FAT
Adafruit_SPIFlash flash(&flashTransport);
FatFileSystem     fatfs;

// ======== CSV buffer ========
// Big enough to hold up to ~5000 rows. Each row ~ up to ~20B (index + commas + CRLF + 2x 0..999).
// 5000*20 = 100k. Give margin.
#define CSV_MAX_BYTES 120000
static uint8_t CSV_BUF[CSV_MAX_BYTES];
static size_t  CSV_LEN = 0;

// ======== transfer state (pipeline) ========
enum Phase : uint8_t { PH_JOINING, PH_INIT, PH_SEND_START, PH_SEND_FRAGS, PH_SEND_END, PH_WAIT_RESULT, PH_DONE };
static Phase    phase = PH_INIT;
static uint16_t sessId = 0;
static int32_t  lastAckedSeq = -1;  // last fragment index ACKed (-1 before any ACK)

// ======== NEW: source-of-truth row counters ========
static uint32_t last_transmitted_row = 0;  // persisted after success (RESULT: OK)
static uint32_t sessionStartRow      = 0;  // this cycle's first row index
static uint32_t sessionRowsPlanned   = 0;  // requested rows for this cycle
static uint32_t sessionRowsBuilt     = 0;  // actually built into CSV_BUF (should match planned)

// ======== small helpers ========
static inline void put16le(uint8_t* p, uint16_t v){ p[0]=uint8_t(v); p[1]=uint8_t(v>>8); }

// CRC32 (poly 0xEDB88320) — kept in case you still want to send it later
static uint32_t crc32(const uint8_t* d, size_t n){
  uint32_t c = 0xFFFFFFFFul;
  for(size_t i=0;i<n;i++){
    c ^= d[i];
    for(uint8_t b=0;b<8;b++) c = (c & 1) ? (c >> 1) ^ 0xEDB88320ul : (c >> 1);
  }
  return ~c;
}

static void factoryClearIfRequested() {
  pinMode(CLEAR_PIN, INPUT_PULLUP);
  unsigned long t0 = millis();
  if (digitalRead(CLEAR_PIN) != CLEAR_ACTIVE) return;
  while (digitalRead(CLEAR_PIN) == CLEAR_ACTIVE && (millis() - t0) < CLEAR_HOLD_MS) delay(10);
  if ((millis() - t0) < CLEAR_HOLD_MS) return;

  Serial.println(F("Factory clear requested: deleting persisted LoRaWAN files..."));
  if (fsBegin()) {
    if (fatfs.remove(PATH_NONCES))  Serial.println(F("  removed lw_nonces.bin"));
    else                            Serial.println(F("  lw_nonces.bin not present"));
    if (fatfs.remove(PATH_LASTROW)) Serial.println(F("  removed last_row.bin"));
    else                            Serial.println(F("  last_row.bin not present"));
  }
  while (digitalRead(CLEAR_PIN) == CLEAR_ACTIVE) delay(10);
}

// Build CSV for rows [startRow, startRow+rowsWanted)
// Returns rows actually built in outRowsBuilt (should be = rowsWanted unless size cap hit)
static void buildCSVRange(uint32_t startRow, uint32_t rowsWanted, uint32_t &outRowsBuilt){
  String s;
  size_t reserveHint = min((size_t)rowsWanted * (size_t)20, (size_t)CSV_MAX_BYTES);
  s.reserve(reserveHint);

  outRowsBuilt = 0;
  for(uint32_t r=0; r<rowsWanted; r++){
    uint32_t idx = startRow + r;
    uint16_t a = radio.random(1000);
    uint16_t b = radio.random(1000);
    s += String(idx); s += ','; s += String(a); s += ','; s += String(b); s += "\r\n";

    if (s.length() >= (CSV_MAX_BYTES - 32)) {  // leave a little headroom
      break;
    }
    outRowsBuilt++;

    if (r % 50) {
    Serial.printf("created %d of %d\n", (int)r, (int)rowsWanted);
    }
  }
  CSV_LEN = min((size_t)s.length(), (size_t)CSV_MAX_BYTES);
  memcpy(CSV_BUF, s.c_str(), CSV_LEN);
}

// ---- headroom helpers to avoid ERR_PACKET_TOO_LONG ----
static void logHeadroom(const char* tag, size_t need) {
  uint8_t maxp = node.getMaxPayloadLen();
  Serial.print(tag);
  Serial.print(F(": need=")); Serial.print(need);
  Serial.print(F(" max="));  Serial.println(maxp);
}

// send an empty UL (to carry MAC answers); return true if it went out
static bool tickEmptyOnce() {
  LoRaWANEvent_t u{}, d{};
  size_t dnLen = 0;
  int16_t win = node.sendReceive(nullptr, 0, 0, nullptr, &dnLen, false, &u, &d);
  return (win >= 0);
}

// try to open room for a header of size 'needHdr' (e.g., 3 or 6 bytes)
static bool ensureHeaderRoom(size_t needHdr) {
  if (node.getMaxPayloadLen() >= needHdr) return true;
  if (tickEmptyOnce() && node.getMaxPayloadLen() >= needHdr) return true;

  node.setADR(false);
  const uint8_t tryDR[] = { 3, 4 };
  for (uint8_t dr : tryDR) {
    (void)node.setDatarate(dr);
    if (tickEmptyOnce() && node.getMaxPayloadLen() >= needHdr) { node.setADR(true); return true; }
    delay(200);
  }
  node.setADR(true);
  return (node.getMaxPayloadLen() >= needHdr);
}

// One uplink + wait for downlink (Rx1/Rx2)
// Returns downlink length (>=0) or negative RadioLib error.
// Fills dnPort with downlink FPort and dnBuf/dnLen with bytes (dnLen may be 0).
static int16_t txrx(uint8_t fport, const uint8_t* up, size_t upLen,
                    uint8_t* dnBuf, size_t& dnLen, uint8_t& dnPort,
                    LoRaWANEvent_t* evUp=nullptr, LoRaWANEvent_t* evDn=nullptr) {
  dnLen = (dnBuf ? dnLen : 0);
  LoRaWANEvent_t locUp{}, locDn{};
  if(!evUp) evUp = &locUp;
  if(!evDn) evDn = &locDn;

  int16_t win = node.sendReceive(up, upLen, fport, dnBuf, &dnLen,
                                 /*confirmed=*/false, evUp, evDn);
  if (win == RADIOLIB_ERR_PACKET_TOO_LONG) {
    Serial.println(F("Packet too long; empty tick & retry..."));
    (void)tickEmptyOnce();
    dnLen = (dnBuf ? dnLen : 0);
    win = node.sendReceive(up, upLen, fport, dnBuf, &dnLen, false, evUp, evDn);
  }
  dnPort = (uint8_t)evDn->fPort;
  return (win < RADIOLIB_ERR_NONE) ? win : (int16_t)dnLen;
}

// Parse any downlink we got this uplink (ACKs correspond to previous step)
static void processDownlink(uint8_t dnPort, const uint8_t* dn, size_t dnLen) {
  if (dnPort != PORT_CTRL_DN || dnLen < 1) return;

  if (dn[0] == MSG_ACK_START && dnLen >= 3) {
    uint16_t rs = dn[1] | (uint16_t(dn[2]) << 8);
    if (rs == sessId) Serial.println(F("ACK_START received."));
  } else if (dn[0] == MSG_ACK_FRAG && dnLen >= 5) {
    uint16_t rs = dn[1] | (uint16_t(dn[2]) << 8);
    uint16_t aseq = dn[3] | (uint16_t(dn[4]) << 8);
    if (rs == sessId && (int32_t)aseq > lastAckedSeq) {
      lastAckedSeq = (int32_t)aseq;
      Serial.print(F("ACK_FRAG seq=")); Serial.println(aseq);
    }
  } else if (dn[0] == MSG_RESULT && dnLen >= 4) {
    uint16_t rs = dn[1] | (uint16_t(dn[2]) << 8);
    uint8_t status = dn[3];
    if (rs == sessId) {
      Serial.print(F("RESULT: ")); Serial.println(status == 0 ? F("OK") : F("FAIL"));
      phase = PH_DONE;
      return;
    }
  }
}

// ======== FS helpers ========
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
  f.flush(); f.close();
  return wr == (int32_t)len;
}

// Nonces-only activation: persist DevNonce, always fresh-join
static int16_t lwActivate() {
  int16_t st = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  debug(st != RADIOLIB_ERR_NONE, F("LoRaWAN init failed"), st, true);
  { 
    uint8_t buf[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
    if (loadBlob(PATH_NONCES, buf, sizeof(buf))) {
      st = node.setBufferNonces(buf);
      if (st != RADIOLIB_ERR_NONE) Serial.println(F("Note: setBufferNonces not applied"));
    }
  }
  LoRaWANJoinEvent_t je{};
  st = node.activateOTAA(RADIOLIB_LORAWAN_DATA_RATE_UNUSED, &je);
  if (st == RADIOLIB_LORAWAN_NEW_SESSION) {
    uint8_t* n = node.getBufferNonces();
    saveBlob(PATH_NONCES, n, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    Serial.println(F("Joined with Nonces saved."));
    (void)tickEmptyOnce(); // optional: drain MAC right after join
    phase = PH_INIT;
    return st;
  }
  debug(true, F("activateOTAA failed"), st, true);
  phase = PH_JOINING;
  return st;
}

// Nonces-only activation with infinite retry/backoff.
// Returns RADIOLIB_LORAWAN_NEW_SESSION when it finally joins.
static int16_t lwActivateForever() {
  // Set credentials once
  int16_t st = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.print(F("LoRaWAN init failed: ")); Serial.println(st);
  }

  // Try to restore saved nonces (so DevNonce survives resets)
  {
    uint8_t buf[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
    if (loadBlob(PATH_NONCES, buf, sizeof(buf))) {
      st = node.setBufferNonces(buf);
      if (st != RADIOLIB_ERR_NONE) {
        Serial.print(F("setBufferNonces failed: ")); Serial.println(st);
      }
    }
  }

  // Retry activateOTAA every 30 seconds until success
  while (true) {
    LoRaWANJoinEvent_t je{};
    st = node.activateOTAA(RADIOLIB_LORAWAN_DATA_RATE_UNUSED, &je);

    if (st == RADIOLIB_LORAWAN_NEW_SESSION) {
      // Save nonces right away so reboots don’t require nonce flush
      uint8_t* n = node.getBufferNonces();
      if (!saveBlob(PATH_NONCES, n, RADIOLIB_LORAWAN_NONCES_BUF_SIZE)) {
        Serial.println(F("WARN: saving nonces failed"));
      }
      Serial.println(F("Joined with Nonces saved."));
      (void)tickEmptyOnce();   // small empty UL to drain any pending MAC
      return st;
    }

    // Not joined yet — wait 30s and try again
    Serial.print(F("Join failed: ")); Serial.println(st);
    Serial.println(F("Retry in 30 s"));
    uint32_t until = millis() + 30000UL;
    while ((int32_t)(millis() - until) < 0) {
      delay(250);              // short sleeps so USB/Serial stays responsive
    }
  }
}

// ==== START/FRAG/END helpers (unchanged behavior, just called from state machine) ====
static bool sendStart() {
  const size_t needHdr = 3;
  logHeadroom("START", needHdr);
  if (!ensureHeaderRoom(needHdr)) return false;

  uint8_t up[3];
  up[0] = MSG_START;
  put16le(&up[1], sessId);

  uint8_t dn[16]; size_t dnLen = sizeof(dn); uint8_t dnPort = 0;
  (void)txrx(PORT_CTRL_UP, up, sizeof(up), dn, dnLen, dnPort);
  processDownlink(dnPort, dn, dnLen);
  return true;
}

// FRAG: send next un-ACKed fragment
// returns: 1=all sent, 0=sent one, -1=no room (ticked)
static int8_t sendFrag() {
  const size_t hdr = 6;
  uint8_t maxp = node.getMaxPayloadLen();
  logHeadroom("FRAG", hdr);

  if (maxp < hdr) {
    if (!ensureHeaderRoom(hdr)) return -1;
    maxp = node.getMaxPayloadLen();
  }

  // strict stop-and-wait: next needed fragment
  uint16_t seq = (uint16_t)(lastAckedSeq + 1);
  size_t off   = (size_t)seq * CHUNK_SIZE;

  if (off >= CSV_LEN) return 1;  // nothing left

  uint8_t want = (uint8_t)min((size_t)CHUNK_SIZE, CSV_LEN - off);
  uint8_t room = (maxp > hdr) ? (uint8_t)(maxp - hdr) : 0;

  if (room == 0) {
    // open RX to drain MAC / receive ACKs
    uint8_t dn[64]; size_t dnLen = sizeof(dn); uint8_t dnPort = 0;
    (void)txrx(/*port*/0, /*payload*/nullptr, /*len*/0, dn, dnLen, dnPort);
    processDownlink(dnPort, dn, dnLen);
    return -1;
  }
  uint8_t len = (want <= room) ? want : room;

  uint8_t up[6 + CHUNK_SIZE];
  up[0] = MSG_FRAG;
  put16le(&up[1], sessId);
  put16le(&up[3], seq);
  up[5] = len;
  memcpy(&up[6], &CSV_BUF[off], len);

  uint8_t dn[16]; size_t dnLen = sizeof(dn); uint8_t dnPort = 0;
  (void)txrx(PORT_DATA_UP, up, (size_t)(6 + len), dn, dnLen, dnPort);
  processDownlink(dnPort, dn, dnLen);
  return 0;
}

// END: send [type=END][sessId]; if RESULT arrives in same RX it will set PH_DONE
static void sendEnd() {
  const size_t needHdr = 3;
  logHeadroom("END", needHdr);
  if (!ensureHeaderRoom(needHdr)) return;

  uint8_t up[3];
  up[0] = MSG_END;
  put16le(&up[1], sessId);

  uint8_t dn[16]; size_t dnLen = sizeof(dn); uint8_t dnPort = 0;
  int16_t got = txrx(PORT_CTRL_UP, up, sizeof(up), dn, dnLen, dnPort);
  processDownlink(dnPort, dn, dnLen);

  if (got >= 0 && phase != PH_DONE) {
    phase = PH_WAIT_RESULT;
    resultTickCount = 0;
    resultStartMs = millis();
  }
}

// WAIT_RESULT: tick to open RX; handle timeout → retry END
static void tickWaitResult() {
  uint8_t dn[64]; size_t dnLen = sizeof(dn); uint8_t dnPort = 0;
  (void)txrx(/*port*/0, /*payload*/nullptr, /*len*/0, dn, dnLen, dnPort);
  processDownlink(dnPort, dn, dnLen);

  if (phase == PH_DONE) return;

  if (++resultTickCount >= RESULT_MAX_TICKS || (millis() - resultStartMs) > RESULT_WAIT_MS) {
    Serial.println(F("RESULT timeout — retrying END."));
    phase = PH_SEND_END;
  }
}

// ======== setup & loop ========
static const char* phaseText(enum Phase phase) {
  switch (phase) {
    case PH_JOINING: return "PH_JOINING";
    case PH_INIT: return "PH_INIT";
    case PH_SEND_START: return "PH_SEND_START";
    case PH_SEND_FRAGS: return "PH_SEND_FRAGS";
    case PH_SEND_END: return "PH_SEND_END";
    case PH_WAIT_RESULT: return "PH_WAIT_RESULT";
    case PH_DONE: return "PH_DONE";
    default: return "INVALID";
  }
}

void setup() {
  Serial.begin(115200);
  while(!Serial) {}
  delay(500);

  factoryClearIfRequested();

  int16_t st = radio.begin();
  debug(st != RADIOLIB_ERR_NONE, F("Radio init failed"), st, true);

  node.setADR(true);
  node.scanGuard = 30;

  // Load last_transmitted_row (default 0 if not present)
  if (!loadBlob(PATH_LASTROW, (uint8_t*)&last_transmitted_row, sizeof(last_transmitted_row))) {
    last_transmitted_row = 0;
  }
  Serial.print(F("last_transmitted_row=")); Serial.println(last_transmitted_row);
 
  phase = PH_JOINING;
  // st = lwActivate(); // nonces-only restore, join each boot
  // (void)st;

  // Serial.println(F("Ready!"));
  // phase = PH_INIT;
}

void loop() {
  Serial.println(phaseText(phase));

  switch (phase) {
    case PH_JOINING:
      lwActivateForever();
      break;
    case PH_INIT: {
      // Decide how many rows to send this cycle
      sessionRowsPlanned = (uint32_t)(CSV_ROWS_MIN + radio.random(CSV_ROWS_SPAN)); // 1000..5000
      sessionStartRow    = last_transmitted_row + 1;

      // Build the CSV buffer for this cycle
      buildCSVRange(sessionStartRow, sessionRowsPlanned, sessionRowsBuilt);

      Serial.print(F("CSV rows planned=")); Serial.print(sessionRowsPlanned);
      Serial.print(F(" built="));           Serial.print(sessionRowsBuilt);
      Serial.print(F(" bytes="));           Serial.println(CSV_LEN);

      sessId = (uint16_t)radio.random(0xFFFF);
      lastAckedSeq = -1;
      phase = PH_SEND_START;
    } break;

    case PH_SEND_START:
      if (sendStart()) phase = PH_SEND_FRAGS;
      break;

    case PH_SEND_FRAGS: {
      int8_t r = sendFrag();
      if (r == 1) phase = PH_SEND_END;   // all fragments emitted; wait for their ACKs and wrap up
      // r==0: sent one (ACK will arrive on subsequent ticks)
      // r==-1: no room; we already ticked
    } break;

    case PH_SEND_END:
      sendEnd(); // either becomes PH_WAIT_RESULT or already PH_DONE (if RESULT came inline)
      break;

    case PH_WAIT_RESULT:
      tickWaitResult();
      break;

    case PH_DONE: {
      // Update and persist last_transmitted_row ONLY after RESULT: OK
      uint32_t actuallySentRows = sessionRowsBuilt; // since we built the whole buffer
      uint32_t newLast = sessionStartRow + (actuallySentRows ? actuallySentRows - 1 : 0);

      if (newLast >= last_transmitted_row) {
        last_transmitted_row = newLast;
        if (saveBlob(PATH_LASTROW, (const uint8_t*)&last_transmitted_row, sizeof(last_transmitted_row))) {
          Serial.print(F("Persisted last_transmitted_row=")); Serial.println(last_transmitted_row);
        } else {
          Serial.println(F("WARN: failed to persist last_transmitted_row"));
        }
      }

      // "Sleep" then start next batch
      Serial.print(F("Sleeping for ms=")); Serial.println(SLEEP_MS);
      delay(SLEEP_MS);

      phase = PH_INIT;
    } break;

    default:
      delay(250);
      break;
  }

  delay(SEND_GAP_MS);
}
