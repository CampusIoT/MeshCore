/*
 * TODO: ADD BANNER
 */

#include "Observer.h"

#include "HardwareSerial.h"

#include <cstddef>
#include <cstdint>
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
  size_t i = 0;
  while (src[i] != '\0' && i < dstsize - 1) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
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

static void formatIPv4(char *buf, const IPAddress ip) {
  snprintf(buf, 16, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

static void stripTrailingSlashes(char *s) {
  size_t n = strlen(s);
  while (n > 0 && s[n - 1] == '/') {
    s[--n] = '\0';
  }
}

#ifndef MQTT_HOST
// #define MQTT_HOST "127.0.0.1"
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

Observer::Observer(mesh::MainBoard &board, mesh::Radio &radio, mesh::MillisecondClock &ms, mesh::RNG &rng,
                   mesh::RTCClock &rtc, mesh::MeshTables &tables)
    : MyMesh(board, radio, ms, rng, rtc, tables) {

  memset(&config, 0, sizeof(config));
  last_reconnect_attempt = 0;

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
  // beginNetwork(); // bring up Ethernet (DHCP or static) before connecting MQTT

  mqttClient = PubSubClient(config.mqttServer, config.serverPort, notifyAll, ethClient);
  // mqttClient = PubSubClient(ethClient);

  // mqttClient.setServer(config.mqttServer, config.serverPort);
  //   Prefer hostname; if certificate CN/SAN does not match hostname (common when CN is an IP),
  //   we'll retry with the resolved IP address inside connectMQTT().

  // mqttClient.setCallback(notifyAll);

  // static function, sends the same info to all instances of Observer.

  mqttClient.setBufferSize(2048); // Allow larger JSON payloads
  //  Improve connection robustness
  mqttClient.setKeepAlive(60);     // Increase keepalive to 60s
  mqttClient.setSocketTimeout(10); // Allow more time for TLS handshake/ops

  connectMQTT();
}

// Bring up the Ethernet interface. Uses a static configuration when an IP has
// been set (any non-zero config.network.ip), otherwise falls back to DHCP. The
// MAC is taken from config when set, otherwise derived from the node's pub key.
void Observer::beginNetwork() {
// One-time hardware bring-up: reset the W5100S and bind the Ethernet library
// to the correct SPI bus + chip-select. On this board the W5100S sits on SPI1
// (separate from the LoRa radio on SPI0), so the library's defaults (default
// SPI + CS pin 10) are wrong and must be overridden before begin().
#ifdef PIN_ETHERNET_RESET
  pinMode(PIN_ETHERNET_RESET, OUTPUT);
  digitalWrite(PIN_ETHERNET_RESET, LOW);
  delay(50);
  digitalWrite(PIN_ETHERNET_RESET, HIGH);
  delay(200);
#endif
#if defined(ETH_SPI_PORT) && defined(PIN_ETHERNET_SS)
  Ethernet.init(ETH_SPI_PORT, PIN_ETHERNET_SS);
#elif defined(PIN_ETHERNET_SS)
  Ethernet.init(PIN_ETHERNET_SS);
#endif

  uint8_t mac[6];
  if (isAllZero(config.network.mac, 6)) {
    mac[0] = 0x02; // locally administered, unicast
    self_id.copyHashTo(mac + 1, 5);
  } else {
    memcpy(mac, config.network.mac, 6);
  }

  // Wait (bounded) for the PHY link to come up before starting DHCP. After the
  // chip reset the link takes a moment to negotiate; without this the first DHCP
  // DISCOVER goes out on a dead link and fails.
  uint32_t link_start = millis();
  while (Ethernet.linkStatus() != LinkON && millis() - link_start < 8000) {
    delay(100);
  }

  bool dhcp = isAllZero(config.network.ip, 4);
  bool ok = true;

  if (dhcp) {
    ok = Ethernet.begin(mac) ? true : false;

  } else {
    IPAddress ip(config.network.ip);
    IPAddress dns = isAllZero(config.network.dns, 4) ? ip : IPAddress(config.network.dns);
    IPAddress gw = isAllZero(config.network.gateway, 4) ? ip : IPAddress(config.network.gateway);
    IPAddress mask = isAllZero(config.network.netmask, 4) ? IPAddress(255, 255, 255, 0)
                                                          : IPAddress(config.network.netmask);
    Ethernet.begin(mac, ip, dns, gw, mask);
  }
  // Attempts to boot Ethernet module. Defaults to self on private network range for all unset values if IP
  // is.

  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println(F("ERROR: Ethernet hardware not found (check RAK13800 seating / SPI1)"));
    return;
  }

  if (!ok) {
    Serial.print(F("INFO: Ethernet DHCP FAILED (link down or no DHCP server?)"));
  } else {
    char b[16];
    formatIPv4(b, Ethernet.localIP());

    Serial.printf("INFO: Ethernet %s IP: %s", dhcp ? "DHCP" : "static", b);
  }
  Serial.println();

  EthernetLinkStatus link = Ethernet.linkStatus();
  Serial.print(F("INFO: Ethernet link: "));
  Serial.println(link == LinkON ? F("ON") : (link == LinkOFF ? F("OFF (check cable)") : F("unknown")));

  // NTP is attempted from loop() during a 60s startup window, not here.
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

  if (!udp.beginPacket(NTP_SERVER, NTP_PORT) || udp.write(pkt, NTP_PACKET_SIZE) != NTP_PACKET_SIZE ||
      !udp.endPacket()) {
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

  if (!mqttClient.connected()) {
    return;
  }

  char topic[128];
  // snprintf(topic, sizeof(topic), "%s/raw", config.topicPrefix);
  uint8_t *pub_key = self_id.pub_key;
  uint32_t observer_id;
  memcpy(&observer_id, pub_key, sizeof(observer_id));

  // Prepare last will message for topic <prefix>/<datarate>/<8lsb>/interruption
  // TODO add Band into topic
  // TODO add Datarate into topic
  // TODO add 8 LSB of public key
  snprintf(topic, sizeof(topic), "%s/%08x/raw", config.topic, observer_id); // TODO add the preset (Band ...)

  // Create JSON payload
  StaticJsonDocument<512> doc;
  doc["timestamp"] = getRTCClock()->getCurrentTime(); // Unix epoch (set via NTP), not uptime
  doc["rssi"] = rssi;
  doc["snr"] = snr;
  doc["gateway"] = getNodePrefs()->node_name;
  // doc["pub_key"] = pub_key;

  // TODO add pub_key (hex or base64) in the document

  // Convert data to hex string
  // TODO: base64 is more compact
  char hexStr[len * 2 + 1];
  mesh::Utils::toHex(hexStr, raw, len);
  hexStr[len * 2] = '\0';

  doc["data"] = hexStr;
  doc["length"] = len;

  String output;
  serializeJson(doc, output);

  Serial.print("[DEBUG] MQTT: data received: ");
  Serial.println(output);

  uint8_t attempt_number = 0;
  bool success = false;
  while (attempt_number < 5 && !success) {
    success = mqttClient.publish(topic, output.c_str(), false);
    attempt_number++;
  }
}

void Observer::loop() {
  MyMesh::loop();

  unsigned long now = millis();
  // NTP: keep trying during the first 60s of uptime, every 5s, until it
  // succeeds. Once it works (or the window closes) leave the clock alone.
  if (!ntp_done) {
    if (now - last_ntp_attempt > 5000) {
      last_ntp_attempt = now;
      ntp_done = (now > 60000UL || syncTimeFromNTP());
    }
  }

  // Handle MQTT reconnection
  if (!mqttClient.connected()) {
    if (now - last_reconnect_attempt > 5000) {
      last_reconnect_attempt = connectMQTT() ? now : 0;
    }
  } else {
    mqttClient.loop();
  }
}

const char *Observer::getStatusMessage(bool online) {
  StaticJsonDocument<128> jsonData;

  jsonData["node"] = getNodePrefs()->node_name;
  jsonData["timestamp"] = getRTCClock()->getCurrentTime(); // Unix epoch (set via NTP), not uptime

  jsonData["online"] = online;

  String message;
  serializeJson(jsonData, message);

  return message.c_str();
}

bool Observer::connectMQTT() {
  if (mqttClient.connected()) {
    return true;
  }

  // Prepare last will message for topic <prefix>/<datarate>/<8lsb>/interruption
  // TODO add Band into topic
  // TODO add Datarate into topic
  // TODO add 8 LSB of public key

  char *name = getNodePrefs()->node_name;
  uint8_t *pub_key = self_id.pub_key;
  uint32_t observer_id;
  memcpy(&observer_id, pub_key, sizeof(observer_id));

  char willTopic[128];
  snprintf(willTopic, sizeof(willTopic), "%s/%08x/interruption", config.topic, observer_id);

  const char *willPayload = getStatusMessage(false);

  if (strlen(config.username) > 0) {
    mqttClient.connect(name, config.username, config.password, willTopic, 1, true, willPayload);
  } else {
    mqttClient.connect(name, willTopic, 1, true, willPayload);
  }

  if (mqttClient.connected()) {
    Serial.println("[INFO] MQTT: Connection successful");
    mqttClient.publish(willTopic, getStatusMessage(true), false);

#if ENABLE_COMMANDS == 1

    char cmd_topic[128];
    // Prepare last will message for topic <prefix>/<datarate>/<8lsb>/interruption
    // TODO add Band into topic
    // TODO add Datarate into topic
    // TODO add 8 LSB of public key
    snprintf(cmd_topic, sizeof(cmd_topic), "%s/commands", config.topic); // TODO add the preset (Band ...)
    mqttClient.subscribe(cmd_topic);

    Serial.print("[INFO] MQTT: Subscribed to: ");
    Serial.println(cmd_topic);

#endif

  } else {
    Serial.print("[ERR] MQTT: connection failed, state=");
    Serial.println(mqttClient.state());
  }

  return mqttClient.connected();
}

void Observer::handleMQTTMessage(char *topic, byte *payload, unsigned int length) {

  // TODO use default logger
  Serial.print("[INFO] MQTT: message received on ");
  Serial.println(topic);

#if defined(ENABLE_COMMANDS)
  // We only subscribe to the commands topic, so treat any inbound message whose
  // topic ends in "/commands" as a CLI command to execute.
  if (strstr(topic, "/commands") == NULL) {
    return;
  }

  // Copy the payload into a bounded, null-terminated command buffer (MQTT
  // payloads are not null-terminated, and may be zero-length).
  char command[160];
  unsigned int n = min(length, sizeof(command) - 1);
  // length < sizeof(command) - 1 ? length : sizeof(command) - 1;
  memcpy(command, payload, n);
  command[n] = '\0';

  Serial.print("[INFO] MQTT: executing command: ");
  Serial.print(command);

  char reply[160];
  handleCommand(0, command, reply); // no sender_timestamp available over MQTT

  // Publish the reply on the per-observer ack topic.
  uint32_t observer_id;
  memcpy(&observer_id, self_id.pub_key, sizeof(observer_id));
  char ack_topic[128];
  snprintf(ack_topic, sizeof(ack_topic), "%s/%08x/ack", config.topic, observer_id);
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
    snprintf(reply, 160, "mqtt host=%s port=%u user=%s topic=%s auth=%s", config.mqttServer,
             config.serverPort, config.username, config.topic, strlen(config.username) > 0 ? "yes" : "no");
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
    snprintf(reply, 160, "OK DHCP, re-acquiring lease...");
    savePrefs();
    beginNetwork();
    reloadMQTT();
    return;
  }
  if ((arg = matchCmd(command, "netset ip")) != NULL) {
    if (!parseIPv4(arg, config.network.ip)) {
      snprintf(reply, 160, "ERR usage: netset ip <a.b.c.d>");
      return;
    }
    snprintf(reply, 160, "OK ip set, re-applying network...");
    savePrefs();
    beginNetwork();
    reloadMQTT();
    return;
  }
  if ((arg = matchCmd(command, "netset mask")) != NULL) {
    if (!parseIPv4(arg, config.network.netmask)) {
      snprintf(reply, 160, "ERR usage: netset mask <a.b.c.d>");
      return;
    }
    snprintf(reply, 160, "OK mask set");
    savePrefs();
    beginNetwork();
    reloadMQTT();
    return;
  }
  if ((arg = matchCmd(command, "netset gw")) != NULL) {
    if (!parseIPv4(arg, config.network.gateway)) {
      snprintf(reply, 160, "ERR usage: netset gw <a.b.c.d>");
      return;
    }
    snprintf(reply, 160, "OK gateway set");
    savePrefs();
    beginNetwork();
    reloadMQTT();
    return;
  }
  if ((arg = matchCmd(command, "netset dns")) != NULL) {
    if (!parseIPv4(arg, config.network.dns)) {
      snprintf(reply, 160, "ERR usage: netset dns <a.b.c.d>");
      return;
    }
    snprintf(reply, 160, "OK dns set");
    savePrefs();
    beginNetwork();
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
    snprintf(reply, 160, "OK mac set (applies on next boot)");
    savePrefs();
    return;
  }

  MyMesh::handleCommand(sender_timestamp, command, reply);
}

void Observer::reloadMQTT() {
  mqttClient.disconnect();

  mqttClient.setServer(config.mqttServer, config.serverPort);

  connectMQTT();
}
