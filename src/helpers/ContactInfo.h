#pragma once

#include <Arduino.h>
#include <Mesh.h>

#define OUT_PATH_UNKNOWN   0xFF

struct ContactInfo {
  mesh::Identity id;
  char name[32];
  uint8_t type;   // on of ADV_TYPE_*
  uint8_t flags;
  uint8_t out_path_len;
  mutable bool shared_secret_valid; // flag to indicate if shared_secret has been calculated
  uint8_t out_path[MAX_PATH_SIZE];
  uint32_t last_advert_timestamp;   // by THEIR clock
  uint32_t lastmod;  // by OUR clock
  int32_t gps_lat, gps_lon;    // 6 dec places
  uint32_t sync_since;
#ifdef MESH_MULTISF
  // multi-SF / per-link ADR link metrics (learned from received packets/adverts)
  uint8_t  last_rx_sf;      // SF we last heard this contact on (0 = unknown) -> reply-SF fallback
  int8_t   last_rx_snr;     // SNR (dB) of the last packet from them
  int16_t  last_rx_rssi;    // RSSI (dBm) of the last packet from them
  int8_t   their_tx_power;  // their TX power (dBm) from advert feat1 (-128 = unknown) -> path loss
  // polyglot: the RX window this contact ADVERTISES (advert feat2, 0x5F marker; 0 = unknown).
  // pref_sf = their floor = the SF they prefer to be spoken to on; rx_top_sf = highest SF
  // their side detectors cover. Valid at any hop count (payload data, not a link measurement).
  uint8_t  pref_sf;
  uint8_t  rx_top_sf;
#endif

  const uint8_t* getSharedSecret(const mesh::LocalIdentity& self_id) const {
    if (!shared_secret_valid) {
      self_id.calcSharedSecret(shared_secret, id.pub_key);
      shared_secret_valid = true;
    }
    return shared_secret;
  }

private:
  mutable uint8_t shared_secret[PUB_KEY_SIZE];
};
