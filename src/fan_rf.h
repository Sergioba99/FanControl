#pragma once

#include <Arduino.h>

struct FanRfCommand
{
  const char *name;
  const char *code;
};

constexpr FanRfCommand FAN_RF_COMMANDS[] = {
  {"luz", "111101010001"},
  {"2h", "111110100010"},
  {"4h", "111110100100"},
  {"8h", "111110101000"},
  {"high", "111101101001"},
  {"med", "111100100011"},
  {"low", "111100010111"},
  {"off", "111101000101"}
};

inline const char *findFanRfCode(const String &command)
{
  for (const FanRfCommand &definition : FAN_RF_COMMANDS)
  {
    if (command == definition.name)
    {
      return definition.code;
    }
  }
  return nullptr;
}
