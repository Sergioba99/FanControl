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

ESP32Time rtc;

const char* ntpServer1 = "europe.pool.ntp.org";
const char* ntpServer2 = "ntp.roa.es";
const char* ntpServer3 = "ntp.rediris.es";
const long gmtOffset_sec = 1*3600;
const int dayligthOffset_sec = 1*3600;

const char* PARAM_INPUT_1 = "button";

RCSwitch mySwitch = RCSwitch();
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

String local_time = "";
String local_date = "";

AsyncWebServer server(80);

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
  // mySwitch.setRepeatTransmit(15);

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

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/style.css","text/css");
  });

  server.on("/localtime", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", String(local_time).c_str());
  });

  server.on("/localdate", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", String(local_date).c_str());
  });

  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", String(t).c_str());
  });
  
  server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", String(h).c_str());
  });

  server.on("/botones", HTTP_GET, [] (AsyncWebServerRequest *request) {
      String inputMessage1;
      // GET input1 value on <ESP_IP>/update?output=<inputMessage1>&state=<inputMessage2>
      if (request->hasParam(PARAM_INPUT_1)) {
        inputMessage1 = request->getParam(PARAM_INPUT_1)->value();
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
        if (inputMessage1 == "luz"){
          mySwitch.send("111101010001");
        }
        else if (inputMessage1 == "2h")
        {
          mySwitch.send("111110100010");
        }
        else if (inputMessage1 == "4h")
        {
          mySwitch.send("111110100100");
        }
        else if (inputMessage1 == "8h")
        {
          mySwitch.send("111110101000");
        }
        else if (inputMessage1 == "high")
        {
          mySwitch.send("111101101001");
        }
        else if (inputMessage1 == "med")
        {
          mySwitch.send("111100100011");
        }
        else if (inputMessage1 == "low")
        {
          mySwitch.send("111100010111");
        }
        else if (inputMessage1 == "off")
        {
          mySwitch.send("111101000101");
        }
        
      }
      else {
        inputMessage1 = "No message sent";
      }
      Serial.println("Boton: " +String(inputMessage1));
      request->send(200, "text/plain", "OK");
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
    local_time = rtc.getTime("%H:%M");
    local_date = rtc.getTime("%d/%m/%Y");
  }

  if (currentMillis - wifiMillis >= wifiinterval)
  {
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
