/*
 * TODO: ADD BANNER
 */

#include "../simple_repeater/MyMesh.h"

#include <EthernetClient.h>
#include <PubSubClient.h>
#include <RAK13800_W5100S.h>
#include <SPI.h>
#include <Utils.h>
#include <stdlib.h>

// Depth of the deferred publish ring (see RxSample below). Each slot costs
// sizeof(RxSample) (~268 B). 8 slots covers roughly 10-15 s of broker outage at
// SF8/BW62.5 max-packet rates; beyond that the oldest samples are dropped, which
// is the right trade for an observer - fresh data matters more than a backlog.
#ifndef OBSERVER_QUEUE_LEN
#define OBSERVER_QUEUE_LEN 8
#endif

// Worst-case bytes PubSubClient hands the socket in one write():
//   fixed header (<=5) + 2-byte topic length + topic (<=128) + payload.
// Payload worst case is a 255-byte packet: 510 hex chars, plus the 32-char pub
// key, a 31-char node name, timestamp/rssi/snr/length and JSON punctuation -
// about 710 B. The W5100S socket TX buffer is 2048 B (SSIZE), so waiting for
// this much free space is satisfiable rather than a deadlock.
#ifndef OBSERVER_MAX_WIRE_LEN
#define OBSERVER_MAX_WIRE_LEN 1024
#endif

// Max serialised JSON payload, from the same derivation as above (~710 B worst
// case). snprintf() truncates rather than overruns if that estimate is ever off.
#ifndef OBSERVER_JSON_MAX
#define OBSERVER_JSON_MAX 800
#endif

struct MQTTConfig { // TODO : refactor for ObserverConfig
  /*
  Config detailing Ethernet settings. Currently supports IPv4-type addresses.

  For each array, if all bytes are null, default to automatic mode (i.e. guessing/factory default)
   */
  struct NetworkConfig {
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t dns[4];
    uint8_t gateway[4];
    uint8_t netmask[4];
  };

  NetworkConfig network;

  char mqttServer[128];
  uint16_t serverPort;

  char username[64];
  char password[64];

  char topic[64];
};

class Observer : public MyMesh {
  FILESYSTEM *fs;
  EthernetClient ethClient;
  PubSubClient mqttClient;
  unsigned long last_reconnect_attempt;
  unsigned long last_ntp_attempt;

  bool ntp_done;

  // Packets dropped because publish() failed (broker unreachable at send time).
  uint32_t n_publish_errors;
  // Packets dropped because the ring was full (outage longer than it covers).
  uint32_t n_queue_drops;

  // --- Deferred publish queue -------------------------------------------------
  // logRxRaw() is called from Dispatcher::checkRecv(). Publishing from there is
  // unsafe: EthernetClass::socketSend() spins with NO timeout, first waiting for
  // W5100S TX-buffer space and then for SEND_OK. A hung broker or a stalled TCP
  // connection therefore freezes the whole mesh node - no packet reception, no
  // TX servicing, no watchdog petting - until TCP tears the socket down, which
  // is seconds under RTO backoff. yield() does not run the MeshCore loop.
  //
  // So logRxRaw() only copies the sample into this fixed ring, and loop() does
  // the blocking work. Statically sized: no allocation on the receive path.
  struct RxSample {
    uint32_t timestamp;
    float rssi;
    float snr;
    uint16_t len;
    uint8_t raw[MAX_TRANS_UNIT];
  };

  RxSample _queue[OBSERVER_QUEUE_LEN];
  uint8_t _q_head, _q_tail, _q_count;

  // Scratch buffer for the serialised payload. Single-threaded, reused per
  // publish, so the send path allocates nothing.
  char _json[OBSERVER_JSON_MAX];

  // PubSubClient takes a plain C function pointer for its callback, so we keep a single static pointer to
  // the live instance to route messages back to it.
  static Observer *instance;

  struct MQTTConfig config;

public:
  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len);
  // logRx can be used for MeshCore's packets with a valid header but by default, listens to any packet.

  void begin(FILESYSTEM *fs);

  Observer(mesh::MainBoard &board, mesh::Radio &radio, mesh::MillisecondClock &ms, mesh::RNG &rng,
           mesh::RTCClock &rtc, mesh::MeshTables &tables);

  void loop();
  void savePrefs();
  void handleCommand(uint32_t sender_timestamp, char *command, char *reply);

private:
  void handleMQTTMessage(char *topic, uint8_t *payload, unsigned int length);

  inline bool isConnected() { return mqttClient.connected(); }
  bool connectMQTT();

  // Initialise the Ethernet interface from config (static IP or DHCP).
  void beginNetwork();

  // Query an NTP server over UDP and set the RTC from the result. Called automatically once the network is up
  // (DHCP/static), and via the `ntpsync` command. Returns true if the clock was updated.
  bool syncTimeFromNTP();

  static void notifyAll(char *topic, uint8_t *payload, unsigned int length) {
    if (Observer::instance != nullptr) {
      Observer::instance->handleMQTTMessage(topic, payload, length);
    }
  }

  /*
   Attempts to load existing MQTT config from filesystem. In case of failure, use default values when possible
   instead
  */
  void loadConfig();

  /* Called when changing some settings of the MQTT client. Disconnects properly current session and
   * reconnects with new parameters*/
  void reloadMQTT();

  /* Creates JSON status messages, containing last successful connection timestamp, node name and current
   * availability. Writes into a caller-supplied buffer - no Arduino String, so no heap. */
  void getStatusMessage(bool online, char *out, size_t out_size);

  /* Serialise one queued sample as JSON into `out`. Built with snprintf rather than a JSON
   * document: the payload is a fixed flat shape, so a DOM buys nothing and costs the heap.
   * Returns bytes written, excluding the NUL. */
  size_t formatSample(const RxSample &s, char *out, size_t out_size);

  /* Serialise and publish at most one queued sample. Called from loop(), where blocking on the
   * socket is acceptable. Returns true if a sample was dequeued (whether or not it published). */
  bool publishNext();
};