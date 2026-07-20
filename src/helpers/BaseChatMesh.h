#pragma once

#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <helpers/AdvertDataHelpers.h>
#include <helpers/TxtDataHelpers.h>

#define MAX_TEXT_LEN    (10*CIPHER_BLOCK_SIZE)  // must be LESS than (MAX_PACKET_PAYLOAD - 4 - CIPHER_MAC_SIZE - 1)

#include "ContactInfo.h"

#define MAX_SEARCH_RESULTS   8

#define MSG_SEND_FAILED       0
#define MSG_SEND_SENT_FLOOD   1
#define MSG_SEND_SENT_DIRECT  2

#define REQ_TYPE_GET_STATUS      0x01   // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE      0x02

#define RESP_SERVER_LOGIN_OK      0   // response to ANON_REQ

class ContactVisitor {
public:
  virtual void onContactVisit(const ContactInfo& contact) = 0;
};

class BaseChatMesh;

class ContactsIterator {
  int next_idx = 0;
public:
  bool hasNext(const BaseChatMesh* mesh, ContactInfo& dest);
};

#ifndef MAX_CONTACTS
  #define MAX_CONTACTS  32
#endif

#define MAX_ANON_CONTACTS  8

#ifndef MAX_CONNECTIONS
  #define MAX_CONNECTIONS  16
#endif

struct ConnectionInfo {
  mesh::Identity server_id;
  unsigned long next_ping;
  uint32_t last_activity;
  uint32_t keep_alive_millis;
  uint32_t expected_ack;
};

#include "ChannelDetails.h"

/**
 *  \brief  abstract Mesh class for common 'chat' client
 */
#ifdef MESH_MULTISF
// Per-link ADR (multi-SF). Our TX power rides in the advert's officially-"FUTURE" feat1 field,
// tagged with a 0xAD marker byte so unrelated feat1 uses are ignored by our decoder.
#define ADR_TX_POWER_UNKNOWN  ((int8_t)-128)
static inline uint16_t adrEncodeTxPower(int8_t dbm) { return (uint16_t)0xAD00 | (uint8_t)dbm; }
static inline int8_t   adrDecodeTxPower(uint16_t feat1) {
  return ((feat1 >> 8) == 0xAD) ? (int8_t)(feat1 & 0xFF) : ADR_TX_POWER_UNKNOWN;
}
// Polyglot: our RX window (floor..top SF) rides in feat2, 0x5F marker byte + two SF nibbles.

static inline uint16_t adrEncodeSFWindow(uint8_t floor_sf, uint8_t top_sf) {
  if (top_sf < floor_sf) top_sf = floor_sf;   // single-SF radio: window degenerates to the floor
  return (uint16_t)0x5F00 | (uint16_t)((floor_sf & 0x0F) << 4) | (uint16_t)(top_sf & 0x0F);
}
// returns true + floor/top when feat2 carries a plausible window (marker + 5 <= floor <= top <= 12)
static inline bool adrDecodeSFWindow(uint16_t feat2, uint8_t* floor_sf, uint8_t* top_sf) {
  if ((feat2 >> 8) != 0x5F) return false;
  uint8_t f = (feat2 >> 4) & 0x0F, t = feat2 & 0x0F;
  if (f < 5 || f > 12 || t < f || t > 12) return false;
  *floor_sf = f; *top_sf = t;
  return true;
}
#endif

class BaseChatMesh : public mesh::Mesh {

  friend class ContactsIterator;

  ContactInfo contacts[MAX_CONTACTS+MAX_ANON_CONTACTS];
  int num_contacts;
  int sort_array[MAX_CONTACTS+MAX_ANON_CONTACTS];
  int matching_peer_indexes[MAX_SEARCH_RESULTS];
  unsigned long txt_send_timeout;
#ifdef MAX_GROUP_CHANNELS
  ChannelDetails channels[MAX_GROUP_CHANNELS];
  int num_channels;  // only for addChannel()
#endif
  mesh::Packet* _pendingLoopback;
  uint8_t temp_buf[MAX_TRANS_UNIT];
  ConnectionInfo connections[MAX_CONNECTIONS];

  mesh::Packet* composeMsgPacket(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt, const char *text, uint32_t& expected_ack);
  void sendAckTo(const ContactInfo& dest, const uint8_t* ack_hash, uint8_t ack_len=4);

protected:
  BaseChatMesh(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::PacketManager& mgr, mesh::MeshTables& tables)
      : mesh::Mesh(radio, ms, rng, rtc, mgr, tables)
  { 
    num_contacts = 0;
  #ifdef MAX_GROUP_CHANNELS
    memset(channels, 0, sizeof(channels));
    num_channels = 0;
  #endif
    txt_send_timeout = 0;
    _pendingLoopback = NULL;
    memset(connections, 0, sizeof(connections));
  }

  void bootstrapRTCfromContacts();
  void resetContacts() { num_contacts = 0; }
  void populateContactFromAdvert(ContactInfo& ci, const mesh::Identity& id, const AdvertDataParser& parser, uint32_t timestamp);
  ContactInfo* allocateContactSlot(bool transient_only=false); // helper to find slot for new contact

#ifdef MESH_MULTISF
  // Per-link ADR (multi-SF). Companion overrides getSelfAdvertTxPower() to advertise its TX power
  // (dBm) in the self-advert; default = unknown -> nothing added to the advert.
  virtual int8_t getSelfAdvertTxPower() const { return ADR_TX_POWER_UNKNOWN; }

  // The SF that makes a zero-hop transmission audible to contact c (0 = unknown -> floor SF).
  // Range-guarded: contacts restored from storage may predate the ADR fields, and a bogus SF
  // would make the driver reject the switch and silently transmit at floor.
  uint8_t linkTxSF(const ContactInfo& c) const {
    uint8_t sf = (c.pref_sf >= 5 && c.pref_sf <= 12) ? c.pref_sf
               : ((c.last_rx_sf >= 5 && c.last_rx_sf <= 12) ? c.last_rx_sf : 0);
    if (sf == 0) return 0;
    // Isolation invariant: only ever transmit at an SF we can also RECEIVE (one of our
    // main+side detectors). A contact whose SF is outside our window is unreachable to us.
    if (!_radio->canRxSF(sf)) return 0;
    return sf;
  }
  // Per-link SF for a ROUTED direct send: only a zero-hop path ends at the contact itself.
  // With repeaters in the out_path the first receiver is a repeater listening at the floor
  uint8_t selectTxSF(const ContactInfo& c) const {
    return (c.out_path_len == 0) ? linkTxSF(c) : 0;
  }
  // distinct non-floor SFs of every contact in the index (for the polyglot advert-back):
  // fills dest[] (max entries), returns the count
  int getContactAdvertSFs(uint8_t dest[], int max) const;
  // Cross-floor flood rescue: clone 'src' and send it zero-hop direct at the contact's SF.
  void sendCrossSFZeroHopCopy(const ContactInfo& c, const mesh::Packet* src, uint32_t delay_millis);
#endif

  // 'UI' concepts, for sub-classes to implement
  virtual bool isAutoAddEnabled() const { return true; }
  virtual bool shouldAutoAddContactType(uint8_t type) const { return true; }
  virtual void onContactsFull() {};
  virtual bool shouldOverwriteWhenFull() const { return false; }
  virtual uint8_t getAutoAddMaxHops() const { return 0; }  // 0 = no limit, 1 = direct (0 hops), N = up to N-1 hops
  virtual void onContactOverwrite(const uint8_t* pub_key) {};
  virtual void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) = 0;
  virtual ContactInfo* processAck(const uint8_t *data) = 0;
  virtual void onContactPathUpdated(const ContactInfo& contact) = 0;
  virtual bool onContactPathRecv(ContactInfo& from, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len);
  virtual void onMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) = 0;
  virtual void onCommandDataRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) = 0;
  virtual void onSignedMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const uint8_t *sender_prefix, const char *text) = 0;
  virtual uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const = 0;
  virtual uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const = 0;
  virtual void onSendTimeout() = 0;
  virtual void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp, const char *text) = 0;
  virtual void onChannelDataRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint16_t data_type,
                                 const uint8_t* data, size_t data_len) {}
  virtual uint8_t onContactRequest(const ContactInfo& contact, uint32_t sender_timestamp, const uint8_t* data, uint8_t len, uint8_t* reply) = 0;
  virtual void onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) = 0;
  virtual void handleReturnPathRetry(const ContactInfo& contact, const uint8_t* path, uint8_t path_len);

  virtual void sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis=0);
  virtual void sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis=0);

  // storage concepts, for sub-classes to override/implement
  virtual int  getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) { return 0; }  // not implemented
  virtual bool putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], int len) { return false; }

  // Mesh overrides
  void onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len) override;
  int searchPeersByHash(const uint8_t* hash) override;
  void getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) override;
#ifdef MESH_MULTISF
  uint8_t getPeerTxSF(int peer_idx) const override;   // per-link SF for core-Mesh direct replies
#endif
  void onPeerDataRecv(mesh::Packet* packet, uint8_t type, int sender_idx, const uint8_t* secret, uint8_t* data, size_t len) override;
  bool onPeerPathRecv(mesh::Packet* packet, int sender_idx, const uint8_t* secret, uint8_t* path, uint8_t path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  void onAckRecv(mesh::Packet* packet, uint32_t ack_crc) override;
#ifdef MAX_GROUP_CHANNELS
  int searchChannelsByHash(const uint8_t* hash, mesh::GroupChannel channels[], int max_matches) override;
#endif
  void onGroupDataRecv(mesh::Packet* packet, uint8_t type, const mesh::GroupChannel& channel, uint8_t* data, size_t len) override;

  // Connections
  bool startConnection(const ContactInfo& contact, uint16_t keep_alive_secs);
  void stopConnection(const uint8_t* pub_key);
  bool hasConnectionTo(const uint8_t* pub_key);
  void markConnectionActive(const ContactInfo& contact);
  ContactInfo* checkConnectionsAck(const uint8_t* data);
  void checkConnections();

public:
  mesh::Packet* createSelfAdvert(const char* name);
  mesh::Packet* createSelfAdvert(const char* name, double lat, double lon);
  int  sendMessage(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt, const char* text, uint32_t& expected_ack, uint32_t& est_timeout);
  int  sendCommandData(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt, const char* text, uint32_t& est_timeout);
  bool sendGroupMessage(uint32_t timestamp, mesh::GroupChannel& channel, const char* sender_name, const char* text, int text_len);
  bool sendGroupData(mesh::GroupChannel& channel, uint8_t* path, uint8_t path_len, uint16_t data_type, const uint8_t* data, int data_len);
  int  sendLogin(const ContactInfo& recipient, const char* password, uint32_t& est_timeout);
  int  sendAnonReq(const ContactInfo& recipient, const uint8_t* data, uint8_t len, uint32_t& tag, uint32_t& est_timeout);
  int  sendRequest(const ContactInfo& recipient, uint8_t req_type, uint32_t& tag, uint32_t& est_timeout);
  int  sendRequest(const ContactInfo& recipient, const uint8_t* req_data, uint8_t data_len, uint32_t& tag, uint32_t& est_timeout);
  bool shareContactZeroHop(const ContactInfo& contact);
  uint8_t exportContact(const ContactInfo& contact, uint8_t dest_buf[]);
  bool importContact(const uint8_t src_buf[], uint8_t len);
  void resetPathTo(ContactInfo& recipient);
  void scanRecentContacts(int last_n, ContactVisitor* visitor);
  ContactInfo* searchContactsByPrefix(const char* name_prefix);
  ContactInfo* lookupContactByPubKey(const uint8_t* pub_key, int prefix_len);
  bool  removeContact(ContactInfo& contact);
  bool  addContact(const ContactInfo& contact);
  int getNumContacts() const { return num_contacts; }
  bool getContactByIdx(uint32_t idx, ContactInfo& contact);
  ContactsIterator startContactsIterator();
  ChannelDetails* addChannel(const char* name, const char* psk_base64);
  bool getChannel(int idx, ChannelDetails& dest);
  bool setChannel(int idx, const ChannelDetails& src);
  int findChannelIdx(const mesh::GroupChannel& ch);

  void loop();
};
