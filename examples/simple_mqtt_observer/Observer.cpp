/*
 * TODO: ADD BANNER
 */

#include "Observer.h"

#include "IPAddress.h"
#include "Utils.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdio.h>

#if defined(ESP32) || defined(RP2040_PLATFORM)
#include <FS.h>
#define FILESYSTEM fs::FS
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
#include <Adafruit_LittleFS.h>
#define FILESYSTEM Adafruit_LittleFS

using namespace Adafruit_LittleFS_Namespace;
#endif

// small parsing/formatting helpers (file-local)

static bool isAllZero(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (p[i] != 0) return false;
  }
  return true;
}

static bool isValidByte(int value) {
  return !(value < 0 || value > 255);
}

// Returns pointer to the argument (spaces skipped) if `cmd` starts with keyword
// `kw` followed by end-of-string or a space; NULL otherwise. Supports
// multi-word keywords like "mqttset host".
static char *matchCmd(char *cmd, const char *kw) {
  size_t n = strlen(kw);
  if (strncmp(cmd, kw, n) != 0) return NULL;
  if (cmd[n] != '\0' && cmd[n] != ' ') return NULL;
  char *p = cmd + n;
  while (*p == ' ')
    p++;
  return p;
}

static void copyArg(char *dst, size_t dstsize, const char *src) {
  size_t len = min(strlen(src), dstsize - 1);
  memcpy(dst, src, len);
  dst[len] = '\0';
}

static bool parseIPv4(const char *s, uint8_t out[4]) {
  int a, b, c, d;
  if (sscanf(s, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
    return false;
  }

  if (!isValidByte(a) || !isValidByte(b) || !isValidByte(c) || !isValidByte(d)) {
    // Might be necessary due to sscanf?
    return false;
  }
  out[0] = a;
  out[1] = b;
  out[2] = c;
  out[3] = d;
  return true;
}

static bool parseMAC(const char *s, uint8_t out[6]) {
  int m[6];
  if (sscanf(s, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6) return false;
  for (int i = 0; i < 6; i++) {
    if (!isValidByte(m[i])) {
      return false;
    }
    out[i] = (uint8_t)m[i];
  }
  return true;
}

static void formatIPv4(char *buf, const uint8_t ip[4]) {
  snprintf(buf, 16, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

static void stripTrailingSlashes(char *s) {
  size_t n = strlen(s);
  while (n > 0 && s[n - 1] == '/') {
    s[--n] = '\0';
  }
}

#ifndef MQTT_HOST
#define MQTT_HOST "test.mosquitto.org"
#endif

#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif

#ifndef MQTT_USERNAME
#define MQTT_USERNAME "guest"
#endif

#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD "guest"
#endif

#ifndef MQTT_TOPIC
// #define MQTT_TOPIC    "meshcore"
#define MQTT_TOPIC "meshcore/EU_868"
#endif

// TODO rename /observer_conf
#ifndef CONFIG_FILE
#define CONFIG_FILE "/observer_conf"
#endif

// NTP server used to set the clock once the network is up. DHCP only provides
// IP/mask/gw/DNS, not the wall-clock time, so we fetch it from here over UDP.
#ifndef NTP_SERVER
#define NTP_SERVER "pool.ntp.org"
#endif
#define NTP_PORT        123
#define NTP_LOCAL_PORT  8888
#define NTP_PACKET_SIZE 48
#define NTP_UNIX_OFFSET 2208988800UL

#ifndef MAX_RETRIES
#define MAX_RETRIES 5
#endif

// Per-packet serial tracing. Off by default: this fires for every received
// packet, and printing the full JSON costs real time on a deployed node. The
// per-event messages (connect, command, NTP) are always on - they are rare.
#ifndef OBSERVER_DEBUG_LOGGING
#define OBSERVER_DEBUG_LOGGING 0
#endif

Observer::Observer(mesh::MainBoard &board, mesh::Radio &radio, mesh::MillisecondClock &ms, mesh::RNG &rng,
                   mesh::RTCClock &rtc, mesh::MeshTables &tables)
    : MyMesh(board, radio, ms, rng, rtc, tables) {

  memset(&config, 0, sizeof(config));
  last_reconnect_attempt = 0;
  reconnect_interval = MQTT_RECONNECT_MIN_MS;
  last_logged_mqtt_state = MQTT_CONNECTED;
  link_up_at = 0;
  // Both are read in loop() before anything writes them. `the_mesh` is a global,
  // so static zero-initialisation covers it today - but that is an accident of
  // where the object lives, not a property of the class.
  last_ntp_attempt = 0;
  ntp_done = false;
  n_publish_errors = 0;
  n_queue_drops = 0;
  _q_head = _q_tail = _q_count = 0;

  Observer::instance = this;

  snprintf(config.mqttServer, sizeof(config.mqttServer), "%s", MQTT_HOST);
  config.serverPort = MQTT_PORT;
  snprintf(config.topic, sizeof(config.topic), "%s", MQTT_TOPIC);
  stripTrailingSlashes(config.topic);

  // Username/password are left empty by default (anonymous connect). Set them at
  // runtime with `mqttset user` / `mqttset pass`.

  // config.useTLS = false;
  //  Should be true most of the time
  //  bool insecureTLS;      // Allow TLS without certificate validation (setInsecure)
  //  bool useCustomCA;  // Use a user-provided CA certificate
  //  char caCert[2048]; // PEM-encoded CA certificate (optional)
}

void Observer::begin(FILESYSTEM *fs) {
  MyMesh::begin(fs);
  this->fs = fs;

  loadConfig();
  delay(1000);

  // Configure in place, never by assigning a temporary. PubSubClient owns a
  // malloc'd buffer and frees it in its destructor, but declares neither a copy
  // constructor nor an operator=, so `mqttClient = PubSubClient(...)` copies the
  // temporary's buffer pointer member-wise and then frees it when the temporary
  // dies. mqttClient is left holding a dangling pointer, and the setBufferSize()
  // below realloc()s it - which in newlib-nano frees the same block a second
  // time, closing the free list into a cycle. The next malloc (the MQTT connect
  // path, or xTaskCreate() for the Ethernet task) then spins forever.
  //
  // setServer() stores the pointer without copying, which is safe here:
  // config.mqttServer is a member array and outlives the client.
  mqttClient.setClient(ethClient);
  mqttClient.setServer(config.mqttServer, config.serverPort);
  mqttClient.setCallback(notifyAll);

  mqttClient.setBufferSize(2048); // Allow larger JSON payloads
  //  Improve connection robustness
  mqttClient.setKeepAlive(60);     // Increase keepalive to 60s
  mqttClient.setSocketTimeout(10); // Allow more time for TLS handshake/ops

  // No connectMQTT() here. main.cpp calls this before ethernet_start_task(), so
  // neither ETHERNET_SPI_PORT.begin() nor Ethernet.init() has run yet: the
  // W5100S is uninitialised, socketBegin() bails on getChip() == 0, and the
  // connect can only fail. loop() retries every 5 s once the link is up.
}

// Send a standard 48-byte NTP request to NTP_SERVER and, if a reply arrives,
// extract the transmit timestamp and set the RTC. Best-effort: failures (no
// socket, link down, DNS failure, timeout) just leaves the clock untouched.
bool Observer::syncTimeFromNTP() {
  EthernetUDP udp;
  if (!udp.begin(NTP_LOCAL_PORT)) {
    Serial.println("WARN: NTP: no free UDP socket");
    return false;
  }

  uint8_t pkt[NTP_PACKET_SIZE];
  memset(pkt, 0, sizeof(pkt));
  pkt[0] = 0b11100011; // LI=3 (unsynced), VN=4, Mode=3 (client)
  pkt[1] = 0;          // stratum
  pkt[2] = 6;          // polling interval
  pkt[3] = 0xEC;       // peer clock precision

  IPAddress ip = IPAddress();
  bool connectionSuccess;
  if (ip.fromString(NTP_SERVER)) {
    connectionSuccess = udp.beginPacket(ip, NTP_PORT);
  } else {
    connectionSuccess = udp.beginPacket(NTP_SERVER, NTP_PORT);
  }

  if (!connectionSuccess || udp.write(pkt, NTP_PACKET_SIZE) != NTP_PACKET_SIZE || !udp.endPacket()) {
    Serial.println("WARN: NTP: send failed (DNS/link?)");
    udp.stop();
    return false;
  }

  // Wait for the reply.
  uint32_t start = millis();
  while (millis() - start < 1500) {
    if (udp.parsePacket() >= NTP_PACKET_SIZE) {
      udp.read(pkt, NTP_PACKET_SIZE);
      udp.stop();

      // Transmit timestamp (seconds since 1900).
      uint32_t secs1900 = ((uint32_t)pkt[40] << 24) | ((uint32_t)pkt[41] << 16) | ((uint32_t)pkt[42] << 8) |
                          (uint32_t)pkt[43];

      if (secs1900 <= NTP_UNIX_OFFSET) {
        Serial.println("WARN: NTP: implausible reply, ignoring");
        return false;
      }
      uint32_t epoch = secs1900 - NTP_UNIX_OFFSET;

      // Never let the clock jump backwards.
      if (epoch > getRTCClock()->getCurrentTime()) {
        getRTCClock()->setCurrentTime(epoch);
      }

      Serial.print("INFO: NTP time set, epoch=");
      Serial.println(getRTCClock()->getCurrentTime());
      return true;
    }
  }

  udp.stop();
  Serial.println("WARN: NTP: no reply (timeout)");
  return false;
}

/*
Send message over ethernet logging packet
No decoding is done with this observer, it's up to the client of the broker to do it.
*/
void Observer::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
  MyMesh::logRxRaw(snr, rssi, raw, len);

  if (len <= 0 || len > (int)MAX_TRANS_UNIT) {
    return; // defensive: never index the ring with an out-of-range length
  }

  // Capture only - no JSON, no SPI, no TCP. See the RxSample comment in
  // Observer.h for why publishing from this call site can freeze the node.
  //
  // Queued regardless of connection state, so a short broker outage is covered
  // by the ring rather than losing every packet during it.
  if (_q_count == OBSERVER_QUEUE_LEN) {
    // Full: drop the OLDEST sample so the ring always holds the freshest data.
    _q_tail = (_q_tail + 1) % OBSERVER_QUEUE_LEN;
    _q_count--;
    n_queue_drops++;
  }

  RxSample &s = _queue[_q_head];
  s.timestamp = getRTCClock()->getCurrentTime(); // Unix epoch (set via NTP), not uptime
  s.rssi = rssi;
  s.snr = snr;
  s.len = (uint16_t)len;
  memcpy(s.raw, raw, len);

  _q_head = (_q_head + 1) % OBSERVER_QUEUE_LEN;
  _q_count++;
}

// Escape a string for embedding in a JSON string literal: the two characters
// that would break the document, plus anything non-printable. ArduinoJson did
// this implicitly; snprintf does not, and node_name is operator-supplied and
// validated nowhere upstream. The existing "Needs sanitization: control
// caracters make it fail to connect and/or mangles messages" note in
// connectMQTT() is the same problem seen from the other end.
// Buffer size needed to hold node_name once escaped. Worst case is every byte
// expanding to \uXXXX (6 chars), plus the NUL.
static constexpr size_t NAME_ESCAPED_MAX = sizeof(NodePrefs::node_name) * 6 + 1;

static size_t jsonEscape(const char *src, char *dst, size_t dst_size) {
  size_t o = 0;
  for (const char *p = src; *p && o + 7 < dst_size; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '"' || c == '\\') {
      dst[o++] = '\\';
      dst[o++] = c;
    } else if (c >= 0x20 && c < 0x7F) {
      dst[o++] = c;
    } else {
      o += snprintf(dst + o, dst_size - o, "\\u%04x", c);
    }
  }
  dst[o] = '\0';
  return o;
}

size_t Observer::formatSample(const RxSample &s, char *out, size_t out_size) {
  char pub_key_string[33];
  mesh::Utils::toHex(pub_key_string, self_id.pub_key, 16);
  // Full 16-byte public key of the observer node.

  char name_escaped[NAME_ESCAPED_MAX];
  jsonEscape(getNodePrefs()->node_name, name_escaped, sizeof(name_escaped));

  // Fixed 511-byte scratch (MAX_TRANS_UNIT * 2 + 1). Was a variable-length array
  // sized from the packet, which put up to 511 bytes on the stack per call.
  static char hexStr[MAX_TRANS_UNIT * 2 + 1];
  // TODO: base64 is more compact
  mesh::Utils::toHex(hexStr, s.raw, s.len);

  int n = snprintf(out, out_size,
                   "{\"timestamp\":%lu,\"gateway\":\"%s\",\"pub_key\":\"%s\","
                   "\"rssi\":%.1f,\"snr\":%.2f,\"length\":%u,\"data\":\"%s\"}",
                   (unsigned long)s.timestamp, name_escaped, pub_key_string, (double)s.rssi,
                   (double)s.snr, (unsigned)s.len, hexStr);

  if (n < 0) return 0;
  return (size_t)n >= out_size ? out_size - 1 : (size_t)n; // snprintf truncates, never overruns
}

bool Observer::publishNext() {
  if (_q_count == 0) return false;

  // Capacity gate - deliberately BEFORE any serialisation.
  //
  // socketSend() opens with an UNBOUNDED spin waiting for W5100S TX-buffer
  // space: it only breaks if the socket leaves ESTABLISHED/CLOSE_WAIT, so a peer
  // that stops reading (zero window) holds it there indefinitely.
  // availableForWrite() -> socketSendAvailable() is a single SPI read with no
  // loop, so asking first keeps us out of that spin for free.
  //
  // Checked against the worst-case frame rather than this sample's actual size:
  // knowing the actual size would mean serialising first, and a blocked socket
  // would then burn a full hex-encode plus format pass on every loop() iteration
  // only to discard it. Being slightly conservative costs nothing - the sample
  // stays queued either way.
  if ((size_t)ethClient.availableForWrite() < OBSERVER_MAX_WIRE_LEN) {
    return false; // no room, or socket not writable: retry next pass
  }

  const RxSample &s = _queue[_q_tail];

  uint8_t *pub_key = self_id.pub_key;
  uint32_t observer_id;
  memcpy(&observer_id, pub_key, sizeof(observer_id));
  // Observer_id is four first bytes, reversed
  //   key: 00 11 22 33 44 55 66 ...
  //   id: 33 22 11 00

  char topic[128];
  snprintf(topic, sizeof(topic), "%s/%08lx/raw", config.topic, (unsigned long)observer_id);
  // TODO add Band into topic
  // TODO add Datarate into topic

  size_t n = formatSample(s, _json, sizeof(_json));

#if OBSERVER_DEBUG_LOGGING
  Serial.print("[DEBUG] MQTT: publishing: ");
  Serial.println(_json);
#endif

  // Single attempt. The old retry loop re-entered a call that can still block on
  // the SEND_OK wait (bounded by the W5100S retransmission settings), which
  // multiplied the stall instead of avoiding it.
  if (n == 0 || !mqttClient.publish(topic, _json, false)) {
    n_publish_errors++;
#if OBSERVER_DEBUG_LOGGING
    Serial.print("[WARN] MQTT: publish failed, state=");
    Serial.println(mqttClient.state());
#endif
  }

  // Dequeue either way, so one unpublishable sample cannot wedge the queue.
  _q_tail = (_q_tail + 1) % OBSERVER_QUEUE_LEN;
  _q_count--;
  return true;
}

void Observer::loop() {
  MyMesh::loop();

  unsigned long now = millis();

  // Nothing below works without an address, and Ethernet bring-up is asynchronous:
  // ethernet_task() is still probing the controller and negotiating DHCP while
  // loop() already runs. After a soft `reboot` that stretch is much longer than
  // after a power-on - chip detection soft-resets the W5100S, so the PHY has to
  // renegotiate from scratch and the first DHCP attempt can time out and wait out
  // its 30 s retry.
  //
  // Treat that as "not started yet", never as a run of failures. Otherwise the
  // pre-link attempts inflate the MQTT backoff - leaving the observer idle for up
  // to another minute after the address finally arrives - and they burn the whole
  // NTP window, so the node ends up online with an unset clock. That is exactly
  // the difference the operator sees between a power cycle and a `reboot`.
  //
  // hardwareStatus() only returns a cached chip id and never touches SPI, so it
  // is safe before the controller is initialised; localIP() is not, hence the
  // order. Polling at the reconnect cadence keeps that SPI read off the hot path.
  if (link_up_at == 0) {
    if (now - last_reconnect_attempt <= MQTT_RECONNECT_MIN_MS) return;
    last_reconnect_attempt = now;
    if (Ethernet.hardwareStatus() == EthernetNoHardware ||
        Ethernet.localIP() == IPAddress(0, 0, 0, 0)) {
      last_ntp_attempt = now;
      reconnect_interval = MQTT_RECONNECT_MIN_MS;
      return;
    }
    link_up_at = now;
  }

  // NTP: keep trying for the first 60s after the link came up, every 5s, until
  // it succeeds. Once it works (or the window closes) leave the clock alone.
  if (!ntp_done) {
    if (now - last_ntp_attempt > 5000) {
      last_ntp_attempt = now;
      ntp_done = (now - link_up_at > 60000UL || syncTimeFromNTP());
    }
  }

  // Handle MQTT reconnection
  if (!isConnected()) {
    if (now - last_reconnect_attempt > reconnect_interval) {
      // Stamp the attempt unconditionally. Setting this to 0 on failure - as it
      // used to - made `now - 0 > interval` true on the very next pass, so a
      // refusing broker was retried every loop iteration instead of every 5 s.
      last_reconnect_attempt = now;
      if (!connectMQTT() && reconnect_interval < MQTT_RECONNECT_MAX_MS) {
        reconnect_interval *= 2;
        if (reconnect_interval > MQTT_RECONNECT_MAX_MS) reconnect_interval = MQTT_RECONNECT_MAX_MS;
      }
    }
  } else {
    mqttClient.loop();

    // Drain at most ONE queued sample per pass. socketSend() can block for a
    // long time on a degraded link, so draining the whole ring in one pass
    // would reintroduce exactly the stall this queue exists to avoid.
    publishNext();
  }
}

void Observer::getStatusMessage(bool online, char *out, size_t out_size) {
  char pub_key_string[33];
  mesh::Utils::toHex(pub_key_string, self_id.pub_key, 16);

  char name_escaped[NAME_ESCAPED_MAX];
  jsonEscape(getNodePrefs()->node_name, name_escaped, sizeof(name_escaped));

  snprintf(out, out_size, "{\"timestamp\":%lu,\"node\":\"%s\",\"pub_key\":\"%s\",\"online\":%s}",
           (unsigned long)getRTCClock()->getCurrentTime(), // Unix epoch (set via NTP), not uptime
           name_escaped, pub_key_string, online ? "true" : "false");
}

bool Observer::connectMQTT() {
  if (isConnected()) {
    return true;
  }

  // Prepare last will message for topic <prefix>/<datarate>/<8lsb>/interruption
  char *name = getNodePrefs()->node_name;
  uint32_t observer_id;
  memcpy(&observer_id, self_id.pub_key, sizeof(observer_id));

  char willTopic[128];
  snprintf(willTopic, sizeof(willTopic), "%s/%08lx/interruption", config.topic, (unsigned long)observer_id);
  // Needs sanitization: control caracters make it fail to connect and/or mangles messages.

  char willPayload[256];
  getStatusMessage(false, willPayload, sizeof(willPayload));

  if (strlen(config.username) > 0) {
    mqttClient.connect(name, config.username, config.password, willTopic, 1, true, willPayload);
  } else {
    mqttClient.connect(name, willTopic, 1, true, willPayload);
  }

  if (isConnected()) {
    Serial.println("[INFO] MQTT: Connection successful");
    // Back to a fast retry, and re-arm failure logging so a later drop is
    // reported instead of being swallowed as a repeat.
    reconnect_interval = MQTT_RECONNECT_MIN_MS;
    last_logged_mqtt_state = MQTT_CONNECTED;
    char onlinePayload[256];
    getStatusMessage(true, onlinePayload, sizeof(onlinePayload));
    mqttClient.publish(willTopic, onlinePayload, true);

#if ENABLE_COMMANDS == 1

    char cmd_topic[128];
    // Per-node command topic: <prefix>/<observer_id>/commands.
    //
    // A shared "<prefix>/commands" is subscribed by EVERY observer under the
    // same prefix, so a single publish there executes an admin CLI command on
    // all of them at once. Including the observer id scopes each node to its
    // own topic, and lets broker ACLs grant publish rights per node.
    //
    // This remains a remote CLI: whoever can publish here controls this node.
    // Restrict it with broker ACLs, or build without ENABLE_COMMANDS for a
    // strictly read-only observer.
    //
    // TODO add Band into topic
    // TODO add Datarate into topic
    snprintf(cmd_topic, sizeof(cmd_topic), "%s/%08lx/commands", config.topic,
             (unsigned long)observer_id);
    mqttClient.subscribe(cmd_topic);

    Serial.print("[INFO] MQTT: Subscribed to: ");
    Serial.println(cmd_topic);

#endif

  } else {
    // Report each distinct failure once. A broker that refuses us keeps
    // returning the same state, and repeating the line every retry buries the
    // CLI output. `mqttget` always shows the live state on demand.
    int state = mqttClient.state();
    if (state != last_logged_mqtt_state) {
      Serial.print("[ERR] MQTT: connection failed, state=");
      Serial.println(state);
      last_logged_mqtt_state = state;
    }
  }

  return isConnected();
}

void Observer::handleMQTTMessage(char *topic, byte *payload, unsigned int length) {

  // TODO use default logger
  Serial.print("[INFO] MQTT: message received on ");
  Serial.println(topic);

#if defined(ENABLE_COMMANDS)
  uint32_t observer_id;
  memcpy(&observer_id, self_id.pub_key, sizeof(observer_id));

  // Require an exact match on THIS node's command topic. The previous test only
  // asked whether "/commands" appeared anywhere in the topic, so any future
  // subscription whose name happened to contain that substring would have been
  // executed as a CLI command - PubSubClient routes every subscription through
  // this one callback.
  char expected_topic[128];
  snprintf(expected_topic, sizeof(expected_topic), "%s/%08lx/commands", config.topic,
           (unsigned long)observer_id);
  if (strcmp(topic, expected_topic) != 0) {
    return;
  }

  // Copy the payload into a bounded, null-terminated command buffer (MQTT
  // payloads are not null-terminated, and may be zero-length).
  char command[160];
  unsigned int n = min(length, sizeof(command) - 1);
  memcpy(command, payload, n);
  command[n] = '\0';

  Serial.print("[INFO] MQTT: executing command: ");
  Serial.print(command);

  char reply[160];
  handleCommand(0, command, reply); // no sender_timestamp available over MQTT

  // Publish the reply on the per-observer ack topic.
  char ack_topic[128];
  snprintf(ack_topic, sizeof(ack_topic), "%s/%08lx/ack", config.topic, (unsigned long)observer_id);
  mqttClient.publish(ack_topic, reply);
#else
  // Command handling not enabled at compile time
  //  log a textual payload for debugging purposes.
  if (length > 0 && payload[length - 1] == '\0') {
    Serial.print("[DEBUG] MQTT: Commands are disabled, payload received: ");
    Serial.println((char *)payload);
  }
#endif
}

void Observer::loadConfig() {
  if (fs->exists(CONFIG_FILE)) {
    File file = fs->open(CONFIG_FILE);

    file.read(config.network.mac, 6);
    file.read(config.network.ip, 4);
    file.read(config.network.dns, 4);
    file.read(config.network.gateway, 4);
    file.read(config.network.netmask, 4);

    file.read((char *)config.mqttServer, 128);

    uint8_t port[2];
    file.read(port, 2);

    config.serverPort = (port[0] << 8) + port[1];

    file.read((char *)config.username, 64);
    file.read((char *)config.password, 64);
    file.read((char *)config.topic, 64);

    file.close();

    // null-termination and restore defaults for empty fields.
    config.mqttServer[sizeof(config.mqttServer) - 1] = '\0';
    config.username[sizeof(config.username) - 1] = '\0';
    config.password[sizeof(config.password) - 1] = '\0';
    config.topic[sizeof(config.topic) - 1] = '\0';

    if (config.mqttServer[0] == '\0') snprintf(config.mqttServer, sizeof(config.mqttServer), "%s", MQTT_HOST);
    if (config.topic[0] == '\0') snprintf(config.topic, sizeof(config.topic), "%s", MQTT_TOPIC);
    stripTrailingSlashes(config.topic);
    if (config.serverPort == 0) config.serverPort = MQTT_PORT;
  }
}

void Observer::savePrefs() {
  MyMesh::savePrefs();

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove(CONFIG_FILE);
  File file = fs->open(CONFIG_FILE, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = fs->open(CONFIG_FILE, "w");
#else
  File file = fs->open(CONFIG_FILE, "w", true);
#endif

  file.write(config.network.mac, 6);
  file.write(config.network.ip, 4);
  file.write(config.network.dns, 4);
  file.write(config.network.gateway, 4);
  file.write(config.network.netmask, 4);

  file.write((uint8_t *)config.mqttServer, 128);
  // Probably may be optimised using a null terminated string instead, writing only required bytes

  uint8_t port[2];

  port[0] = (config.serverPort >> 8) & 0xFF;
  port[1] = config.serverPort & 0xFF;

  file.write(port, 2);
  file.write((uint8_t *)config.username, 64);
  file.write((uint8_t *)config.password, 64);

  file.write((uint8_t *)config.topic, 64);

  file.close();
}

void Observer::handleCommand(uint32_t sender_timestamp, char *command, char *reply) {

  while (*command == ' ') {
    command++;
  }
  reply[0] = '\0';

  char *arg;

  // MQTT config
  if (matchCmd(command, "mqttget") != NULL) {
    // Includes live state so a deployed node can be diagnosed over the CLI
    // without a debug build. puberr vs qdrop distinguishes "the broker rejected
    // the send" from "the outage outlasted the queue" - different problems.
    // `user` is dropped in favour of auth=yes/no: reply is only 160 bytes and
    // was already at risk of truncation (-Wformat-truncation).
    // host/topic are bounded explicitly: reply is 160 bytes and the fixed text
    // plus counters already takes ~98, so an unbounded host would push the tail
    // out and silently drop puberr/qdrop - the two fields worth reading. Better
    // to abbreviate the host than to lose the counters.
    snprintf(reply, 160,
             "mqtt host=%.36s:%u topic=%.24s auth=%s conn=%s state=%d q=%u/%u puberr=%lu qdrop=%lu",
             config.mqttServer, config.serverPort, config.topic,
             strlen(config.username) > 0 ? "yes" : "no", isConnected() ? "up" : "down",
             mqttClient.state(), (unsigned)_q_count, (unsigned)OBSERVER_QUEUE_LEN,
             (unsigned long)n_publish_errors, (unsigned long)n_queue_drops);
    return;
  }
  if ((arg = matchCmd(command, "mqttset host")) != NULL) {
    if (*arg == '\0') {
      snprintf(reply, 160, "ERR usage: mqttset host <hostname>");
      return;
    }
    copyArg(config.mqttServer, sizeof(config.mqttServer), arg);
    snprintf(reply, 160, "OK host=%s", config.mqttServer);
    savePrefs();
    reloadMQTT();
    return;
  }
  if ((arg = matchCmd(command, "mqttset port")) != NULL) {
    int p = atoi(arg);
    if (p <= 0 || p > 65535) {
      snprintf(reply, 160, "ERR invalid port");
      return;
    }
    config.serverPort = (uint16_t)p;
    snprintf(reply, 160, "OK port=%u", config.serverPort);
    savePrefs();
    reloadMQTT();
    return;
  }
  if ((arg = matchCmd(command, "mqttset user")) != NULL) {
    copyArg(config.username, sizeof(config.username), arg); // empty = anonymous
    snprintf(reply, 160, "OK user=%s", config.username);
    savePrefs();
    reloadMQTT();
    return;
  }
  if ((arg = matchCmd(command, "mqttset pass")) != NULL) {
    copyArg(config.password, sizeof(config.password), arg);
    snprintf(reply, 160, "OK password set (%u chars)", (unsigned)strlen(config.password));
    savePrefs();
    reloadMQTT();
    return;
  }
  if ((arg = matchCmd(command, "mqttset topic")) != NULL) {
    if (*arg == '\0') {
      snprintf(reply, 160, "ERR usage: mqttset topic <root>");
      return;
    }
    copyArg(config.topic, sizeof(config.topic), arg);
    stripTrailingSlashes(config.topic);
    snprintf(reply, 160, "OK topic=%s", config.topic);
    savePrefs();
    reloadMQTT();
    return;
  }

  // Network config
  if (matchCmd(command, "netget") != NULL) {
    char ipb[16], maskb[16], gwb[16], dnsb[16];
    formatIPv4(ipb, config.network.ip);
    formatIPv4(maskb, config.network.netmask);
    formatIPv4(gwb, config.network.gateway);
    formatIPv4(dnsb, config.network.dns);
    snprintf(reply, 160, "net mode=%s ip=%s mask=%s gw=%s dns=%s",
             isAllZero(config.network.ip, 4) ? "DHCP" : "STATIC", ipb, maskb, gwb, dnsb);
    return;
  }
  if (matchCmd(command, "netset dhcp") != NULL) {
    memset(config.network.ip, 0, 4);
    memset(config.network.dns, 0, 4);
    memset(config.network.gateway, 0, 4);
    memset(config.network.netmask, 0, 4);
    snprintf(reply, 160, "OK DHCP - applies at next boot");
    savePrefs();
    reloadMQTT();
    return;
  }
  if ((arg = matchCmd(command, "netset ip")) != NULL) {
    if (!parseIPv4(arg, config.network.ip)) {
      snprintf(reply, 160, "ERR usage: netset ip <a.b.c.d>");
      return;
    }
    snprintf(reply, 160, "OK ip set - applies at next boot");
    savePrefs();
    reloadMQTT();
    return;
  }
  if ((arg = matchCmd(command, "netset mask")) != NULL) {
    if (!parseIPv4(arg, config.network.netmask)) {
      snprintf(reply, 160, "ERR usage: netset mask <a.b.c.d>");
      return;
    }
    snprintf(reply, 160, "OK mask set - applies at next boot");
    savePrefs();
    reloadMQTT();
    return;
  }
  if ((arg = matchCmd(command, "netset gw")) != NULL) {
    if (!parseIPv4(arg, config.network.gateway)) {
      snprintf(reply, 160, "ERR usage: netset gw <a.b.c.d>");
      return;
    }
    snprintf(reply, 160, "OK gateway set - applies at next boot");
    savePrefs();
    reloadMQTT();
    return;
  }
  if ((arg = matchCmd(command, "netset dns")) != NULL) {
    if (!parseIPv4(arg, config.network.dns)) {
      snprintf(reply, 160, "ERR usage: netset dns <a.b.c.d>");
      return;
    }
    snprintf(reply, 160, "OK dns set - applies at next boot");
    savePrefs();
    reloadMQTT();
    return;
  }
  if (matchCmd(command, "ntpsync") != NULL) {
    if (syncTimeFromNTP()) {
      snprintf(reply, 160, "OK clock synced (epoch=%lu)", (unsigned long)getRTCClock()->getCurrentTime());
    } else {
      snprintf(reply, 160, "ERR NTP sync failed (check link/DNS)");
    }
    return;
  }
  if ((arg = matchCmd(command, "netset mac")) != NULL) {
    if (!parseMAC(arg, config.network.mac)) {
      snprintf(reply, 160, "ERR usage: netset mac <aa:bb:cc:dd:ee:ff>");
      return;
    }
    snprintf(reply, 160, "OK mac set - applies at next boot");
    savePrefs();
    return;
  }

  MyMesh::handleCommand(sender_timestamp, command, reply);
}

void Observer::reloadMQTT() {
  mqttClient.disconnect();

  mqttClient.setServer(config.mqttServer, config.serverPort);

  // The operator just changed the config, so give it a clean slate: retry fast
  // again, and let the next failure print even if it repeats the previous one.
  reconnect_interval = MQTT_RECONNECT_MIN_MS;
  last_logged_mqtt_state = MQTT_CONNECTED;

  connectMQTT();
}
