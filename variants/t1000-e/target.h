#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include "T1000eBoard.h"
#include <helpers/radiolib/CustomLR1110Wrapper.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#ifdef DISPLAY_CLASS
  #include "NullDisplayDriver.h"
#endif

// Derives from EnvironmentSensorManager for the external I2C sensor detection only. The GPS is
// NOT the generic one: the T1000-E needs its own power sequence over GPS_EN/GPS_VRTC_EN/
// GPS_RESET/GPS_SLEEP_INT, so begin() and loop() deliberately bypass the base implementations.
class T1000SensorManager: public EnvironmentSensorManager {
  void start_gps();
  void sleep_gps();
  void stop_gps();
public:
  T1000SensorManager(LocationProvider &nmea): EnvironmentSensorManager(nmea) { }
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
  void loop() override;
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
};

#ifdef DISPLAY_CLASS
  extern NullDisplayDriver display;
#endif

extern T1000eBoard board;
extern WRAPPER_CLASS radio_driver;
extern VolatileRTCClock rtc_clock;
extern T1000SensorManager sensors;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
