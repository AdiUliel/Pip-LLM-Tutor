#pragma once
#include <Wire.h>
#include "pins.h"

// ─────────────────────────────────────────────────────────────────────────────
// ES8311 Audio Codec Driver — derived from official LCD Wiki example
// (Example_17_echo / Example_16_music / es8311.cpp)
//
// MCLK = 384 × 16000 = 6.144 MHz  (I2S_MCLK_MULTIPLE_384 in i2s_start_recording)
// BCLK = 6144000 / (bclk_div=4) = ... actually set by I2S driver automatically
// LRCK = 16000 Hz
// Slot width: 16-bit stereo (32 BCLK per LRCK)
// ─────────────────────────────────────────────────────────────────────────────

static void es8311_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool initES8311() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  // Verify codec is on the I2C bus
  Wire.beginTransmission(ES8311_I2C_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.println("[ES8311] Not found on I2C bus! Check wiring.");
    return false;
  }

  // ── Reset ─────────────────────────────────────────────────────────────────
  es8311_write(0x00, 0x1F);   // Soft reset all registers
  delay(20);
  es8311_write(0x00, 0x00);   // Clear reset
  es8311_write(0x00, 0x80);   // ★ Power-on command (was missing — critical!)

  // ── Clock: MCLK pin → 6.144 MHz (384 × 16 kHz) ───────────────────────────
  // Coefficients from official coeff_div table for {6144000, 16000}:
  //   pre_div=3, pre_multi=1, bclk_div=4, lrck=0x00/0xFF, osr=0x10
  es8311_write(0x01, 0x3F);   // ★ Enable ALL clocks, MCLK from MCLK pin (was 0x30)
  es8311_write(0x02, 0x48);   // ★ pre_div=3→(2<<5)=0x40, pre_multi=1→(1<<3)=0x08 (was 0x00)
  es8311_write(0x03, 0x10);   // fs_mode=0, adc_osr=0x10
  es8311_write(0x04, 0x10);   // dac_osr=0x10
  es8311_write(0x05, 0x00);   // adc_div=1, dac_div=1
  es8311_write(0x06, 0x03);   // bclk_div=4 → 4-1=3
  es8311_write(0x07, 0x00);   // lrck_h
  es8311_write(0x08, 0xFF);   // lrck_l

  // ── I2S format: 16-bit (ES8311_RESOLUTION_16 → 3<<2 = 0x0C) ─────────────
  es8311_write(0x09, 0x0C);   // ★ SDP In  (ADC→I2S) 16-bit I2S (was 0x00)
  es8311_write(0x0A, 0x0C);   // ★ SDP Out (I2S→DAC) 16-bit I2S (was 0x00)

  // ── Power up ──────────────────────────────────────────────────────────────
  es8311_write(0x0D, 0x01);   // Power up analog circuitry
  es8311_write(0x0E, 0x02);   // Enable analog PGA + ADC modulator
  es8311_write(0x12, 0x00);   // Power up DAC
  es8311_write(0x13, 0x10);   // ★ Enable output to HP drive (was 0x00)

  // ── Microphone (analog, not PDM) ──────────────────────────────────────────
  es8311_write(0x14, 0x11);   // Enable analog MIC, PGA gain reduced (bits[2:0]=1 → +3dB only)
  es8311_write(0x17, 0x80);   // ADC digital gain ~50% — reduce further if still clipping

  // ── ADC equalizer bypass + cancel DC offset ───────────────────────────────
  es8311_write(0x1C, 0x6A);   // ★ ADC EQ bypass, DC offset cancel (was 0x00)

  // ── DAC equalizer bypass ──────────────────────────────────────────────────
  es8311_write(0x37, 0x08);   // Bypass DAC EQ

  // ── DAC volume: 100% ──────────────────────────────────────────────────────
  es8311_write(0x32, 0xBF);

  Serial.println("[ES8311] Init OK");
  return true;
}
