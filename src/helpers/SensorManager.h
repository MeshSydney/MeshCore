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

  // Returns true and sets volts/current/power from a system power sensor (e.g. INA219),
  // for boards whose own ADC cannot report a battery/system voltage (getBattMilliVolts() == 0).
  virtual bool getBackupBattReadings(float& volts, float& current, float& power) { return false; }

  // Resolves the millivolt reading to report as 'battery' (eg. companion app battery/storage
  // frame), falling back to a backup sensor reading when the board's own ADC reports 0mV.
  uint16_t getSelfMilliVolts(uint16_t board_millivolts) {
    if (board_millivolts != 0) return board_millivolts;
    float volts, current, power;
    return getBackupBattReadings(volts, current, power) ? (uint16_t)(volts * 1000.0f) : 0;
  }

  // Adds the 'self' voltage to report on TELEM_CHANNEL_SELF. Only when the board's own ADC
  // reports no voltage present (0mV) do we fall back to a backup sensor reading — and in that
  // case its current/power readings are reported here too, instead of on their own channel
  // (see EnvironmentSensorManager::querySensors(), which skips that sensor's own channel).
  void addSelfPower(CayenneLPP& telemetry, uint16_t board_millivolts) {
    if (board_millivolts != 0) {
      telemetry.addVoltage(TELEM_CHANNEL_SELF, board_millivolts / 1000.0f);
      return;
    }
    float volts, current, power;
    if (getBackupBattReadings(volts, current, power)) {
      telemetry.addVoltage(TELEM_CHANNEL_SELF, volts);
      telemetry.addCurrent(TELEM_CHANNEL_SELF, current);
      telemetry.addPower(TELEM_CHANNEL_SELF, power);
    } else {
      telemetry.addVoltage(TELEM_CHANNEL_SELF, 0.0f);
    }
  }

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
