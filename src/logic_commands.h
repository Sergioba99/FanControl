#pragma once

#include <Arduino.h>

enum class LogicCommandType : uint8_t
{
  Fan,
  TimerStart,
  TimerCancel,
  AutoSettings,
  RfSettings
};

struct LogicCommand
{
  LogicCommandType type;
  char mode[8];
  unsigned long durationMillis;
  bool hasEnabled;
  bool enabled;
  bool hasThresholds;
  float lowTemp;
  float medTemp;
  float highTemp;
  float humidityBoost;
  unsigned int rfDefault;
  unsigned int rfLow;
  unsigned int rfMed;
  unsigned int rfHigh;
  unsigned int rfOff;
};
