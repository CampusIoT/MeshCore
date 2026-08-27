#pragma once

#include <CayenneLPP.h>
#include "sensors/LocationProvider.h"

#define TELEM_PERM_BASE         0x01   // 'base' permission includes battery
#define TELEM_PERM_LOCATION     0x02
#define TELEM_PERM_ENVIRONMENT  0x04   // permission to access environment sensors

#define TELEM_CHANNEL_SELF   1   // LPP data channel for 'self' device

class SensorManager {
public:
  double node_lat, node_lon;  // modify these, if you want to affect Advert location
  double node_altitude;       // altitude in meters

  SensorManager() { node_lat = 0; node_lon = 0; node_altitude = 0; }
  virtual bool begin() { return false; }
  virtual bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) { return false; }
  virtual void loop() { }
  virtual int getNumSettings() const { return 0; }
  virtual const char* getSettingName(int i) const { return NULL; }
  virtual const char* getSettingValue(int i) const { return NULL; }
  virtual bool setSettingValue(const char* name, const char* value) { return false; }
  virtual LocationProvider* getLocationProvider() { return NULL; }

  // Runtime I2C introspection. Detection normally runs once at boot, so without these the
  // only evidence a device was found is a MESH_DEBUG line no release build prints.
  // Managers with no I2C bus keep the defaults and the CLI reports "not supported".
  virtual bool rescanSensors() { return false; }          // re-scan the bus AND re-init drivers
  virtual int getNumDetectedSensors() const { return 0; }
  virtual const char* getDetectedSensorName(int i) const { return NULL; }
  virtual uint8_t getDetectedSensorAddress(int i) const { return 0; }
  virtual uint8_t getDetectedSensorChannel(int i) const { return 0; }
  // every address that ACKed, including ones no driver claimed. Returns the count.
  virtual int getBusAddresses(uint8_t dest[], int max_n) const { return 0; }

  // Helper functions to manage setting by keys (useful in many places ...)
  const char* getSettingByKey(const char* key) {
    int num = getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(getSettingName(i), key) == 0) {
        return getSettingValue(i);
      }
    }
    return NULL;
  }
};
