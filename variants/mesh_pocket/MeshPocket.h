#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>

// built-ins
#define  PIN_VBAT_READ    29
#define  PIN_BAT_CTL      34
#define  MV_LSB   (3000.0F / 4096.0F) // 12-bit ADC with 3.0V input range

class HeltecMeshPocket : public NRF52BoardDCDC {
public:
  HeltecMeshPocket() : NRF52Board((char*)"MESH_POCKET_OTA") {}
  void begin();

  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    pinMode(PIN_BAT_CTL, OUTPUT);
    pinMode(PIN_VBAT_READ, INPUT);
    digitalWrite(PIN_BAT_CTL, HIGH);

    delay(10);
    uint32_t raw = 0;
    for (int i = 0; i < 8; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw /= 8;
    digitalWrite(PIN_BAT_CTL, LOW);

    return (uint16_t)((float)raw * MV_LSB * 4.96);  // 4.96 = 390k/100k divider + ~1.3% ADC correction
  }

  const char* getManufacturerName() const override {
    return "Heltec MeshPocket";
  }
};
