#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiMulti.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <RCSwitch.h>
#include <DHTesp.h>
#include <ESP32Time.h>
#include <Preferences.h>

ESP32Time rtc;

const char* ntpServer1 = "europe.pool.ntp.org";
const char* ntpServer2 = "ntp.roa.es";
const char* ntpServer3 = "ntp.rediris.es";
const long gmtOffset_sec = 1*3600;
const int dayligthOffset_sec = 1*3600;

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
Preferences preferences;
//Defnimos objeto multiwifi
WiFiMulti wifimulti;

// Credenciales de acceso de las diferentes redes
/*
const char *ssid1 = "MiFibra-1DAA";
const char *password1 = "yYjNz2V9";

*/
const char *ssid3 = "MiFibra-1DAA-V-IoT";
const char *password3 = "yYjNz2V9";

IPAddress local_IP(192, 168, 1, 148);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns (8,8,8,8);

const uint32_t TiempoEsperaWifi = 5000;

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

// Generally, you should use "unsigned long" for variables that hold time
// The value will quickly become too large for an int to store
unsigned long dhtpreviousMillis = 0; // will store last time DHT was updated
unsigned long timepreviousMillis = 0;
unsigned long wifiMillis = 0;

// Updates DHT readings every 10 seconds
const long dhtinterval = 30000;
const long timeinterval = 50000;
const long wifiinterval = 60000;
const unsigned int rfRepeatTransmit = 20;
const unsigned int rfLowRepeatTransmit = 30;
const unsigned long autoControlInterval = 60000;
const unsigned long autoMinChangeInterval = 180000;
const float autoHysteresis = 0.5;

String local_time = "";
String local_date = "";

AsyncWebServer server(80);

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

void clearCustomTimer()
{
  customTimer.active = false;
  customTimer.mode = "";
  customTimer.startMillis = 0;
  customTimer.durationMillis = 0;
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
  preferences.begin("fancontrol", true);
  autoSettings.enabled = preferences.getBool("auto_en", false);
  autoSettings.lowTemp = preferences.getFloat("auto_low", 25.0);
  autoSettings.medTemp = preferences.getFloat("auto_med", 27.0);
  autoSettings.highTemp = preferences.getFloat("auto_high", 29.0);
  autoSettings.humidityBoost = preferences.getFloat("auto_hum", 65.0);
  rfSettings.defaultRepeat = preferences.getUInt("rf_def", rfRepeatTransmit);
  rfSettings.lowRepeat = preferences.getUInt("rf_low", rfLowRepeatTransmit);
  rfSettings.medRepeat = preferences.getUInt("rf_med", rfRepeatTransmit);
  rfSettings.highRepeat = preferences.getUInt("rf_high", rfRepeatTransmit);
  rfSettings.offRepeat = preferences.getUInt("rf_off", rfRepeatTransmit);
  currentFanMode = preferences.getString("fan_mode", "off");
  preferences.end();

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

void saveAutoSettings()
{
  preferences.begin("fancontrol", false);
  preferences.putBool("auto_en", autoSettings.enabled);
  preferences.putFloat("auto_low", autoSettings.lowTemp);
  preferences.putFloat("auto_med", autoSettings.medTemp);
  preferences.putFloat("auto_high", autoSettings.highTemp);
  preferences.putFloat("auto_hum", autoSettings.humidityBoost);
  preferences.end();
}

void saveRfSettings()
{
  preferences.begin("fancontrol", false);
  preferences.putUInt("rf_def", rfSettings.defaultRepeat);
  preferences.putUInt("rf_low", rfSettings.lowRepeat);
  preferences.putUInt("rf_med", rfSettings.medRepeat);
  preferences.putUInt("rf_high", rfSettings.highRepeat);
  preferences.putUInt("rf_off", rfSettings.offRepeat);
  preferences.end();
}

void saveFanState()
{
  preferences.begin("fancontrol", false);
  preferences.putString("fan_mode", currentFanMode);
  preferences.putBool("auto_en", autoSettings.enabled);
  preferences.end();
}

void setAutoModeEnabled(bool enabled)
{
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
  saveAutoSettings();
}

bool sendFanCommand(const String &command)
{
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
  mySwitch.setRepeatTransmit(getRfRepeatForCommand(command));

  if (command == "luz"){
    mySwitch.send("111101010001");
  }
  else if (command == "2h")
  {
    mySwitch.send("111110100010");
  }
  else if (command == "4h")
  {
    mySwitch.send("111110100100");
  }
  else if (command == "8h")
  {
    mySwitch.send("111110101000");
  }
  else if (command == "high")
  {
    mySwitch.send("111101101001");
  }
  else if (command == "med")
  {
    mySwitch.send("111100100011");
  }
  else if (command == "low")
  {
    mySwitch.send("111100010111");
  }
  else if (command == "off")
  {
    mySwitch.send("111101000101");
  }
  else
  {
    return false;
  }

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
    currentFanMode = mode;
    saveFanState();
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
  return "{\"enabled\":" + jsonBool(autoSettings.enabled) +
    ",\"low\":" + String(autoSettings.lowTemp, 1) +
    ",\"med\":" + String(autoSettings.medTemp, 1) +
    ",\"high\":" + String(autoSettings.highTemp, 1) +
    ",\"humidity\":" + String(autoSettings.humidityBoost, 1) +
    ",\"currentMode\":\"" + currentFanMode + "\"" +
    ",\"autoMode\":\"" + autoFanMode + "\"}";
}

String getRfSettingsJson()
{
  return "{\"default\":" + String(rfSettings.defaultRepeat) +
    ",\"low\":" + String(rfSettings.lowRepeat) +
    ",\"med\":" + String(rfSettings.medRepeat) +
    ",\"high\":" + String(rfSettings.highRepeat) +
    ",\"off\":" + String(rfSettings.offRepeat) +
    ",\"currentMode\":\"" + currentFanMode + "\"}";
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
    autoFanMode = targetMode;
    autoLastChangeMillis = currentMillis;
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
  if (!customTimer.active)
  {
    return "{\"active\":false}";
  }

  unsigned long elapsed = millis() - customTimer.startMillis;
  if (elapsed >= customTimer.durationMillis)
  {
    return "{\"active\":false,\"expired\":true}";
  }

  unsigned long remaining = customTimer.durationMillis - elapsed;
  return "{\"active\":true,\"mode\":\"" + customTimer.mode + "\",\"remaining\":\"" + formatDuration(remaining) + "\"}";
}

// Replaces placeholder with DHT values
String processor(const String &var)
{
  //Serial.println(var);
  if (var == "TEMPERATURE")
  {
    return String(t);
  }
  else if (var == "HUMIDITY")
  {
    return String(h);
  }
  else if(var == "BUTTONPLACEHOLDER")
  {
    String buttons = "";
    buttons += "<div class = botones style=\"display: flex; justify-content: center;\"><table><tr>";
    buttons += "<th><button class=\"button\" id=\"luz\" onmousedown=\"toggleCheckboxButton(this);\">LUZ ON/OFF</button></th>";
    buttons += "<th><button class=\"button\" id=\"high\" onmousedown=\"toggleCheckboxButton(this);\">HIGH</button></th>";
    buttons += "</tr>";
    buttons += "<tr>";
    buttons += "<th><button class=\"button\" id=\"2h\" onmousedown=\"toggleCheckboxButton(this);\">2H</button></th>";
    buttons += "<th><button class=\"button\" id=\"med\" onmousedown=\"toggleCheckboxButton(this);\">MED</button></th>";
    buttons += "</tr>";
    buttons += "<tr>";
    buttons += "<th><button class=\"button\" id=\"4h\" onmousedown=\"toggleCheckboxButton(this);\">4H</button></th>";
    buttons += "<th><button class=\"button\" id=\"low\" onmousedown=\"toggleCheckboxButton(this);\">LOW</button></th>";
    buttons += "</tr>";
    buttons += "<tr>";
    buttons += "<th><button class=\"button\" id=\"8h\" onmousedown=\"toggleCheckboxButton(this);\">8H</button></th>";
    buttons += "<th><button class=\"button\" id=\"off\" onmousedown=\"toggleCheckboxButton(this);\">FAN OFF</button></th>";
    buttons += "</tr></table></div>";
    return buttons;
  }

  else if(var == "LOCALTIME")
  {
    return String(local_time);
  }

  else if(var == "LOCALDATE")
  {
    return String(local_date);
  }

  return String();
}

void setup() {
  rtc.setTime(1,30,45,26,5,1999);
  loadAutoSettings();
  dht.setup(DHTPIN,DHTTYPE);
  t = dht.getTemperature() + dht_cal;
  h = dht.getHumidity();
  Serial.begin(115200);
  Serial.println("\nFan Control v0.2\n");
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

  Serial.println("\nIniciando WIFI");
  
  if (!WiFi.config(local_IP, gateway, subnet, dns)) {
    Serial.println("STA Failed to configure");
  }

  //wifimulti.addAP(ssid1,password1);
  //wifimulti.addAP(ssid2,password2);
  wifimulti.addAP(ssid3,password3);



  WiFi.mode(WIFI_STA);
  Serial.print("Conectando a Wifi ..");
  while (wifimulti.run(TiempoEsperaWifi) != WL_CONNECTED) {
    Serial.print(".");
  }
  Serial.println(".. Conectado");
  Serial.print("SSID:");
  Serial.print(WiFi.SSID());
  Serial.print(" ID:");
  Serial.println(WiFi.localIP());

  if (!MDNS.begin("fancontrol")) {
    Serial.println("Error configurando mDNS!");
    while (1) {
      delay(1000);
      ESP.restart();
    }
  }
  Serial.println("mDNS configurado");

  Serial.println("HTTP server started");  

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", String(), false, processor);
  });

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/config.html", String(), false, processor);
  });

  server.on("/config.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/config.html", String(), false, processor);
  });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/style.css","text/css");
  });

  server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/app.js", "application/javascript");
  });

  server.on("/localtime", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", local_time);
  });

  server.on("/localdate", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", local_date);
  });

  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(t));
  });
  
  server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(h));
  });

  server.on("/botones", HTTP_GET, [] (AsyncWebServerRequest *request) {
      String inputMessage1;
      if (request->hasParam(PARAM_INPUT_1)) {
        inputMessage1 = request->getParam(PARAM_INPUT_1)->value();
        if (!sendFanMode(inputMessage1))
        {
          request->send(400, "text/plain", "Invalid button");
          return;
        }

        if (isFanControlCommand(inputMessage1))
        {
          clearCustomTimer();
          setAutoModeEnabled(false);
        }
      }
      else {
        request->send(400, "text/plain", "Missing button");
        return;
      }
      Serial.println("Boton: " +String(inputMessage1));
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

    rfSettings.defaultRepeat = defaultRepeat;
    rfSettings.lowRepeat = lowRepeat;
    rfSettings.medRepeat = medRepeat;
    rfSettings.highRepeat = highRepeat;
    rfSettings.offRepeat = offRepeat;
    saveRfSettings();

    Serial.println("RF ajustes: " + getRfSettingsJson());
    request->send(200, "application/json", getRfSettingsJson());
  });

  server.on("/auto", HTTP_GET, [] (AsyncWebServerRequest *request) {
    bool updated = false;

    if (request->hasParam(PARAM_AUTO_ENABLED))
    {
      String enabled = request->getParam(PARAM_AUTO_ENABLED)->value();
      if (enabled == "1" || enabled == "true")
      {
        autoSettings.enabled = true;
        autoFanMode = "";
        autoPreviousMillis = 0;
        autoLastChangeMillis = 0;
      }
      else if (enabled == "0" || enabled == "false")
      {
        autoSettings.enabled = false;
        autoFanMode = "off";
      }
      else
      {
        request->send(400, "text/plain", "Invalid enabled value");
        return;
      }
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

      autoSettings.lowTemp = lowTemp;
      autoSettings.medTemp = medTemp;
      autoSettings.highTemp = highTemp;
      autoSettings.humidityBoost = humidityBoost;
      autoPreviousMillis = 0;
      updated = true;
    }

    if (!updated)
    {
      request->send(400, "text/plain", "No auto settings sent");
      return;
    }

    clearCustomTimer();
    saveAutoSettings();
    Serial.println("Auto ajustes: " + getAutoSettingsJson());
    request->send(200, "application/json", getAutoSettingsJson());
  });

  server.on("/temporizador", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if (request->hasParam("cancel"))
    {
      clearCustomTimer();
      Serial.println("Temporizador cancelado");
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

    if (!sendFanMode(mode))
    {
      request->send(500, "text/plain", "Could not start fan");
      return;
    }

    customTimer.active = true;
    customTimer.mode = mode;
    customTimer.startMillis = millis();
    customTimer.durationMillis = durationMillis;
    setAutoModeEnabled(false);

    Serial.println("Temporizador: modo " + mode + " durante " + durationText);
    request->send(200, "application/json", getTimerStatus());
  });

  AsyncElegantOTA.begin(&server);

  MDNS.addService("http", "tcp", 80);

  
  server.begin();

  configTime(gmtOffset_sec, dayligthOffset_sec, ntpServer1, ntpServer2, ntpServer3);
  struct tm timeInfo;
  
  if (getLocalTime(&timeInfo))
  {
    Serial.println(&timeInfo, "%A, %B %d %Y %H:%M:%S");
    rtc.setTimeStruct(timeInfo);
  }
  else{
    Serial.println("Failed to obtain actual date");
  }


  local_time = rtc.getTime("%H:%M");
  local_date = rtc.getTime("%d/%m/%Y");
}

void loop() {
  unsigned long currentMillis = millis();

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
      t = newT;
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
      h = newH;
      Serial.println("Humedad: "+String(h));
    }
  }

  if (currentMillis - timepreviousMillis >= timeinterval)
  {
    timepreviousMillis = currentMillis;
    local_time = rtc.getTime("%H:%M");
    local_date = rtc.getTime("%d/%m/%Y");
  }

  if (currentMillis - wifiMillis >= wifiinterval)
  {
    wifiMillis = currentMillis;
    if(wifimulti.run(TiempoEsperaWifi) != WL_CONNECTED)
    {
      Serial.println("Conexion perdida");
      Serial.print("Conectando a Wifi ..");
      while (wifimulti.run(TiempoEsperaWifi) != WL_CONNECTED) {
        Serial.print(".");
      }
      Serial.println(".. Conectado");
      Serial.print("SSID:");
      Serial.print(WiFi.SSID());
      Serial.print(" ID:");
      Serial.println(WiFi.localIP());

      if (!MDNS.begin("fancontrol")) {
        Serial.println("Error configurando mDNS!");
        delay(1000);
        ESP.restart();
      }
      Serial.println("mDNS configurado");

      Serial.println("HTTP server started");  

    }
  }
  delay(500);
}
