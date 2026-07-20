#pragma once

#include <RadioLib.h>
#include "MeshCore.h"

// Custom RadioLib driver for the Semtech LR2021 (LoRa Plus), as used on the
// Seeed Wio-LR2021 (LR2021 + nRF54L15). Two board/chip quirks are handled here
// (both discovered during hardware bring-up):


#ifndef LR2021_IRQ_DIO
  #define LR2021_IRQ_DIO 8        // Wio-LR2021 wires the LR2021's DIO8 to the host IRQ line
#endif

#ifndef LR2021_RX_BOOST_LEVEL
  #define LR2021_RX_BOOST_LEVEL 7 // matches Semtech usp_zephyr rx-boost-cfg = <7>
#endif

// Multi-SF receive (LR2021 side detectors)

#if defined(LR2021_MULTISF_STEP)
  #if (LR2021_MULTISF_STEP) != 1
    #error "LR2021 multi-SF supports only STEP=1: the radio requires max(SF)-min(SF) <= 4, so STEP=2 (e.g. {6,8,10,12}, span 6) is impossible."
  #endif
  #ifndef LR2021_MULTISF
    #define LR2021_MULTISF 1
  #endif
#endif
#if defined(LR2021_MULTISF) && !defined(LR2021_MULTISF_SIDES)
  #define LR2021_MULTISF_SIDES 3   // full RX window above the floor
#endif
// TX-SF policy (OPT-IN, default off): send adverts at floor+offset.
#if defined(LR2021_MULTISF) && !defined(LR2021_ADVERT_SF_OFFSET)
  #define LR2021_ADVERT_SF_OFFSET 0
#endif
// Tuning knobs: SIDE_BASE places the first side detector at floor+BASE; DETPEAK_ADJ shifts every detection
// threshold from the Table 6-19 values (raise = fewer false grabs, less sensitive).
#if defined(LR2021_MULTISF) && !defined(LR2021_MULTISF_SIDE_BASE)
  #define LR2021_MULTISF_SIDE_BASE 1
#endif
#if defined(LR2021_MULTISF) && !defined(LR2021_MULTISF_DETPEAK_ADJ)
  #define LR2021_MULTISF_DETPEAK_ADJ 0
#endif
// per-packet multi-SF RX log (heavy: extra status read + print on every packet). Zephyr routes it
// to printk (RTT); other platforms to MESH_DEBUG_PRINTLN. OFF unless -DLR2021_MULTISF_DEBUG.
#if defined(LR2021_MULTISF_DEBUG)
  #if defined(__ZEPHYR__)
    #include <zephyr/sys/printk.h>
    #define LR2021_MSF_LOG(...) printk(__VA_ARGS__)
  #else
    #define LR2021_MSF_LOG(...) MESH_DEBUG_PRINTLN(__VA_ARGS__)
  #endif
#else
  #define LR2021_MSF_LOG(...)
#endif

class CustomLR2021 : public LR2021 {
  uint8_t _rx_boost_level = 0;
  uint8_t _saved_sf = 0;      // primary SF stashed while a per-TX SF switch is active
  bool    _sf_switched = false;
#ifdef LR2021_MULTISF
  uint8_t _det_sf[4] = { 0 }; // SF per detector index (0=main/floor, 1..3=side detectors)
  uint8_t _last_rx_sf = 0;    // SF the last good packet was demodulated on (per-link ADR)
#endif

public:
  CustomLR2021(Module *mod) : LR2021(mod) { }

  // route the host IRQ to the DIO the board actually wires (std_init does this
  // too; callers that do their own begin() sequence use this directly)
  void setIrqDio(uint8_t n) { irqDioNum = n; }

#if defined(ARDUINO)   // Arduino-core bring-up; Zephyr (compat shim, no SPIClass) drives begin() itself
  bool std_init(SPIClass* spi = NULL) {
    // route the host IRQ to the DIO the board actually wires (default DIO8)
    irqDioNum = LR2021_IRQ_DIO;

  #ifdef LORA_CR
    uint8_t cr = LORA_CR;
  #else
    uint8_t cr = 5;
  #endif

  #if defined(P_LORA_SCLK)
    #if defined(ESP32_PLATFORM)
      if (spi) spi->begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
    #elif defined(NRF52_PLATFORM)
      if (spi) { spi->setPins(P_LORA_MISO, P_LORA_SCLK, P_LORA_MOSI); spi->begin(); }
    #else
      if (spi) spi->begin();   // bare-metal nRF54L15 core: SPI pins are fixed (D8/D9/D10)
    #endif
  #else
    if (spi) spi->begin();
  #endif

    // tcxoVoltage = 0 -> skip SetTcxoMode (RadioLib mis-scales its start_time; see note above).
    int status = begin(LORA_FREQ, LORA_BW, LORA_SF, cr,
                       RADIOLIB_LR2021_LORA_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, 0.0f);
    if (status != RADIOLIB_ERR_NONE) {
      Serial.print("ERROR: LR2021 init failed: ");
      Serial.println(status);
      return false;
    }

    setCRC(2);
    explicitHeader();

  #ifdef RX_BOOSTED_GAIN
    if (RX_BOOSTED_GAIN) setRxBoostedGainMode(LR2021_RX_BOOST_LEVEL);
  #endif

    // arm multi-SF side detectors for the primary SF (no-op unless -DLR2021_MULTISF)
    int16_t ms = applyMultiSF();
    if (ms != RADIOLIB_ERR_NONE) {
      Serial.print("WARN: LR2021 multi-SF config failed: ");
      Serial.println(ms);  // degrade to single-SF; init still succeeds
    }

    return true;  // success
  }
#endif  // ARDUINO

  size_t getPacketLength(bool update) override {
    size_t len = LR2021::getPacketLength(update);
    uint32_t irq = getIrqFlags();
    if (len == 0 && (irq & RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR)) {
      // corrupted header: return to a known-good state; recvRaw restarts RX
      MESH_DEBUG_PRINTLN("LR2021: got header CRC err, calling standby()");
      standby();
    }
  #if defined(LR2021_MULTISF) && defined(RADIOLIB_GODMODE) && RADIOLIB_GODMODE
    // multi-SF / per-link ADR: record which detector (-> which SF) demodulated this packet
    if (irq & (RADIOLIB_LR2021_IRQ_RX_DONE | RADIOLIB_LR2021_IRQ_CRC_ERROR |
               RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR)) {
      uint8_t det = readDetectorIndex();   // 1=Main, 2=Side1, 4=Side2, 8=Side3
      uint8_t idx = det == 2 ? 1 : det == 4 ? 2 : det == 8 ? 3 : 0;
      bool crcFail = irq & RADIOLIB_LR2021_IRQ_CRC_ERROR;
      bool hdrErr  = irq & RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR;
      if (len > 0 && !crcFail && !hdrErr)
        _last_rx_sf = _det_sf[idx];        // SF of the last GOOD reception (reply-SF for ADR)
      LR2021_MSF_LOG("rx-pkt: det=%u sf=%u len=%u rssi=%d crc=%s\n", det, _det_sf[idx],
                     (unsigned)len, (int)getRSSI(), crcFail ? "FAIL" : (hdrErr ? "HDR" : "ok"));
    }
  #endif
    return len;
  }

  int16_t startReceive() override {
  #if defined(RADIOLIB_GODMODE) && RADIOLIB_GODMODE
    // re-assert max payload length before every RX: a TX leaves the chip's
    // packet-length param at the last TX size, which would clip longer
    // incoming packets. Needs GODMODE (setLoRaPacketParams is private).
    setLoRaPacketParams(this->preambleLengthLoRa, this->headerType,
                        RADIOLIB_LR2021_MAX_PACKET_LENGTH, this->crcTypeLoRa,
                        this->invertIQEnabled);
  #endif
  #ifdef LR2021_MULTISF
    applyMultiSF();   // re-arm side detectors each RX arm (begin/setSpreadingFactor wipe them)
  #endif
    return LR2021::startReceive();
  }

  bool isReceiving() {
    uint32_t irq = getIrqFlags();
    return (irq & RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED)
        || (irq & RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID);
  }

  int16_t setRxBoostedGainMode(uint8_t level) {
    _rx_boost_level = level;
    return LR2021::setRxBoostedGainMode(level);
  }

  uint8_t getRxBoostLevel() const { return _rx_boost_level; }

  uint8_t primarySF() const { return spreadingFactor; }
  float currentBandwidthKhz() const { return bandwidthKhz; }   // for LDRO-vs-BW checks outside the class

#ifdef LR2021_MULTISF
  // SF of the last good reception (0 = none yet) — feeds mesh::Radio::getLastRxSF() / ADR
  uint8_t lastRxSF() const { return _last_rx_sf; }

  // Highest SF this receiver can currently demodulate (floor + armed side detectors).
  // With the floor SF, this is the RX window advertised to peers (polyglot discovery).
  uint8_t topRxSF() const {
    uint8_t top = spreadingFactor;
    for (uint8_t i = 1; i < 4; i++) if (_det_sf[i] > top) top = _det_sf[i];
    return top;
  }

  // True iff `sf` is one of our currently-armed detectors (Main or a side). This is the
  // exact set the chip can demodulate, so it is checked against a candidate TX SF: a node
  // must never transmit at an SF it cannot itself receive.
  bool canRxSF(uint8_t sf) const {
    for (uint8_t i = 0; i < 4; i++) if (_det_sf[i] && _det_sf[i] == sf) return true;
    return false;
  }
#endif

#if defined(RADIOLIB_GODMODE) && RADIOLIB_GODMODE
  // Raw detector field of the last RX: 1=Main, 2=Side1, 4=Side2, 8=Side3 (0=none).
  // GetLoraPacketStatus byte 7 (datasheet Table 9-13) = ps[5] after RadioLib strips
  // the 2 leading stat bytes. Used by multi-SF RX tracking and the A/B test harness.
  uint8_t readDetectorIndex() {
    uint8_t ps[6] = { 0 };
    SPIcommand(RADIOLIB_LR2021_CMD_GET_LORA_PACKET_STATUS, false, ps, sizeof(ps));
    return (ps[5] >> 2) & 0x0F;
  }
#endif

  // (Re)configure the side detectors for the CURRENT primary SF/BW. Call after
  // begin() and after any runtime SF/BW change (see radio_set_params). No-op
  // unless -DLR2021_MULTISF is set; returns the driver status of the config.
  int16_t applyMultiSF() {
  #ifdef LR2021_MULTISF
    const uint8_t floorSf = spreadingFactor;   // primary detector is the floor
    const float   bw      = bandwidthKhz;

    // hardware caps on the number of side detectors
    uint8_t want = LR2021_MULTISF_SIDES;
    const uint8_t maxSides = (floorSf >= 10 || bw > 500.0f) ? 2 : 3;
    if (want > maxSides) want = maxSides;

    LR2021LoRaSideDetector_t sides[3];
    uint8_t n = 0;
    // side SFs start at floor+SIDE_BASE, within a span of 4 and <= SF12
    for (uint8_t sf = floorSf + LR2021_MULTISF_SIDE_BASE; sf <= 12 && (sf - floorSf) <= 4 && n < want; sf++) {
      sides[n].sf       = sf;
      // LDRO must MATCH the transmitter's auto-LDRO for this SF/BW (RadioLib: symbol time
      // >= 16 ms), else the detector syncs but every decode fails CRC.
      sides[n].ldro     = ((float)(1UL << sf) / bw) >= 16.0f;
      sides[n].invertIQ = false;
      sides[n].syncWord = RADIOLIB_LR2021_LORA_SYNC_WORD_PRIVATE;
      n++;
    }

    // map detector index -> SF (0=main/floor, 1..3=side detectors) for lastRxSF()
    _det_sf[0] = floorSf;
    for (uint8_t i = 0; i < 3; i++) _det_sf[i + 1] = (i < n) ? sides[i].sf : 0;

    if (n == 0) {
      // floor SF too high for any side (e.g. SF12): nothing to detect above it
    #if defined(RADIOLIB_GODMODE) && RADIOLIB_GODMODE
      return setLoRaSideDetConfig(nullptr, 0);  // clear any stale detectors
    #else
      return RADIOLIB_ERR_NONE;  // no public disable without GODMODE; avoid SF12 floor here
    #endif
    }

    int16_t st = setSideDetector(sides, n);
    // Detection thresholds: the chip's post-reset defaults are correct and sufficient .
  #if defined(RADIOLIB_GODMODE) && RADIOLIB_GODMODE && (LR2021_MULTISF_DETPEAK_ADJ != 0)
    if (st == RADIOLIB_ERR_NONE) {
      static const uint8_t detPeakBySf[8] = { 51, 51, 52, 54, 56, 60, 60, 65 };
      uint8_t pnrDelta[3], detPeak[3];
      for (uint8_t i = 0; i < n; i++) {
        pnrDelta[i] = RADIOLIB_LR2021_LORA_CAD_PNR_DELTA_STANDARD;
        int dp = (int)detPeakBySf[sides[i].sf - 5] + (LR2021_MULTISF_DETPEAK_ADJ);
        detPeak[i]  = dp < 0 ? 0 : (dp > 127 ? 127 : dp);
      }
      st = setLoRaSideDetCad(pnrDelta, detPeak, n);
    }
  #endif
    return st;
  #else
    return RADIOLIB_ERR_NONE;
  #endif
  }

  // Switch to `sf` for an upcoming transmit (no-op if sf==0 or already there).
  // Returns true if a switch happened, so the caller knows to restore afterwards.
  bool switchTxSF(uint8_t sf) {
    if (sf == 0 || sf == spreadingFactor) return false;
    standby();                        // change SF from standby, not continuous RX (avoids CMD_PERR)
    _saved_sf = spreadingFactor;
    if (setSpreadingFactor(sf) == RADIOLIB_ERR_NONE) { _sf_switched = true; return true; }
    return false;                     // SF unchanged on failure; nothing to restore
  }

  // Restore the RX floor SF and re-arm side detectors (no-op unless a TX switch is active).
  void restoreRxSF() {
    if (!_sf_switched) return;
    standby();
    setSpreadingFactor(_saved_sf);
    applyMultiSF();                   // side detectors are validated against the restored floor
    _sf_switched = false;
  }
};
