#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <RCSwitch.h>
#include <DHTesp.h>
#include <ESP32Time.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "fan_rf.h"
#include "logic_commands.h"
#include "wifi_profiles.h"

ESP32Time rtc;

const char* ntpServer1 = "europe.pool.ntp.org";
const char* ntpServer2 = "ntp.roa.es";
const char* ntpServer3 = "ntp.rediris.es";
const char* timezone = "CET-1CEST,M3.5.0,M10.5.0/3";

const char* PARAM_INPUT_1 = "button";
const char* PARAM_TIMER_TIME = "time";
const char* PARAM_TIMER_HOURS = "hours";
const char* PARAM_TIMER_MINUTES = "minutes";
const char* PARAM_AUTO_ENABLED = "enabled";
const char* PARAM_AUTO_LOW = "low";
const char* PARAM_AUTO_MED = "med";
const char* PARAM_AUTO_HIGH = "high";
const char* PARAM_AUTO_HUMIDITY = "humidity";
const char* PARAM_RF_DEFAULT = "default";
const char* PARAM_RF_LOW = "low";
const char* PARAM_RF_MED = "med";
const char* PARAM_RF_HIGH = "high";
const char* PARAM_RF_OFF = "off";

RCSwitch mySwitch = RCSwitch();
DNSServer dnsServer;

const char *WIFI_SETUP_SSID = "FanControl-Setup";
const char *WIFI_SETUP_PASSWORD = "fancontrol";
const unsigned long wifiProfileAttemptTimeout = 8000;
const unsigned long wifiReconnectInterval = 30000;

#define DHTPIN 19 // Digital pin connected to the DHT sensor - GPIO12 = D6

// Uncomment the type of sensor in use:
#define DHTTYPE  DHTesp::DHT11 // DHT 11
//#define DHTTYPE    DHT22     // DHT 22 (AM2302)
//#define DHTTYPE    DHT21     // DHT 21 (AM2301)
#define dht_cal 2
DHTesp dht;


// current temperature & humidity, updated in loop()
float t = 0.0;
float h = 0.0;

unsigned long dhtpreviousMillis = 0; // will store last time DHT was updated
unsigned long timepreviousMillis = 0;

// Updates DHT readings every 10 seconds
const long dhtinterval = 30000;
const long timeinterval = 50000;
const unsigned int rfRepeatTransmit = 20;
const unsigned int rfLowRepeatTransmit = 30;
const unsigned long autoControlInterval = 60000;
const unsigned long autoMinChangeInterval = 180000;
const float autoHysteresis = 0.5;

QueueHandle_t logicCommandQueue = nullptr;
SemaphoreHandle_t stateMutex = nullptr;
const UBaseType_t LOGIC_QUEUE_LENGTH = 12;
const uint8_t NVS_DIRTY_AUTO = 1U << 0;
const uint8_t NVS_DIRTY_RF = 1U << 1;
const uint8_t NVS_DIRTY_FAN = 1U << 2;
const unsigned long NVS_WRITE_DELAY = 1000;
uint8_t pendingNvsWrites = 0;
unsigned long nvsDirtyAt = 0;

String local_time = "";
String local_date = "";

AsyncWebServer server(80);

struct WifiSettings
{
  bool configured = false;
  bool dhcp = true;
  String ssid = "";
  String password = "";
  IPAddress localIp;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns;
};

WifiSettings wifiSettings;
WifiProfileStore wifiStore = {};
int8_t activeWifiProfile = -1;
volatile int8_t pendingWifiProfile = -1;
uint8_t attemptedWifiProfiles = 0;
volatile bool wifiPortalActive = false;
bool wifiWasConnected = false;
bool mdnsStarted = false;
volatile bool wifiApplyPending = false;
volatile bool wifiFinishPending = false;
volatile bool wifiResetPending = false;
bool timeSynchronized = false;
volatile unsigned long wifiApplyAt = 0;
volatile unsigned long wifiFinishAt = 0;
volatile unsigned long wifiResetAt = 0;
unsigned long wifiConnectStartedAt = 0;
unsigned long wifiLastAttemptAt = 0;
unsigned long ntpLastAttemptAt = 0;
String wifiScanJson = "{\"networks\":[]}";
String wifiScanOptions = "<option value=\"\">No se encontraron redes</option>";
volatile bool wifiScanRequested = false;
bool wifiScanInProgress = false;

struct TimerState
{
  bool active = false;
  String mode = "";
  unsigned long startMillis = 0;
  unsigned long durationMillis = 0;
};

TimerState customTimer;

struct AutoSettings
{
  bool enabled = false;
  float lowTemp = 25.0;
  float medTemp = 27.0;
  float highTemp = 29.0;
  float humidityBoost = 65.0;
};

AutoSettings autoSettings;

struct RfSettings
{
  unsigned int defaultRepeat = rfRepeatTransmit;
  unsigned int lowRepeat = rfLowRepeatTransmit;
  unsigned int medRepeat = rfRepeatTransmit;
  unsigned int highRepeat = rfRepeatTransmit;
  unsigned int offRepeat = rfRepeatTransmit;
};

RfSettings rfSettings;
String currentFanMode = "off";
String autoFanMode = "off";
unsigned long autoPreviousMillis = 0;
unsigned long autoLastChangeMillis = 0;

unsigned int getRfRepeatForCommand(const String &command);

bool isFanControlCommand(const String &command)
{
  return command == "2h" || command == "4h" || command == "8h" || command == "high" || command == "med" || command == "low" || command == "off";
}

void lockState()
{
  if (stateMutex != nullptr)
  {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
  }
}

void unlockState()
{
  if (stateMutex != nullptr)
  {
    xSemaphoreGive(stateMutex);
  }
}

void clearCustomTimer()
{
  lockState();
  customTimer.active = false;
  customTimer.mode = "";
  customTimer.startMillis = 0;
  customTimer.durationMillis = 0;
  unlockState();
}

unsigned int normalizeRfRepeat(unsigned int repeat, unsigned int fallback)
{
  if (repeat < 1 || repeat > 80)
  {
    return fallback;
  }
  return repeat;
}

void loadAutoSettings()
{
  Preferences prefs;
  prefs.begin("fancontrol", true);
  autoSettings.enabled = prefs.getBool("auto_en", false);
  autoSettings.lowTemp = prefs.getFloat("auto_low", 25.0);
  autoSettings.medTemp = prefs.getFloat("auto_med", 27.0);
  autoSettings.highTemp = prefs.getFloat("auto_high", 29.0);
  autoSettings.humidityBoost = prefs.getFloat("auto_hum", 65.0);
  rfSettings.defaultRepeat = prefs.getUInt("rf_def", rfRepeatTransmit);
  rfSettings.lowRepeat = prefs.getUInt("rf_low", rfLowRepeatTransmit);
  rfSettings.medRepeat = prefs.getUInt("rf_med", rfRepeatTransmit);
  rfSettings.highRepeat = prefs.getUInt("rf_high", rfRepeatTransmit);
  rfSettings.offRepeat = prefs.getUInt("rf_off", rfRepeatTransmit);
  currentFanMode = prefs.getString("fan_mode", "off");
  prefs.end();

  rfSettings.defaultRepeat = normalizeRfRepeat(rfSettings.defaultRepeat, rfRepeatTransmit);
  rfSettings.lowRepeat = normalizeRfRepeat(rfSettings.lowRepeat, rfLowRepeatTransmit);
  rfSettings.medRepeat = normalizeRfRepeat(rfSettings.medRepeat, rfRepeatTransmit);
  rfSettings.highRepeat = normalizeRfRepeat(rfSettings.highRepeat, rfRepeatTransmit);
  rfSettings.offRepeat = normalizeRfRepeat(rfSettings.offRepeat, rfRepeatTransmit);
  if (currentFanMode != "low" && currentFanMode != "med" && currentFanMode != "high" && currentFanMode != "off")
  {
    currentFanMode = "off";
  }

  if (autoSettings.enabled)
  {
    autoFanMode = "";
  }
}

String escapeJson(const String &value)
{
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++)
  {
    char character = value.charAt(i);
    if (character == '\\' || character == '"')
    {
      escaped += '\\';
      escaped += character;
    }
    else if (character == '\n')
    {
      escaped += "\\n";
    }
    else if (character == '\r')
    {
      escaped += "\\r";
    }
    else if (static_cast<uint8_t>(character) >= 0x20)
    {
      escaped += character;
    }
  }
  return escaped;
}

String escapeHtml(const String &value)
{
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++)
  {
    char character = value.charAt(i);
    if (character == '&')
    {
      escaped += "&amp;";
    }
    else if (character == '<')
    {
      escaped += "&lt;";
    }
    else if (character == '>')
    {
      escaped += "&gt;";
    }
    else if (character == '"')
    {
      escaped += "&quot;";
    }
    else if (character == '\'')
    {
      escaped += "&#39;";
    }
    else
    {
      escaped += character;
    }
  }
  return escaped;
}

void buildWifiScanCache(int scanResult)
{
  if (scanResult < 0)
  {
    lockState();
    wifiScanJson = "{\"networks\":[]}";
    wifiScanOptions = "<option value=\"\">No se pudo escanear</option>";
    unlockState();
    WiFi.scanDelete();
    Serial.println("No se pudo escanear redes WiFi");
    return;
  }

  String scanJson = "{\"networks\":[";
  String scanOptions = "<option value=\"\">Selecciona una red</option>";
  scanJson.reserve(32 + scanResult * 96);
  scanOptions.reserve(64 + scanResult * 96);
  for (int i = 0; i < scanResult; i++)
  {
    String ssid = WiFi.SSID(i);
    if (i > 0)
    {
      scanJson += ',';
    }
    scanJson += "{\"ssid\":\"" + escapeJson(ssid) + "\",\"rssi\":" + String(WiFi.RSSI(i));
    scanJson += ",\"open\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";

    if (!ssid.isEmpty())
    {
      String safeSsid = escapeHtml(ssid);
      scanOptions += "<option value=\"" + safeSsid + "\">" + safeSsid + " (" + String(WiFi.RSSI(i)) + " dBm";
      scanOptions += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? ", abierta)</option>" : ")</option>";
    }
  }
  scanJson += "]}";
  lockState();
  wifiScanJson = scanJson;
  wifiScanOptions = scanOptions;
  unlockState();
  WiFi.scanDelete();
  Serial.println("Redes WiFi detectadas: " + String(scanResult));
}

void refreshWifiScanCache()
{
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(100);
  buildWifiScanCache(WiFi.scanNetworks(false, true));
}

void handleWifiScan()
{
  if (wifiScanRequested && !wifiScanInProgress)
  {
    wifiScanRequested = false;
    WiFi.scanDelete();
    int result = WiFi.scanNetworks(true, true);
    if (result == WIFI_SCAN_FAILED)
    {
      buildWifiScanCache(result);
      return;
    }
    wifiScanInProgress = true;
    Serial.println("Escaneo WiFi manual iniciado");
  }

  if (!wifiScanInProgress)
  {
    return;
  }

  int result = WiFi.scanComplete();
  if (result >= 0)
  {
    wifiScanInProgress = false;
    buildWifiScanCache(result);
  }
  else if (result == WIFI_SCAN_FAILED)
  {
    wifiScanInProgress = false;
    buildWifiScanCache(result);
  }
}

void loadWifiSettings()
{
  memset(&wifiStore, 0, sizeof(wifiStore));
  Preferences prefs;
  prefs.begin("fancontrol", true);
  bool storeLoaded = prefs.getBytesLength("wifi_profiles") == sizeof(wifiStore) &&
                     prefs.getBytes("wifi_profiles", &wifiStore, sizeof(wifiStore)) == sizeof(wifiStore) &&
                     wifiStore.magic == WIFI_STORE_MAGIC && wifiStore.version == WIFI_STORE_VERSION;

  bool legacyConfigured = prefs.getBool("wifi_set", false);
  String legacySsid = prefs.getString("wifi_ssid", "");
  String legacyPassword = prefs.getString("wifi_pass", "");
  bool legacyDhcp = prefs.getBool("wifi_dhcp", true);
  uint32_t legacyIp = prefs.getUInt("wifi_ip", 0);
  uint32_t legacyGateway = prefs.getUInt("wifi_gw", 0);
  uint32_t legacySubnet = prefs.getUInt("wifi_mask", 0);
  uint32_t legacyDns = prefs.getUInt("wifi_dns", 0);
  prefs.end();

  if (!storeLoaded)
  {
    memset(&wifiStore, 0, sizeof(wifiStore));
    wifiStore.magic = WIFI_STORE_MAGIC;
    wifiStore.version = WIFI_STORE_VERSION;
    wifiStore.lastSuccessful = -1;

    if (legacyConfigured && !legacySsid.isEmpty())
    {
      WifiProfileRecord &profile = wifiStore.profiles[0];
      profile.used = 1;
      profile.dhcp = legacyDhcp ? 1 : 0;
      strlcpy(profile.ssid, legacySsid.c_str(), sizeof(profile.ssid));
      strlcpy(profile.password, legacyPassword.c_str(), sizeof(profile.password));
      profile.localIp = legacyIp;
      profile.gateway = legacyGateway;
      profile.subnet = legacySubnet;
      profile.dns = legacyDns;
      wifiStore.lastSuccessful = 0;
    }

    prefs.begin("fancontrol", false);
    prefs.putBytes("wifi_profiles", &wifiStore, sizeof(wifiStore));
    prefs.end();
  }

  if (wifiStore.lastSuccessful < 0 || wifiStore.lastSuccessful >= MAX_WIFI_PROFILES ||
      !wifiStore.profiles[wifiStore.lastSuccessful].used)
  {
    wifiStore.lastSuccessful = -1;
  }

  activeWifiProfile = wifiStore.lastSuccessful;
  if (activeWifiProfile < 0)
  {
    for (uint8_t i = 0; i < MAX_WIFI_PROFILES; i++)
    {
      if (wifiStore.profiles[i].used)
      {
        activeWifiProfile = i;
        break;
      }
    }
  }
}

void saveWifiProfiles()
{
  lockState();
  WifiProfileStore store = wifiStore;
  unlockState();
  Preferences prefs;
  prefs.begin("fancontrol", false);
  prefs.putBytes("wifi_profiles", &store, sizeof(store));
  prefs.end();
}

uint8_t getWifiProfileCount()
{
  uint8_t count = 0;
  lockState();
  for (uint8_t i = 0; i < MAX_WIFI_PROFILES; i++)
  {
    count += wifiStore.profiles[i].used ? 1 : 0;
  }
  unlockState();
  return count;
}

bool isWifiProfileUsed(uint8_t index)
{
  if (index >= MAX_WIFI_PROFILES)
  {
    return false;
  }
  lockState();
  bool used = wifiStore.profiles[index].used;
  unlockState();
  return used;
}

String getWifiProfilePassword(uint8_t index)
{
  if (index >= MAX_WIFI_PROFILES)
  {
    return String();
  }
  lockState();
  String password = wifiStore.profiles[index].used ? wifiStore.profiles[index].password : "";
  unlockState();
  return password;
}

int8_t findWifiProfileBySsid(const String &ssid)
{
  int8_t result = -1;
  lockState();
  for (uint8_t i = 0; i < MAX_WIFI_PROFILES; i++)
  {
    if (wifiStore.profiles[i].used && ssid == wifiStore.profiles[i].ssid)
    {
      result = i;
      break;
    }
  }
  unlockState();
  return result;
}

int8_t findFreeWifiProfile()
{
  int8_t result = -1;
  lockState();
  for (uint8_t i = 0; i < MAX_WIFI_PROFILES; i++)
  {
    if (!wifiStore.profiles[i].used)
    {
      result = i;
      break;
    }
  }
  unlockState();
  return result;
}

bool parseWifiProfileIndex(const String &value, int8_t &index)
{
  if (value.length() != 1 || value.charAt(0) < '0' || value.charAt(0) >= '0' + MAX_WIFI_PROFILES)
  {
    return false;
  }
  index = value.charAt(0) - '0';
  return true;
}

void loadWifiProfile(uint8_t index)
{
  if (index >= MAX_WIFI_PROFILES)
  {
    lockState();
    wifiSettings = WifiSettings();
    activeWifiProfile = -1;
    unlockState();
    return;
  }

  lockState();
  if (!wifiStore.profiles[index].used)
  {
    wifiSettings = WifiSettings();
    activeWifiProfile = -1;
    unlockState();
    return;
  }
  WifiProfileRecord profile = wifiStore.profiles[index];
  wifiSettings.configured = true;
  wifiSettings.dhcp = profile.dhcp != 0;
  wifiSettings.ssid = profile.ssid;
  wifiSettings.password = profile.password;
  wifiSettings.localIp = IPAddress(profile.localIp);
  wifiSettings.gateway = IPAddress(profile.gateway);
  wifiSettings.subnet = IPAddress(profile.subnet);
  wifiSettings.dns = IPAddress(profile.dns);
  activeWifiProfile = index;
  unlockState();
}

void storeWifiProfile(uint8_t index, const WifiSettings &settings)
{
  lockState();
  WifiProfileRecord &profile = wifiStore.profiles[index];
  memset(&profile, 0, sizeof(profile));
  profile.used = 1;
  profile.dhcp = settings.dhcp ? 1 : 0;
  strlcpy(profile.ssid, settings.ssid.c_str(), sizeof(profile.ssid));
  strlcpy(profile.password, settings.password.c_str(), sizeof(profile.password));
  profile.localIp = static_cast<uint32_t>(settings.localIp);
  profile.gateway = static_cast<uint32_t>(settings.gateway);
  profile.subnet = static_cast<uint32_t>(settings.subnet);
  profile.dns = static_cast<uint32_t>(settings.dns);
  unlockState();
}

void startWifiPortal()
{
  if (wifiPortalActive)
  {
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(WIFI_SETUP_SSID, WIFI_SETUP_PASSWORD))
  {
    Serial.println("No se pudo iniciar el portal WiFi");
    return;
  }

  dnsServer.start(53, "*", WiFi.softAPIP());
  wifiPortalActive = true;
  Serial.print("Portal WiFi disponible en http://");
  Serial.println(WiFi.softAPIP());
}

void stopWifiPortal()
{
  if (!wifiPortalActive)
  {
    return;
  }

  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  wifiPortalActive = false;
  Serial.println("Portal WiFi cerrado");
}

void connectToConfiguredWifi()
{
  if (!wifiSettings.configured)
  {
    startWifiPortal();
    return;
  }

  WiFi.mode(wifiPortalActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.disconnect(false, false);

  if (wifiSettings.dhcp)
  {
    IPAddress unsetAddress(0, 0, 0, 0);
    WiFi.config(unsetAddress, unsetAddress, unsetAddress);
  }
  else if (!WiFi.config(wifiSettings.localIp, wifiSettings.gateway, wifiSettings.subnet, wifiSettings.dns))
  {
    Serial.println("No se pudo aplicar la configuracion IP estatica");
  }

  WiFi.begin(wifiSettings.ssid.c_str(), wifiSettings.password.c_str());
  wifiConnectStartedAt = millis();
  wifiLastAttemptAt = wifiConnectStartedAt;
  Serial.print("Conectando a ");
  Serial.println(wifiSettings.ssid);
}

int8_t findNextWifiProfile()
{
  int8_t result = -1;
  lockState();
  if (wifiStore.lastSuccessful >= 0 && wifiStore.lastSuccessful < MAX_WIFI_PROFILES &&
      wifiStore.profiles[wifiStore.lastSuccessful].used &&
      !(attemptedWifiProfiles & (1U << wifiStore.lastSuccessful)))
  {
    result = wifiStore.lastSuccessful;
  }
  else
  {
    for (uint8_t i = 0; i < MAX_WIFI_PROFILES; i++)
    {
      if (wifiStore.profiles[i].used && !(attemptedWifiProfiles & (1U << i)))
      {
        result = i;
        break;
      }
    }
  }
  unlockState();
  return result;
}

void connectToWifiProfile(uint8_t index)
{
  loadWifiProfile(index);
  attemptedWifiProfiles |= 1U << index;
  connectToConfiguredWifi();
}

void startWifiConnectionCycle()
{
  attemptedWifiProfiles = 0;
  int8_t nextProfile = findNextWifiProfile();
  if (nextProfile < 0)
  {
    wifiSettings = WifiSettings();
    activeWifiProfile = -1;
    WiFi.disconnect(false, false);
    startWifiPortal();
    return;
  }
  connectToWifiProfile(nextProfile);
}

String getWifiStatusJson()
{
  bool connected = WiFi.status() == WL_CONNECTED;
  lockState();
  WifiSettings settings = wifiSettings;
  int8_t activeProfile = activeWifiProfile;
  unlockState();
  String json = "{\"configured\":" + String(getWifiProfileCount() > 0 ? "true" : "false");
  json.reserve(384);
  json += ",\"connected\":" + String(connected ? "true" : "false");
  json += ",\"portalActive\":" + String(wifiPortalActive ? "true" : "false");
  json += ",\"mode\":\"" + String(settings.dhcp ? "dhcp" : "static") + "\"";
  json += ",\"ssid\":\"" + escapeJson(connected ? WiFi.SSID() : settings.ssid) + "\"";
  json += ",\"ip\":\"" + String(connected ? WiFi.localIP().toString() : "") + "\"";
  json += ",\"rssi\":" + String(connected ? WiFi.RSSI() : 0);
  json += ",\"profileCount\":" + String(getWifiProfileCount());
  json += ",\"activeProfile\":" + String(activeProfile);
  json += ",\"apSsid\":\"" + String(WIFI_SETUP_SSID) + "\"";
  json += ",\"apIp\":\"" + String(wifiPortalActive ? WiFi.softAPIP().toString() : "") + "\"";
  json += ",\"configuredIp\":\"" + settings.localIp.toString() + "\"";
  json += ",\"gateway\":\"" + settings.gateway.toString() + "\"";
  json += ",\"subnet\":\"" + settings.subnet.toString() + "\"";
  json += ",\"dns\":\"" + settings.dns.toString() + "\"}";
  return json;
}

bool isWifiPortalRequest(AsyncWebServerRequest *request)
{
  return wifiPortalActive && request->client()->localIP() == WiFi.softAPIP();
}

String getWifiProfilesHtml()
{
  lockState();
  WifiProfileStore store = wifiStore;
  int8_t activeProfile = activeWifiProfile;
  unlockState();

  uint8_t profileCount = 0;
  for (uint8_t i = 0; i < MAX_WIFI_PROFILES; i++)
  {
    profileCount += store.profiles[i].used ? 1 : 0;
  }
  if (profileCount == 0)
  {
    return "<p class=\"form-message\">No hay redes guardadas.</p>";
  }

  String html = "<div class=\"wifi-profile-list\">";
  html.reserve(128 + profileCount * 420);
  for (uint8_t i = 0; i < MAX_WIFI_PROFILES; i++)
  {
    const WifiProfileRecord &profile = store.profiles[i];
    if (!profile.used)
    {
      continue;
    }

    html += "<div class=\"wifi-profile-item\"><div><strong>" + escapeHtml(profile.ssid) + "</strong>";
    html += "<span>" + String(profile.dhcp ? "DHCP" : "IP estatica") + "</span>";
    if (activeProfile == i && WiFi.status() == WL_CONNECTED)
    {
      html += "<span class=\"profile-active\">Conectada</span>";
    }
    else if (store.lastSuccessful == i)
    {
      html += "<span>Ultima valida</span>";
    }
    html += "</div><div class=\"profile-actions\">";
    html += "<a class=\"update-button nav-link secondary-button\" href=\"/wifi?profile=" + String(i) + "\">Editar</a>";
    html += "<form class=\"wifi-delete-form\" method=\"post\" action=\"/wifi/profile/delete\">";
    html += "<input type=\"hidden\" name=\"profile\" value=\"" + String(i) + "\">";
    html += "<button class=\"update-button danger-button\" type=\"submit\">Eliminar</button></form></div></div>";
  }
  html += "</div>";
  return html;
}

String wifiPageProcessor(const String &var, int8_t editIndex)
{
  WifiProfileRecord editedProfile = {};
  lockState();
  bool editing = editIndex >= 0 && editIndex < MAX_WIFI_PROFILES && wifiStore.profiles[editIndex].used;
  if (editing)
  {
    editedProfile = wifiStore.profiles[editIndex];
  }
  unlockState();
  const WifiProfileRecord *profile = editing ? &editedProfile : nullptr;

  if (var == "WIFI_NETWORK_OPTIONS")
  {
    lockState();
    String options = wifiScanOptions;
    unlockState();
    return options;
  }
  if (var == "WIFI_CONNECTION")
  {
    return WiFi.status() == WL_CONNECTED ? "Conectado a " + escapeHtml(WiFi.SSID()) : "Sin conexion";
  }
  if (var == "WIFI_CURRENT_IP")
  {
    return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "--";
  }
  if (var == "WIFI_SSID")
  {
    return editing ? escapeHtml(profile->ssid) : String();
  }
  if (var == "WIFI_PROFILE_INDEX")
  {
    return editing ? String(editIndex) : "-1";
  }
  if (var == "WIFI_FORM_TITLE")
  {
    return editing ? "Editar red" : "Nueva red";
  }
  if (var == "WIFI_DHCP_CHECKED")
  {
    return !editing || profile->dhcp ? "checked" : String();
  }
  if (var == "WIFI_STATIC_CHECKED")
  {
    return editing && !profile->dhcp ? "checked" : String();
  }
  if (var == "WIFI_IP")
  {
    return editing && !profile->dhcp ? IPAddress(profile->localIp).toString() : String();
  }
  if (var == "WIFI_GATEWAY")
  {
    return editing && !profile->dhcp ? IPAddress(profile->gateway).toString() : String();
  }
  if (var == "WIFI_SUBNET")
  {
    return editing && !profile->dhcp ? IPAddress(profile->subnet).toString() : "255.255.255.0";
  }
  if (var == "WIFI_DNS")
  {
    return editing && !profile->dhcp ? IPAddress(profile->dns).toString() : String();
  }
  if (var == "WIFI_SAVED_PROFILES")
  {
    return getWifiProfilesHtml();
  }
  if (var == "WIFI_PROFILE_COUNT")
  {
    return String(getWifiProfileCount()) + "/" + String(MAX_WIFI_PROFILES);
  }
  if (var == "WIFI_SAVE_DISABLED")
  {
    return !editing && getWifiProfileCount() >= MAX_WIFI_PROFILES ? "disabled" : String();
  }
  if (var == "WIFI_FINISH_HIDDEN")
  {
    return WiFi.status() == WL_CONNECTED && wifiPortalActive ? String() : "hidden";
  }
  return String();
}

void sendWifiResultPage(AsyncWebServerRequest *request, const String &title, const String &message, bool refresh)
{
  String html = "<!DOCTYPE html><html lang=\"es\"><head><meta charset=\"utf-8\">";
  html.reserve(1024);
  html += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  if (refresh)
  {
    html += "<meta http-equiv=\"refresh\" content=\"4;url=/wifi\">";
  }
  html += "<title>Fan Control - WiFi</title><link rel=\"stylesheet\" href=\"/style.css?v=7\"></head>";
  html += "<body><div class=\"header\"><h2 class=\"header\">FAN CONTROL</h2></div>";
  html += "<main class=\"config-page\"><div class=\"base config-panel wifi-panel\"><section class=\"config-section\">";
  html += "<h4>" + escapeHtml(title) + "</h4><p class=\"form-message\">" + escapeHtml(message) + "</p>";
  html += "<div class=\"form-actions\"><a class=\"update-button nav-link\" href=\"/wifi\">Continuar</a></div>";
  html += "</section></div></main></body></html>";
  request->send(200, "text/html", html);
}

void handleWifi(unsigned long currentMillis)
{
  if (wifiPortalActive)
  {
    dnsServer.processNextRequest();
  }

  handleWifiScan();

  if (wifiResetPending && currentMillis - wifiResetAt >= 500)
  {
    wifiResetPending = false;
    wifiApplyPending = false;
    lockState();
    memset(&wifiStore, 0, sizeof(wifiStore));
    wifiStore.magic = WIFI_STORE_MAGIC;
    wifiStore.version = WIFI_STORE_VERSION;
    wifiStore.lastSuccessful = -1;
    wifiSettings = WifiSettings();
    activeWifiProfile = -1;
    unlockState();
    attemptedWifiProfiles = 0;
    saveWifiProfiles();
    WiFi.disconnect(false, false);
    startWifiPortal();
  }

  if (wifiApplyPending && currentMillis - wifiApplyAt >= 500)
  {
    wifiApplyPending = false;
    attemptedWifiProfiles = 0;
    if (pendingWifiProfile >= 0 && isWifiProfileUsed(pendingWifiProfile))
    {
      connectToWifiProfile(pendingWifiProfile);
    }
    else
    {
      startWifiConnectionCycle();
    }
    pendingWifiProfile = -1;
  }

  if (wifiFinishPending && currentMillis - wifiFinishAt >= 500)
  {
    wifiFinishPending = false;
    stopWifiPortal();
  }

  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected)
  {
    if (!wifiWasConnected)
    {
      wifiWasConnected = true;
      wifiConnectStartedAt = 0;
      Serial.print("WiFi conectado. IP: ");
      Serial.println(WiFi.localIP());

      lockState();
      bool updateLastSuccessful = activeWifiProfile >= 0 && wifiStore.lastSuccessful != activeWifiProfile;
      if (updateLastSuccessful)
      {
        wifiStore.lastSuccessful = activeWifiProfile;
      }
      unlockState();
      if (updateLastSuccessful)
      {
        saveWifiProfiles();
      }

      if (!mdnsStarted)
      {
        mdnsStarted = MDNS.begin("fancontrol");
        if (mdnsStarted)
        {
          MDNS.addService("http", "tcp", 80);
          Serial.println("mDNS disponible en http://fancontrol.local");
        }
        else
        {
          Serial.println("No se pudo iniciar mDNS; use la direccion IP");
        }
      }
    }

    if (!timeSynchronized && currentMillis - ntpLastAttemptAt >= 2000)
    {
      ntpLastAttemptAt = currentMillis;
      struct tm timeInfo;
      if (getLocalTime(&timeInfo, 10))
      {
        rtc.setTimeStruct(timeInfo);
        timeSynchronized = true;
      }
    }
    return;
  }

  if (wifiWasConnected)
  {
    wifiWasConnected = false;
    wifiConnectStartedAt = currentMillis;
    timeSynchronized = false;
    if (mdnsStarted)
    {
      MDNS.end();
      mdnsStarted = false;
    }
    Serial.println("Conexion WiFi perdida");
  }

  if (getWifiProfileCount() == 0)
  {
    startWifiPortal();
    return;
  }

  if (wifiConnectStartedAt == 0)
  {
    wifiConnectStartedAt = currentMillis;
  }

  if (!wifiScanInProgress && currentMillis - wifiConnectStartedAt >= wifiProfileAttemptTimeout)
  {
    int8_t nextProfile = findNextWifiProfile();
    if (nextProfile >= 0)
    {
      connectToWifiProfile(nextProfile);
    }
    else if (!wifiPortalActive)
    {
      startWifiPortal();
      wifiLastAttemptAt = currentMillis;
    }
  }

  if (!wifiApplyPending && !wifiScanInProgress && currentMillis - wifiLastAttemptAt >= wifiReconnectInterval)
  {
    startWifiConnectionCycle();
  }
}

void saveAutoSettings()
{
  pendingNvsWrites |= NVS_DIRTY_AUTO;
  nvsDirtyAt = millis();
}

void saveRfSettings()
{
  pendingNvsWrites |= NVS_DIRTY_RF;
  nvsDirtyAt = millis();
}

void saveFanState()
{
  pendingNvsWrites |= NVS_DIRTY_FAN;
  nvsDirtyAt = millis();
}

void flushPendingSettings(unsigned long currentMillis)
{
  if (pendingNvsWrites == 0 || currentMillis - nvsDirtyAt < NVS_WRITE_DELAY)
  {
    return;
  }

  lockState();
  uint8_t writes = pendingNvsWrites;
  pendingNvsWrites = 0;
  AutoSettings automatic = autoSettings;
  RfSettings rf = rfSettings;
  String fanMode = currentFanMode;
  unlockState();

  Preferences prefs;
  prefs.begin("fancontrol", false);
  if (writes & NVS_DIRTY_AUTO)
  {
    prefs.putBool("auto_en", automatic.enabled);
    prefs.putFloat("auto_low", automatic.lowTemp);
    prefs.putFloat("auto_med", automatic.medTemp);
    prefs.putFloat("auto_high", automatic.highTemp);
    prefs.putFloat("auto_hum", automatic.humidityBoost);
  }
  if (writes & NVS_DIRTY_RF)
  {
    prefs.putUInt("rf_def", rf.defaultRepeat);
    prefs.putUInt("rf_low", rf.lowRepeat);
    prefs.putUInt("rf_med", rf.medRepeat);
    prefs.putUInt("rf_high", rf.highRepeat);
    prefs.putUInt("rf_off", rf.offRepeat);
  }
  if (writes & NVS_DIRTY_FAN)
  {
    prefs.putString("fan_mode", fanMode);
  }
  prefs.end();
}

void setAutoModeEnabled(bool enabled)
{
  lockState();
  bool changed = autoSettings.enabled != enabled;
  autoSettings.enabled = enabled;
  autoPreviousMillis = 0;
  autoLastChangeMillis = 0;
  if (enabled)
  {
    autoFanMode = "";
  }
  else
  {
    autoFanMode = "off";
  }
  unlockState();
  if (changed)
  {
    saveAutoSettings();
  }
}

bool sendFanCommand(const String &command)
{
  const char *code = findFanRfCode(command);
  if (code == nullptr)
  {
    return false;
  }

  mySwitch.setRepeatTransmit(getRfRepeatForCommand(command));
  mySwitch.send(code);
  return true;
}

bool sendFanMode(const String &mode)
{
  if (!sendFanCommand(mode))
  {
    return false;
  }

  if (mode == "low" || mode == "med" || mode == "high" || mode == "off")
  {
    lockState();
    bool changed = currentFanMode != mode;
    currentFanMode = mode;
    unlockState();
    if (changed)
    {
      saveFanState();
    }
  }

  return true;
}

bool parseDuration(const String &timeValue, unsigned long &durationMillis)
{
  int separator = timeValue.indexOf(':');
  if (separator <= 0 || separator != timeValue.lastIndexOf(':'))
  {
    return false;
  }

  String hoursText = timeValue.substring(0, separator);
  String minutesText = timeValue.substring(separator + 1);
  if (hoursText.length() == 0 || minutesText.length() != 2)
  {
    return false;
  }

  for (unsigned int i = 0; i < timeValue.length(); i++)
  {
    char c = timeValue.charAt(i);
    if (c != ':' && !isDigit(c))
    {
      return false;
    }
  }

  int hours = hoursText.toInt();
  int minutes = minutesText.toInt();
  if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59)
  {
    return false;
  }

  unsigned long totalMinutes = (hours * 60UL) + minutes;
  if (totalMinutes == 0)
  {
    return false;
  }

  durationMillis = totalMinutes * 60UL * 1000UL;
  return true;
}

bool parseDuration(unsigned long hours, unsigned long minutes, unsigned long &durationMillis)
{
  if (hours > 23 || minutes > 59)
  {
    return false;
  }

  unsigned long totalMinutes = (hours * 60UL) + minutes;
  if (totalMinutes == 0)
  {
    return false;
  }

  durationMillis = totalMinutes * 60UL * 1000UL;
  return true;
}

bool parseUnsignedNumber(const String &value, unsigned long &number)
{
  if (value.length() == 0)
  {
    return false;
  }

  for (unsigned int i = 0; i < value.length(); i++)
  {
    if (!isDigit(value.charAt(i)))
    {
      return false;
    }
  }

  number = value.toInt();
  return true;
}

bool parseFloatNumber(const String &value, float &number)
{
  String normalized = value;
  normalized.replace(',', '.');

  if (normalized.length() == 0)
  {
    return false;
  }

  bool decimalPointSeen = false;
  for (unsigned int i = 0; i < normalized.length(); i++)
  {
    char c = normalized.charAt(i);
    if (c == '.')
    {
      if (decimalPointSeen)
      {
        return false;
      }
      decimalPointSeen = true;
    }
    else if (!isDigit(c))
    {
      return false;
    }
  }

  number = normalized.toFloat();
  return true;
}

String jsonBool(bool value)
{
  return value ? "true" : "false";
}

unsigned int getRfRepeatForCommand(const String &command)
{
  if (command == "low")
  {
    return rfSettings.lowRepeat;
  }
  if (command == "med")
  {
    return rfSettings.medRepeat;
  }
  if (command == "high")
  {
    return rfSettings.highRepeat;
  }
  if (command == "off")
  {
    return rfSettings.offRepeat;
  }
  return rfSettings.defaultRepeat;
}

bool validateRfRepeat(unsigned long repeat)
{
  return repeat >= 1 && repeat <= 80;
}

String getAutoSettingsJson()
{
  lockState();
  AutoSettings settings = autoSettings;
  String fanMode = currentFanMode;
  String automaticMode = autoFanMode;
  unlockState();
  String json;
  json.reserve(160);
  json = "{\"enabled\":" + jsonBool(settings.enabled);
  json += ",\"low\":" + String(settings.lowTemp, 1);
  json += ",\"med\":" + String(settings.medTemp, 1);
  json += ",\"high\":" + String(settings.highTemp, 1);
  json += ",\"humidity\":" + String(settings.humidityBoost, 1);
  json += ",\"currentMode\":\"" + fanMode + "\"";
  json += ",\"autoMode\":\"" + automaticMode + "\"}";
  return json;
}

String getRfSettingsJson()
{
  lockState();
  RfSettings settings = rfSettings;
  String fanMode = currentFanMode;
  unlockState();
  String json;
  json.reserve(128);
  json = "{\"default\":" + String(settings.defaultRepeat);
  json += ",\"low\":" + String(settings.lowRepeat);
  json += ",\"med\":" + String(settings.medRepeat);
  json += ",\"high\":" + String(settings.highRepeat);
  json += ",\"off\":" + String(settings.offRepeat);
  json += ",\"currentMode\":\"" + fanMode + "\"}";
  return json;
}

bool validateAutoSettings(float lowTemp, float medTemp, float highTemp, float humidityBoost)
{
  if (lowTemp < 10.0 || highTemp > 45.0 || humidityBoost < 30.0 || humidityBoost > 95.0)
  {
    return false;
  }

  return lowTemp < medTemp && medTemp < highTemp;
}

String increaseFanMode(const String &mode)
{
  if (mode == "off")
  {
    return "low";
  }
  if (mode == "low")
  {
    return "med";
  }
  if (mode == "med")
  {
    return "high";
  }
  return "high";
}

String calculateAutoFanMode()
{
  float lowThreshold = autoSettings.lowTemp;
  float medThreshold = autoSettings.medTemp;
  float highThreshold = autoSettings.highTemp;

  if (autoFanMode == "high")
  {
    highThreshold -= autoHysteresis;
  }
  else if (autoFanMode == "med")
  {
    medThreshold -= autoHysteresis;
  }
  else if (autoFanMode == "low")
  {
    lowThreshold -= autoHysteresis;
  }

  String targetMode = "off";
  if (t >= highThreshold)
  {
    targetMode = "high";
  }
  else if (t >= medThreshold)
  {
    targetMode = "med";
  }
  else if (t >= lowThreshold)
  {
    targetMode = "low";
  }

  if (h >= autoSettings.humidityBoost)
  {
    targetMode = increaseFanMode(targetMode);
  }

  return targetMode;
}

void runAutoControl(unsigned long currentMillis)
{
  if (!autoSettings.enabled || customTimer.active || isnan(t) || isnan(h))
  {
    return;
  }

  if (autoPreviousMillis != 0 && currentMillis - autoPreviousMillis < autoControlInterval)
  {
    return;
  }
  autoPreviousMillis = currentMillis;

  String targetMode = calculateAutoFanMode();
  if (targetMode == autoFanMode)
  {
    return;
  }

  if (autoLastChangeMillis != 0 && currentMillis - autoLastChangeMillis < autoMinChangeInterval)
  {
    return;
  }

  if (sendFanMode(targetMode))
  {
    lockState();
    autoFanMode = targetMode;
    autoLastChangeMillis = currentMillis;
    unlockState();
    Serial.println("Auto ventilador: " + targetMode);
  }
}

String formatDuration(unsigned long durationMillis)
{
  unsigned long totalMinutes = (durationMillis + 59999UL) / 60000UL;
  unsigned long hours = totalMinutes / 60UL;
  unsigned long minutes = totalMinutes % 60UL;
  String formatted = "";

  if (hours < 10)
  {
    formatted += "0";
  }
  formatted += String(hours);
  formatted += ":";
  if (minutes < 10)
  {
    formatted += "0";
  }
  formatted += String(minutes);

  return formatted;
}

String getTimerStatus()
{
  lockState();
  TimerState timer = customTimer;
  unlockState();
  if (!timer.active)
  {
    return "{\"active\":false}";
  }

  unsigned long elapsed = millis() - timer.startMillis;
  if (elapsed >= timer.durationMillis)
  {
    return "{\"active\":false,\"expired\":true}";
  }

  unsigned long remaining = timer.durationMillis - elapsed;
  unsigned long remainingSeconds = (remaining + 999UL) / 1000UL;
  return "{\"active\":true,\"mode\":\"" + timer.mode + "\",\"remaining\":\"" + formatDuration(remaining) +
    "\",\"remainingSeconds\":" + String(remainingSeconds) + "}";
}

String getDashboardStatusJson()
{
  lockState();
  String timeValue = local_time;
  String dateValue = local_date;
  float temperature = t;
  float humidity = h;
  unlockState();

  UBaseType_t queuedCommands = logicCommandQueue != nullptr ? uxQueueMessagesWaiting(logicCommandQueue) : 0;
  String json;
  json.reserve(512);
  json = "{\"time\":\"" + timeValue + "\",\"date\":\"" + dateValue + "\"";
  json += ",\"temperature\":" + String(isnan(temperature) ? "null" : String(temperature, 1));
  json += ",\"humidity\":" + String(isnan(humidity) ? "null" : String(humidity, 1));
  json += ",\"timer\":" + getTimerStatus();
  json += ",\"auto\":" + getAutoSettingsJson();
  json += ",\"queuedCommands\":" + String(queuedCommands) + "}";
  return json;
}

bool enqueueLogicCommand(const LogicCommand &command)
{
  return logicCommandQueue != nullptr && xQueueSend(logicCommandQueue, &command, 0) == pdTRUE;
}

void processLogicCommand(const LogicCommand &command)
{
  if (command.type == LogicCommandType::Fan)
  {
    String mode(command.mode);
    if (sendFanMode(mode) && isFanControlCommand(mode))
    {
      clearCustomTimer();
      setAutoModeEnabled(false);
    }
    return;
  }

  if (command.type == LogicCommandType::TimerCancel)
  {
    clearCustomTimer();
    Serial.println("Temporizador cancelado");
    return;
  }

  if (command.type == LogicCommandType::TimerStart)
  {
    String mode(command.mode);
    lockState();
    customTimer.active = true;
    customTimer.mode = mode;
    customTimer.startMillis = millis();
    customTimer.durationMillis = command.durationMillis;
    unlockState();
    setAutoModeEnabled(false);

    if (sendFanMode(mode))
    {
      Serial.println("Temporizador iniciado: " + mode);
    }
    else
    {
      clearCustomTimer();
      Serial.println("No se pudo iniciar el temporizador");
    }
    return;
  }

  if (command.type == LogicCommandType::AutoSettings)
  {
    lockState();
    bool changed = false;
    if (command.hasEnabled)
    {
      changed = changed || autoSettings.enabled != command.enabled;
      autoSettings.enabled = command.enabled;
      autoFanMode = command.enabled ? "" : "off";
      autoLastChangeMillis = 0;
    }
    if (command.hasThresholds)
    {
      changed = changed || autoSettings.lowTemp != command.lowTemp || autoSettings.medTemp != command.medTemp ||
        autoSettings.highTemp != command.highTemp || autoSettings.humidityBoost != command.humidityBoost;
      autoSettings.lowTemp = command.lowTemp;
      autoSettings.medTemp = command.medTemp;
      autoSettings.highTemp = command.highTemp;
      autoSettings.humidityBoost = command.humidityBoost;
    }
    autoPreviousMillis = 0;
    unlockState();
    clearCustomTimer();
    if (changed)
    {
      saveAutoSettings();
    }
    Serial.println("Auto ajustes aplicados: " + getAutoSettingsJson());
    return;
  }

  if (command.type == LogicCommandType::RfSettings)
  {
    lockState();
    bool changed = rfSettings.defaultRepeat != command.rfDefault || rfSettings.lowRepeat != command.rfLow ||
      rfSettings.medRepeat != command.rfMed || rfSettings.highRepeat != command.rfHigh || rfSettings.offRepeat != command.rfOff;
    rfSettings.defaultRepeat = command.rfDefault;
    rfSettings.lowRepeat = command.rfLow;
    rfSettings.medRepeat = command.rfMed;
    rfSettings.highRepeat = command.rfHigh;
    rfSettings.offRepeat = command.rfOff;
    unlockState();
    if (changed)
    {
      saveRfSettings();
    }
    Serial.println("RF ajustes aplicados: " + getRfSettingsJson());
  }
}

void processLogicCommands()
{
  LogicCommand command = {};
  if (logicCommandQueue != nullptr && xQueueReceive(logicCommandQueue, &command, 0) == pdTRUE)
  {
    if (command.type == LogicCommandType::Fan && strcmp(command.mode, "luz") != 0)
    {
      LogicCommand nextCommand = {};
      while (xQueuePeek(logicCommandQueue, &nextCommand, 0) == pdTRUE &&
             nextCommand.type == LogicCommandType::Fan && strcmp(nextCommand.mode, "luz") != 0)
      {
        xQueueReceive(logicCommandQueue, &command, 0);
      }
    }
    processLogicCommand(command);
  }
}

void setup() {
  stateMutex = xSemaphoreCreateMutex();
  rtc.setTime(1,30,45,26,5,1999);
  loadAutoSettings();
  loadWifiSettings();
  dht.setup(DHTPIN,DHTTYPE);
  t = dht.getTemperature() + dht_cal;
  h = dht.getHumidity();
  Serial.begin(115200);
  Serial.println("\nFan Control v0.2\n");
  logicCommandQueue = xQueueCreate(LOGIC_QUEUE_LENGTH, sizeof(LogicCommand));
  if (logicCommandQueue == nullptr)
  {
    Serial.println("No se pudo crear la cola de logica");
  }
  if (stateMutex == nullptr)
  {
    Serial.println("No se pudo crear el mutex de estado");
  }
  Serial.printf("Logica Arduino en nucleo %d\n", xPortGetCoreID());
  // Transmitter is connected to Arduino Pin #10  
  mySwitch.enableTransmit(18);
  /*  RF CODES
    LUZ   111101010001
    2H    111110100010
    4H    111110100100
    8H    111110101000
    HIGH  111101101001
    MED   111100100011
    LOW   111100010111
    OFF   111101000101
  */


  // Optional set protocol (default is 1, will work for most outlets)
  // mySwitch.setProtocol(2);

  // Optional set pulse length.
   mySwitch.setPulseLength(320);
  
  // Optional set number of transmission repetitions.
  mySwitch.setRepeatTransmit(rfSettings.defaultRepeat);

  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  Serial.println("\nIniciando WiFi");
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  refreshWifiScanCache();
  startWifiConnectionCycle();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if (isWifiPortalRequest(request))
    {
      request->redirect("/wifi");
      return;
    }
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request){
    int8_t editIndex = -1;
    if (request->hasParam("profile"))
    {
      int8_t candidate = -1;
      if (parseWifiProfileIndex(request->getParam("profile")->value(), candidate) && isWifiProfileUsed(candidate))
      {
        editIndex = candidate;
      }
    }
    request->send(LittleFS, "/wifi.html", String(), false, [editIndex](const String &var) {
      return wifiPageProcessor(var, editIndex);
    });
  });

  server.on("/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", getWifiStatusJson());
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });

  server.on("/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request){
    lockState();
    String scanJson = wifiScanJson;
    unlockState();
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", scanJson);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });

  server.on("/wifi/scan/start", HTTP_POST, [](AsyncWebServerRequest *request){
    wifiScanRequested = true;
    sendWifiResultPage(request, "Buscando redes", "El escaneo puede pausar brevemente el portal. La lista se actualizara al terminar.", true);
  });

  server.on("/wifi/save", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("password", true) || !request->hasParam("mode", true))
    {
      sendWifiResultPage(request, "Datos incompletos", "Faltan datos de red.", false);
      return;
    }

    String ssid = request->hasParam("ssid", true) ? request->getParam("ssid", true)->value() : "";
    String selectedSsid = request->hasParam("ssid_select", true) ? request->getParam("ssid_select", true)->value() : "";
    String password = request->getParam("password", true)->value();
    String mode = request->getParam("mode", true)->value();
    int8_t editIndex = -1;
    if (request->hasParam("profile", true))
    {
      int8_t candidate = -1;
      if (parseWifiProfileIndex(request->getParam("profile", true)->value(), candidate) && isWifiProfileUsed(candidate))
      {
        editIndex = candidate;
      }
    }
    ssid.trim();
    selectedSsid.trim();
    if (!selectedSsid.isEmpty())
    {
      ssid = selectedSsid;
    }

    if (ssid.isEmpty() || ssid.length() > 32 || (password.length() > 0 && (password.length() < 8 || password.length() > 63)))
    {
      sendWifiResultPage(request, "Datos no validos", "Revisa el SSID y la contrasena.", false);
      return;
    }
    if (mode != "dhcp" && mode != "static")
    {
      sendWifiResultPage(request, "Datos no validos", "El modo de asignacion IP no es valido.", false);
      return;
    }

    WifiSettings newSettings;
    newSettings.configured = true;
    newSettings.dhcp = mode == "dhcp";
    newSettings.ssid = ssid;

    int8_t duplicateIndex = findWifiProfileBySsid(ssid);
    if (duplicateIndex >= 0 && editIndex >= 0 && duplicateIndex != editIndex)
    {
      sendWifiResultPage(request, "Red duplicada", "Ya existe otro perfil con ese SSID.", false);
      return;
    }

    int8_t targetIndex = editIndex >= 0 ? editIndex : duplicateIndex;
    if (targetIndex < 0)
    {
      targetIndex = findFreeWifiProfile();
    }
    if (targetIndex < 0)
    {
      sendWifiResultPage(request, "Limite alcanzado", "Elimina una red antes de guardar otra.", false);
      return;
    }

    if (password.isEmpty() && isWifiProfileUsed(targetIndex))
    {
      newSettings.password = getWifiProfilePassword(targetIndex);
    }
    else
    {
      newSettings.password = password;
    }

    if (!newSettings.dhcp)
    {
      if (!request->hasParam("ip", true) || !request->hasParam("gateway", true) ||
          !request->hasParam("subnet", true) || !request->hasParam("dns", true) ||
          !newSettings.localIp.fromString(request->getParam("ip", true)->value()) ||
          !newSettings.gateway.fromString(request->getParam("gateway", true)->value()) ||
          !newSettings.subnet.fromString(request->getParam("subnet", true)->value()) ||
          !newSettings.dns.fromString(request->getParam("dns", true)->value()))
      {
        sendWifiResultPage(request, "Datos no validos", "Revisa la IP, puerta de enlace, mascara y DNS.", false);
        return;
      }
    }

    storeWifiProfile(targetIndex, newSettings);
    saveWifiProfiles();
    startWifiPortal();
    pendingWifiProfile = targetIndex;
    wifiApplyPending = true;
    wifiApplyAt = millis();
    sendWifiResultPage(request, "Conectando", "Configuracion guardada. Esperando una direccion IP...", true);
  });

  server.on("/wifi/profile/delete", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("profile", true))
    {
      sendWifiResultPage(request, "Perfil no valido", "No se ha indicado la red que se debe eliminar.", false);
      return;
    }

    int8_t index = -1;
    if (!parseWifiProfileIndex(request->getParam("profile", true)->value(), index) || !isWifiProfileUsed(index))
    {
      sendWifiResultPage(request, "Perfil no valido", "La red seleccionada no existe.", false);
      return;
    }

    lockState();
    bool deletingActiveProfile = activeWifiProfile == index;
    memset(&wifiStore.profiles[index], 0, sizeof(WifiProfileRecord));
    if (wifiStore.lastSuccessful == index)
    {
      wifiStore.lastSuccessful = -1;
    }
    unlockState();
    saveWifiProfiles();

    if (deletingActiveProfile)
    {
      startWifiPortal();
      pendingWifiProfile = -1;
      wifiApplyPending = true;
      wifiApplyAt = millis();
    }
    sendWifiResultPage(request, "Red eliminada", "El perfil WiFi se ha eliminado correctamente.", true);
  });

  server.on("/wifi/finish", HTTP_POST, [](AsyncWebServerRequest *request){
    if (WiFi.status() != WL_CONNECTED)
    {
      sendWifiResultPage(request, "Conexion pendiente", "El ESP32 aun no ha obtenido una direccion IP.", true);
      return;
    }
    wifiFinishPending = true;
    wifiFinishAt = millis();
    sendWifiResultPage(request, "Configuracion finalizada", "El punto de acceso se cerrara. Usa la IP mostrada para volver a entrar.", false);
  });

  server.on("/wifi/reset", HTTP_POST, [](AsyncWebServerRequest *request){
    wifiResetPending = true;
    wifiResetAt = millis();
    request->send(200, "application/json", "{\"reset\":true}");
  });

  const char *captivePortalPaths[] = {"/generate_204", "/hotspot-detect.html", "/connecttest.txt", "/ncsi.txt"};
  for (const char *path : captivePortalPaths)
  {
    server.on(path, HTTP_GET, [](AsyncWebServerRequest *request){
      request->redirect("http://192.168.4.1/wifi");
    });
  }

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/config.html", "text/html");
  });

  server.on("/config.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/config.html", "text/html");
  });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/style.css","text/css");
  });

  server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/app.js", "application/javascript");
  });

  server.on("/wifi.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/wifi.js", "application/javascript");
  });

  server.on("/localtime", HTTP_GET, [](AsyncWebServerRequest *request) {
    lockState();
    String value = local_time;
    unlockState();
    request->send(200, "text/plain", value);
  });

  server.on("/localdate", HTTP_GET, [](AsyncWebServerRequest *request) {
    lockState();
    String value = local_date;
    unlockState();
    request->send(200, "text/plain", value);
  });

  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request) {
    lockState();
    float value = t;
    unlockState();
    request->send(200, "text/plain", String(value));
  });
  
  server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request) {
    lockState();
    float value = h;
    unlockState();
    request->send(200, "text/plain", String(value));
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", getDashboardStatusJson());
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });

  server.on("/system/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    UBaseType_t queuedCommands = logicCommandQueue != nullptr ? uxQueueMessagesWaiting(logicCommandQueue) : 0;
    TaskHandle_t asyncTask = xTaskGetHandle("async_tcp");
    TaskHandle_t logicTask = xTaskGetHandle("loopTask");
    UBaseType_t asyncStackFree = asyncTask != nullptr ? uxTaskGetStackHighWaterMark(asyncTask) : 0;
    UBaseType_t logicStackFree = logicTask != nullptr ? uxTaskGetStackHighWaterMark(logicTask) : 0;
    String json = "{\"webCore\":" + String(xPortGetCoreID());
    json += ",\"logicCore\":" + String(ARDUINO_RUNNING_CORE);
    json += ",\"queuedCommands\":" + String(queuedCommands);
    json += ",\"queueCapacity\":" + String(LOGIC_QUEUE_LENGTH);
    json += ",\"asyncStackFree\":" + String(asyncStackFree);
    json += ",\"logicStackFree\":" + String(logicStackFree);
    json += ",\"freeHeap\":" + String(ESP.getFreeHeap()) + "}";
    request->send(200, "application/json", json);
  });

  server.on("/botones", HTTP_GET, [] (AsyncWebServerRequest *request) {
      if (!request->hasParam(PARAM_INPUT_1)) {
        request->send(400, "text/plain", "Missing button");
        return;
      }

      String inputMessage1 = request->getParam(PARAM_INPUT_1)->value();
      if (inputMessage1 != "luz" && !isFanControlCommand(inputMessage1))
      {
        request->send(400, "text/plain", "Invalid button");
        return;
      }

      LogicCommand command = {};
      command.type = LogicCommandType::Fan;
      strlcpy(command.mode, inputMessage1.c_str(), sizeof(command.mode));
      if (!enqueueLogicCommand(command))
      {
        request->send(503, "text/plain", "Logic queue full");
        return;
      }
      request->send(200, "text/plain", "OK");
    });

  server.on("/temporizador/status", HTTP_GET, [] (AsyncWebServerRequest *request) {
    request->send(200, "application/json", getTimerStatus());
  });

  server.on("/timer/status", HTTP_GET, [] (AsyncWebServerRequest *request) {
    request->send(200, "application/json", getTimerStatus());
  });

  server.on("/auto/status", HTTP_GET, [] (AsyncWebServerRequest *request) {
    request->send(200, "application/json", getAutoSettingsJson());
  });

  server.on("/rf/status", HTTP_GET, [] (AsyncWebServerRequest *request) {
    request->send(200, "application/json", getRfSettingsJson());
  });

  server.on("/rf", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if (!request->hasParam(PARAM_RF_DEFAULT) || !request->hasParam(PARAM_RF_LOW) ||
        !request->hasParam(PARAM_RF_MED) || !request->hasParam(PARAM_RF_HIGH) ||
        !request->hasParam(PARAM_RF_OFF))
    {
      request->send(400, "text/plain", "Missing RF settings");
      return;
    }

    unsigned long defaultRepeat = 0;
    unsigned long lowRepeat = 0;
    unsigned long medRepeat = 0;
    unsigned long highRepeat = 0;
    unsigned long offRepeat = 0;

    if (!parseUnsignedNumber(request->getParam(PARAM_RF_DEFAULT)->value(), defaultRepeat) ||
        !parseUnsignedNumber(request->getParam(PARAM_RF_LOW)->value(), lowRepeat) ||
        !parseUnsignedNumber(request->getParam(PARAM_RF_MED)->value(), medRepeat) ||
        !parseUnsignedNumber(request->getParam(PARAM_RF_HIGH)->value(), highRepeat) ||
        !parseUnsignedNumber(request->getParam(PARAM_RF_OFF)->value(), offRepeat) ||
        !validateRfRepeat(defaultRepeat) || !validateRfRepeat(lowRepeat) ||
        !validateRfRepeat(medRepeat) || !validateRfRepeat(highRepeat) ||
        !validateRfRepeat(offRepeat))
    {
      request->send(400, "text/plain", "Invalid RF settings");
      return;
    }

    LogicCommand command = {};
    command.type = LogicCommandType::RfSettings;
    command.rfDefault = defaultRepeat;
    command.rfLow = lowRepeat;
    command.rfMed = medRepeat;
    command.rfHigh = highRepeat;
    command.rfOff = offRepeat;
    if (!enqueueLogicCommand(command))
    {
      request->send(503, "text/plain", "Logic queue full");
      return;
    }
    request->send(200, "application/json", "{\"queued\":true}");
  });

  server.on("/auto", HTTP_GET, [] (AsyncWebServerRequest *request) {
    bool updated = false;
    LogicCommand command = {};
    command.type = LogicCommandType::AutoSettings;

    if (request->hasParam(PARAM_AUTO_ENABLED))
    {
      String enabled = request->getParam(PARAM_AUTO_ENABLED)->value();
      if (enabled == "1" || enabled == "true")
      {
        command.enabled = true;
      }
      else if (enabled == "0" || enabled == "false")
      {
        command.enabled = false;
      }
      else
      {
        request->send(400, "text/plain", "Invalid enabled value");
        return;
      }
      command.hasEnabled = true;
      updated = true;
    }

    if (request->hasParam(PARAM_AUTO_LOW) || request->hasParam(PARAM_AUTO_MED) || request->hasParam(PARAM_AUTO_HIGH) || request->hasParam(PARAM_AUTO_HUMIDITY))
    {
      if (!request->hasParam(PARAM_AUTO_LOW) || !request->hasParam(PARAM_AUTO_MED) || !request->hasParam(PARAM_AUTO_HIGH) || !request->hasParam(PARAM_AUTO_HUMIDITY))
      {
        request->send(400, "text/plain", "Missing thresholds");
        return;
      }

      float lowTemp = 0.0;
      float medTemp = 0.0;
      float highTemp = 0.0;
      float humidityBoost = 0.0;

      if (!parseFloatNumber(request->getParam(PARAM_AUTO_LOW)->value(), lowTemp) ||
          !parseFloatNumber(request->getParam(PARAM_AUTO_MED)->value(), medTemp) ||
          !parseFloatNumber(request->getParam(PARAM_AUTO_HIGH)->value(), highTemp) ||
          !parseFloatNumber(request->getParam(PARAM_AUTO_HUMIDITY)->value(), humidityBoost) ||
          !validateAutoSettings(lowTemp, medTemp, highTemp, humidityBoost))
      {
        request->send(400, "text/plain", "Invalid thresholds");
        return;
      }

      command.hasThresholds = true;
      command.lowTemp = lowTemp;
      command.medTemp = medTemp;
      command.highTemp = highTemp;
      command.humidityBoost = humidityBoost;
      updated = true;
    }

    if (!updated)
    {
      request->send(400, "text/plain", "No auto settings sent");
      return;
    }

    if (!enqueueLogicCommand(command))
    {
      request->send(503, "text/plain", "Logic queue full");
      return;
    }
    request->send(200, "application/json", "{\"queued\":true}");
  });

  server.on("/temporizador", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if (request->hasParam("cancel"))
    {
      LogicCommand command = {};
      command.type = LogicCommandType::TimerCancel;
      if (!enqueueLogicCommand(command))
      {
        request->send(503, "text/plain", "Logic queue full");
        return;
      }
      request->send(200, "text/plain", "Timer cancelled");
      return;
    }

    if (!request->hasParam(PARAM_INPUT_1))
    {
      request->send(400, "text/plain", "Missing button");
      return;
    }

    String mode = request->getParam(PARAM_INPUT_1)->value();
    String durationText = "";
    unsigned long durationMillis = 0;

    if (mode != "low" && mode != "med" && mode != "high")
    {
      request->send(400, "text/plain", "Invalid mode");
      return;
    }

    if (request->hasParam(PARAM_TIMER_HOURS) && request->hasParam(PARAM_TIMER_MINUTES))
    {
      unsigned long hours = 0;
      unsigned long minutes = 0;
      String hoursText = request->getParam(PARAM_TIMER_HOURS)->value();
      String minutesText = request->getParam(PARAM_TIMER_MINUTES)->value();

      if (!parseUnsignedNumber(hoursText, hours) || !parseUnsignedNumber(minutesText, minutes) || !parseDuration(hours, minutes, durationMillis))
      {
        request->send(400, "text/plain", "Invalid duration");
        return;
      }

      durationText = String(hours) + "h " + String(minutes) + "m";
    }
    else if (request->hasParam(PARAM_TIMER_TIME))
    {
      durationText = request->getParam(PARAM_TIMER_TIME)->value();

      if (!parseDuration(durationText, durationMillis))
      {
        request->send(400, "text/plain", "Invalid time");
        return;
      }
    }
    else
    {
      request->send(400, "text/plain", "Missing duration");
      return;
    }

    LogicCommand command = {};
    command.type = LogicCommandType::TimerStart;
    command.durationMillis = durationMillis;
    strlcpy(command.mode, mode.c_str(), sizeof(command.mode));
    if (!enqueueLogicCommand(command))
    {
      request->send(503, "text/plain", "Logic queue full");
      return;
    }
    request->send(200, "application/json", "{\"queued\":true}");
  });

  AsyncElegantOTA.begin(&server);
  server.onNotFound([](AsyncWebServerRequest *request){
    if (isWifiPortalRequest(request))
    {
      request->redirect("http://192.168.4.1/wifi");
      return;
    }
    request->send(404, "text/plain", "Not found");
  });
  server.begin();
  Serial.println("Servidor HTTP iniciado");

  configTzTime(timezone, ntpServer1, ntpServer2, ntpServer3);
  local_time = rtc.getTime("%H:%M");
  local_date = rtc.getTime("%d/%m/%Y");
}

void loop() {
  unsigned long currentMillis = millis();

  handleWifi(currentMillis);
  processLogicCommands();

  if (customTimer.active && currentMillis - customTimer.startMillis >= customTimer.durationMillis)
  {
    sendFanMode("off");
    clearCustomTimer();
    Serial.println("Temporizador finalizado: ventilador apagado");
  }

  runAutoControl(currentMillis);

  if (currentMillis - dhtpreviousMillis >= dhtinterval)
  {
    // save the last time you updated the DHT values
    dhtpreviousMillis = currentMillis;
    // Read temperature as Celsius (the default)
    float newT = dht.getTemperature() + dht_cal;
    // Read temperature as Fahrenheit (isFahrenheit = true)
    //float newT = dht.readTemperature(true);
    // if temperature read failed, don't change t value
    if (isnan(newT))
    {
      Serial.println("Failed to read from DHT sensor!");
    }
    else
    {
      lockState();
      t = newT;
      unlockState();
      Serial.println("Temperatura: "+String(t));
    }
    // Read Humidity
    float newH = dht.getHumidity();
    // if humidity read failed, don't change h value
    if (isnan(newH))
    {
      Serial.println("Failed to read from DHT sensor!");
    }
    else
    {
      lockState();
      h = newH;
      unlockState();
      Serial.println("Humedad: "+String(h));
    }
  }

  if (currentMillis - timepreviousMillis >= timeinterval)
  {
    timepreviousMillis = currentMillis;
    lockState();
    local_time = rtc.getTime("%H:%M");
    local_date = rtc.getTime("%d/%m/%Y");
    unlockState();
  }

  flushPendingSettings(currentMillis);

  delay(25);
}
