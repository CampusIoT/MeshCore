#pragma once

#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>

class EnvironmentSensorManager : public SensorManager {
protected:
  static const int MAX_ACTIVE_SENSORS = 16;

  // Query function pointer + sub-channel index (for multi-channel sensors like INA3221).
  // Sub-channel is 0 for all single-output sensors.
  struct ActiveSensor {
    void    (*query)(uint8_t channel, uint8_t sub_channel, CayenneLPP& telemetry);
    uint8_t   sub_channel;
    const char* name;      // static literal from SENSOR_TABLE, for reporting
    uint8_t   address;
  };

  ActiveSensor _active_sensors[MAX_ACTIVE_SENSORS];
  int          _active_sensor_count = 0;
  uint8_t      next_available_channel = TELEM_CHANNEL_SELF + 1;

  // Bitmap of every address that ACKed on the last scan, one bit per 7-bit address.
  // Retained so the CLI can tell "bus is dead" apart from "bus is alive, nothing matched".
  uint8_t      _bus_seen[16] = {0};

  void initI2CSensors();                       // I2C bus scan + table-driven detection, without the GPS bring-up
  void queryI2CSensors(CayenneLPP& telemetry); // detected I2C sensors only, from next_available_channel up

  bool     gps_detected = false;
  bool     gps_active = false;
  uint32_t gps_update_interval_sec = 1;

  #if ENV_INCLUDE_GPS
  LocationProvider* _location;
  void start_gps();
  void stop_gps();
  void initBasicGPS();
  #ifdef RAK_BOARD
  void rakGPSInit();
  bool gpsIsAwake(uint8_t ioPin);
  #endif
  #endif

public:
  #if ENV_INCLUDE_GPS
  EnvironmentSensorManager(LocationProvider &location): _location(&location){};
  LocationProvider* getLocationProvider() { return _location; }
  #else
  EnvironmentSensorManager(){};
  #endif
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;

  bool rescanSensors() override;
  int getNumDetectedSensors() const override { return _active_sensor_count; }
  const char* getDetectedSensorName(int i) const override;
  uint8_t getDetectedSensorAddress(int i) const override;
  uint8_t getDetectedSensorChannel(int i) const override;
  int getBusAddresses(uint8_t dest[], int max_n) const override;
  #if ENV_INCLUDE_GPS || defined(ENV_INCLUDE_BME680_BSEC)
  void loop() override;
  #endif
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
};
