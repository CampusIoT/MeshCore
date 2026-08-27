#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>

#include "MyMesh.h"

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[160];

// For power saving
unsigned long POWERSAVING_FIRSTSLEEP_SECS = 120; // The first sleep (if enabled) from boot

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_)
#include <helpers/ui/MomentaryButton.h>
#define USER_BTN_HOLD_OFF_MILLIS 1500
// Active-LOW button on the internal pull-up, so reverse=true and pulldownup=true.
// Multi-click detection is on by default; check() cancels pending clicks if the press
// turns into a long press, so hold-to-power-off and double-click can't both fire.
static MomentaryButton user_btn(PIN_USER_BTN, USER_BTN_HOLD_OFF_MILLIS, true, true);
// MomentaryButton polls digitalRead(); the pin has no GPIOTE interrupt, so a press cannot wake
// the CPU out of sd_app_evt_wait(). Keep the loop spinning briefly around any button activity
// or powersaving mode will sample too slowly to ever see the 280ms double-click window.
static unsigned long btn_active_until = 0;
static bool btn_was_pressed = false;
#endif

void setup() {
  Serial.begin(115200);

#if defined(MESH_DEBUG) && defined(NRF52_PLATFORM)
  // Wait for the USB CDC host to actually open the port, BEFORE board.begin(), that is where
  // the power-management diagnostics (reset reason, shutdown reason, boot voltage) are printed.
  // After a reboot the host has not finished re-enumerating yet.
  #ifndef MESH_DEBUG_BOOT_WAIT_MS
    #define MESH_DEBUG_BOOT_WAIT_MS 20000
  #endif
  {
    unsigned long t0 = millis();
    while (!Serial && (millis() - t0) < MESH_DEBUG_BOOT_WAIT_MS) delay(10);
    delay(5000);   // let the terminal settle once it has opened the port
  }
#endif

  delay(1000);

  board.begin();

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_)
  user_btn.begin();
  // Boot marker: confirms which pin the button is bound to and its idle level.
  // For an active-LOW button on a pull-up, idle must read 1.
  Serial.printf("btn: user=pin%d idle=%d\n", PIN_USER_BTN, digitalRead(PIN_USER_BTN));
#endif

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) {
    MESH_DEBUG_PRINTLN("Radio init failed!");
    halt();
  }

  fast_rng.begin(radio_driver.getRngSeed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM) || defined(NRF54_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Repeater ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

  // send out initial zero hop Advertisement to the mesh.
  // Under the 'share' policy this deliberately goes out with 0,0: node_lat/node_lon are still
  // zero at boot, so this advert announces "we are online" and the position follows separately
  // once GPS acquires a fix (see the first-fix handler in loop()).
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif

  board.onBootComplete();
}

void loop() {
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {   // end of line: accept CR, LF or CRLF
      if (len == 0) continue;       // skip empty lines (the LF of a CRLF)
      command[len++] = '\r';        // normalise to the '\r' sentinel used below
      command[len] = 0;
      break;
    }
    command[len++] = c;
    command[len] = 0;
    Serial.print(c);
  }
  if (len == sizeof(command)-1) {  // command buffer full
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    Serial.print('\n');
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_)
  // Double-click sends an advert; hold powers the SenseCAP Solar repeater off.
  int btn_ev = user_btn.check();

  // Diagnostics, printed unconditionally (not behind MESH_DEBUG) so they show in any build.
  // Edge-triggered: isPressed() is a level, so logging it directly floods while held.
  bool btn_pressed_now = user_btn.isPressed();
  if (btn_pressed_now != btn_was_pressed) {
    btn_was_pressed = btn_pressed_now;
    btn_active_until = millis() + 1000;
    // Serial.printf("btn: %s\n", btn_pressed_now ? "DOWN" : "UP");
  }
  if (btn_ev != BUTTON_EVENT_NONE) {
    btn_active_until = millis() + 1000;
    // Serial.printf("btn: event=%d (1=click 2=long 3=DOUBLE 4=triple)\n", btn_ev);
  }

  switch (btn_ev) {
    case BUTTON_EVENT_DOUBLE_CLICK:
      Serial.println("Sending advert...");
      the_mesh.sendSelfAdvertisement(1500, true);   // flood, same as the "advert" CLI command
      break;
    case BUTTON_EVENT_LONG_PRESS:
      Serial.println("Powering off...");
      board.powerOff();  // does not return
      break;
    default:
      break;
  }
#endif

  the_mesh.loop();
  sensors.loop();

#if ENV_INCLUDE_GPS == 1
  // First fix since boot: persist it and tell the mesh straight away. Latched consume-once, so
  // this is at most one flash write and one flood advert per boot -- the rate limit is structural.
  if (sensors.takeFirstFixEvent()) {
    NodePrefs* p = the_mesh.getNodePrefs();
    p->node_lat = sensors.node_lat;
    p->node_lon = sensors.node_lon;
    the_mesh.savePrefs();
    Serial.printf("GPS: first fix %.6f, %.6f - saved, advertising\n", p->node_lat, p->node_lon);
    the_mesh.sendSelfAdvertisement(1500, true);   // flood: discovery/position propagation
  }
#endif
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();

  bool btn_busy = false;
#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_)
  btn_busy = (btn_active_until != 0) && !the_mesh.millisHasNowPassed(btn_active_until);
#endif
  if (the_mesh.getNodePrefs()->powersaving_enabled && !the_mesh.hasPendingWork() && !btn_busy) {
#if defined(NRF52_PLATFORM)
    board.sleep(0); // nrf ignores seconds param, sleeps whenever possible
#else
    if (the_mesh.millisHasNowPassed(POWERSAVING_FIRSTSLEEP_SECS * 1000)) { // To check if it is time to sleep
      board.sleep(30); // Sleep. Wake up after a while or when receiving a LoRa packet
    }
#endif
  }
}
