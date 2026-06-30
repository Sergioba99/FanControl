#pragma once

#include <Arduino.h>

constexpr uint8_t MAX_WIFI_PROFILES = 5;
constexpr uint32_t WIFI_STORE_MAGIC = 0x46435746;
constexpr uint8_t WIFI_STORE_VERSION = 1;

struct WifiProfileRecord
{
  uint8_t used;
  uint8_t dhcp;
  char ssid[33];
  char password[65];
  uint32_t localIp;
  uint32_t gateway;
  uint32_t subnet;
  uint32_t dns;
};

struct WifiProfileStore
{
  uint32_t magic;
  uint8_t version;
  int8_t lastSuccessful;
  uint8_t reserved[2];
  WifiProfileRecord profiles[MAX_WIFI_PROFILES];
};
