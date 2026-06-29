/*
 * TODO: ADD BANNER
 */

#include "MyMesh.h"

#include <EthernetClient.h>
#include <PubSubClient.h>
#include <RAK13800_W5100S.h>
#include <SPI.h>
#include <Utils.h>
#include <stdlib.h>

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
  bool eth_hw_init;

  // PubSubClient takes a plain C function pointer for its callback, so we keep a single static pointer to
  // the live instance to route messages back to it.
  static Observer *instance;

  struct MQTTConfig config;

public:
  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len);
  // logRx can be used for MeshCore's packets with a valid header but by default, listens to any packet.

  // void logRx(mesh::Packet *pkt, int len, float score);

  void begin(FILESYSTEM *fs);

  Observer(mesh::MainBoard &board, mesh::Radio &radio, mesh::MillisecondClock &ms, mesh::RNG &rng,
           mesh::RTCClock &rtc, mesh::MeshTables &tables);

  void loop();
  void savePrefs();
  void handleCommand(uint32_t sender_timestamp, char *command, char *reply);

private:
  const char *getStatusMessage(bool online);

  void handleMQTTMessage(char *topic, uint8_t *payload, unsigned int length);

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

  void reloadMQTT();
};