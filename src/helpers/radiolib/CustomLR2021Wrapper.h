#pragma once

#include "CustomLR2021.h"
#include "RadioLibWrappers.h"

// MeshCore wrapper for the LR2021. Implements the RadioLibWrapper hooks the mesh
// engine needs, using the LR2021's RadioLib API (getRssiInst / getRSSI / getSNR /
// boosted-gain). doResetAGC() is left to the base-class default for now; add an
// LR2021-specific reset only if RX sensitivity degrades over long runs.

class CustomLR2021Wrapper : public RadioLibWrapper {
  uint8_t _man_sf = 0;  // manual override (radio_set_tx_sf): persists until cleared with 0
  uint8_t _pkt_sf = 0;  // per-packet ADR SF from the Dispatcher: consumed by the next send

public:
  CustomLR2021Wrapper(CustomLR2021& radio, mesh::MainBoard& board)
    : RadioLibWrapper(radio, board) { }

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    ((CustomLR2021 *)_radio)->setFrequency(freq);
    ((CustomLR2021 *)_radio)->setSpreadingFactor(sf);
    ((CustomLR2021 *)_radio)->setBandwidth(bw);
    ((CustomLR2021 *)_radio)->setCodingRate(cr);
    ((CustomLR2021 *)_radio)->applyMultiSF();   // any SF/BW change wipes the side detectors: re-arm for the new floor
    updatePreamble(sf);
  }

  // Manual TX-SF override on top of the per-packet policy: forces every send to `sf` (should be >
  // the RX floor SF so peers' side detectors catch them); pass 0 to hand control back to the policy.
  void setTxSpreadingFactor(uint8_t sf) { _man_sf = sf; }
  uint8_t getTxSpreadingFactor() const {
    return _man_sf ? _man_sf : ((CustomLR2021 *)_radio)->primarySF();
  }

  // Per-packet TX-SF policy (async-safe, decided at transmit time): adverts go out at
  // floor+LR2021_ADVERT_SF_OFFSET, everything else at the floor. Returns 0 to mean "floor SF".
  uint8_t policyTxSF(const uint8_t* bytes, int len) {
  #if defined(LR2021_MULTISF) && defined(LR2021_ADVERT_SF_OFFSET) && (LR2021_ADVERT_SF_OFFSET > 0)
    if (len >= 1) {
      uint8_t ptype = (bytes[0] >> PH_TYPE_SHIFT) & PH_TYPE_MASK;  // mesh::Packet::getPayloadType()
      if (ptype == PAYLOAD_TYPE_ADVERT) {
        uint8_t sf = ((CustomLR2021 *)_radio)->primarySF() + LR2021_ADVERT_SF_OFFSET;
        return sf > 12 ? 12 : sf;   // clamp to the SF12 ceiling / side-detector window
      }
    }
  #endif
    (void)bytes; (void)len;
    return 0;
  }

  bool startSendRaw(const uint8_t* bytes, int len) override {
    uint8_t sf = _man_sf ? _man_sf : (_pkt_sf ? _pkt_sf : policyTxSF(bytes, len));
    uint8_t floor_sf = ((CustomLR2021 *)_radio)->primarySF();
    LR2021_MSF_LOG("tx-pkt: sf=%u (%s) len=%d\n", sf ? sf : floor_sf,
                   _man_sf ? "man" : (_pkt_sf ? "adr" : "policy"), len);
    if (sf && sf != floor_sf && !((CustomLR2021 *)_radio)->switchTxSF(sf)) {
      // switch rejected (busy/param error): the packet WILL go out at the floor SF --
      // make that visible instead of logging the SF we merely intended
      LR2021_MSF_LOG("tx-pkt: SF switch to %u FAILED -> tx at floor SF%u\n", sf, floor_sf);
    }
    bool ok = RadioLibWrapper::startSendRaw(bytes, len);
    if (!ok) { ((CustomLR2021 *)_radio)->restoreRxSF(); _pkt_sf = 0; }  // failure skips onSendFinished()
    return ok;
  }

  // SF-aware airtime: the Dispatcher's send timeout and duty-cycle accounting must reflect the
  // SF this packet will ACTUALLY use (~2x airtime per SF step above the floor). Without this a
  // floor-based estimate set a timeout shorter than the real transmission and truncated it.
  uint32_t getEstAirtimeFor(int len_bytes) override {
    uint32_t t = RadioLibWrapper::getEstAirtimeFor(len_bytes);   // at the floor modulation
    uint8_t sf = _man_sf ? _man_sf : _pkt_sf;
    uint8_t floor_sf = ((CustomLR2021 *)_radio)->primarySF();
    if (sf > floor_sf) t <<= (sf - floor_sf);
    else if (sf && sf < floor_sf) t >>= (floor_sf - sf);
    return t;
  }

#ifdef LR2021_MULTISF
  // per-link ADR hooks (mesh::Radio): the Dispatcher stamps each outbound packet's
  // TX-SF right before startSendRaw, and reads the SF the last packet arrived on.
  uint8_t getLastRxSF() const override { return ((CustomLR2021 *)_radio)->lastRxSF(); }
  void setTxSF(uint8_t sf) override { _pkt_sf = sf; }
  // RX window (floor..top) this node currently demodulates — advertised to peers (polyglot)
  uint8_t getFloorRxSF() const override { return ((CustomLR2021 *)_radio)->primarySF(); }
  uint8_t getTopRxSF() const override { return ((CustomLR2021 *)_radio)->topRxSF(); }
  // whether `sf` is one of our armed detectors — gates TX-SF so we never transmit off-window
  bool canRxSF(uint8_t sf) const override { return ((CustomLR2021 *)_radio)->canRxSF(sf); }
#endif

  bool isReceivingPacket() override {
    return ((CustomLR2021 *)_radio)->isReceiving();
  }

  void onBeforeStartRecv() override {
    // re-arming (setRxPath) while still in continuous RX -> CMD_PERR (-706)
    // and a wedged receiver; drop to standby before every startReceive()
    _radio->standby();
  }

  float getCurrentRSSI() override {
    float rssi = -110;
    ((CustomLR2021 *)_radio)->getRssiInst(&rssi);
    return rssi;
  }

  void onSendFinished() override {
    RadioLibWrapper::onSendFinished();                 // finishTransmit() -> standby
    ((CustomLR2021 *)_radio)->restoreRxSF();           // restore floor SF + side detectors before RX resumes
    _pkt_sf = 0;                                       // per-packet SF is consumed by this send
    _radio->setPreambleLength(16);  // overcomes weird issues with small and big pkts
  }

  float getLastRSSI() const override { return ((CustomLR2021 *)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomLR2021 *)_radio)->getSNR(); }

  void setRxBoostedGainMode(bool en) override {
    ((CustomLR2021 *)_radio)->setRxBoostedGainMode(en ? LR2021_RX_BOOST_LEVEL : 0);
  }
  bool getRxBoostedGainMode() const override {
    return ((CustomLR2021 *)_radio)->getRxBoostLevel() != 0;
  }
};
