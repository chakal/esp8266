/*
  ESP12e
  - Connect GPIO16 to RST



*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <HomeWifi.h>
#include <DHT.h>
#include <PubSubClient.h>


#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
#define PIN0 0
#define PINDHT 2

void wificonnect();
void reconnect();
void getTemp();
void getVolt();
String macToStr(const uint8_t* mac);
void callback(char* topic, byte* payload, unsigned int length);

// Define variables
const char* mqtt_server = "openhab.home.lan";
const String node_id = "congelateur";

// Define Pubsubclient
WiFiClient espClient;
// Define Sensor
DHT dht(PINDHT, DHTTYPE);
// Define MQTT client
PubSubClient client(mqtt_server, 1883, callback, espClient);
// Define telnet console
//WiFiServer telnetServer(23);
//WiFiClient serverClient;

long lastMsg = 0;
char msg[50];
int value = 0;

uint32_t sleep_delay = 1 * 60;

int PIN0_val = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  Serial.println("Starting telnet server");
  /*
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  */
  Serial.println("Initializing...");

  //initialising wifi connection
  wificonnect();

  //initialise OTA module
  ArduinoOTA.setHostname((char*) node_id.c_str());
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");


  //Initialise DHT probe
  Serial.println("Initializing DHT22...");
  dht.begin();
  
  delay(100);
}

void loop() {
  //Listen to OTA events
  ArduinoOTA.handle();

  /*
  //Listen for telnet client
  if (telnetServer.hasClient()) {
    if (!serverClient || !serverClient.connected()) {
      if (serverClient) {
        serverClient.stop();
        Serial.println("Telnet Client Stop");
      }
      serverClient = telnetServer.available();
      Serial.println("New Telnet client");
      serverClient.flush();  // clear input buffer, else you get strange characters 
    }
  }
  */

  const unsigned long oneMinutes = 1 * 60 * 1000UL;
  static unsigned long lastSampleTime = 0 - oneMinutes;  // initialize such that a reading is due the first time through loop()
  unsigned long now = millis();
  
  if (now - lastSampleTime >= oneMinutes)
  {
    lastSampleTime += oneMinutes;
  
    //Reconnect to MQTT if !connected
    if (!client.connected()) {
      delay(10);
      reconnect();
    }
      
    Serial.print("Sampling temperature...");
    //Read and send temperatures
    getTemp();
    //Read and send voltage
    //getVolt();
    Serial.println("Done.");
  }
  //delay(100);
  //ESP.deepSleep(sleep_delay * 1000000);
  //delay(sleep_delay * 1000);
}


void wificonnect() {
  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to ");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  /*
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  */
  Serial.print("WiFi connected: ");
  Serial.println(WiFi.localIP());
}

String macToStr(const uint8_t* mac)
{
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    String clientName;
    clientName += "esp8266-";
    uint8_t mac[6];
    WiFi.macAddress(mac);
    clientName += macToStr(mac);
    clientName += "-";
    clientName += String(micros() & 0xff, 16);


    // Attempt to connect
    if (client.connect((char*) clientName.c_str())) {
      Serial.print("connected as ");
      Serial.println(clientName);
      // Once connected, publish an announcement...
      //client.publish("outTopic", "hello world");
      // ... and resubscribe
      //client.subscribe("inTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void getTemp() {

  //client.loop();

  // Read temperature as Celsius
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }
  else {
    // Compute heat index in Celsius (isFahreheit = false)
    float hic = dht.computeHeatIndex(t, h, false);
    Serial.print("Humidity: ");
    Serial.print(h);
    Serial.print(" %\t");
    Serial.print("Temperature: ");
    Serial.print(t);
    Serial.print(" *C\t");
    Serial.print("Heat index: ");
    Serial.print(hic);
    Serial.print(" *C\t");
    Serial.print("VCC: ");
    Serial.print(ESP.getVcc());      //this is the raw reading in mV, should something like 3300 if powered by 3.3volts
    Serial.println(" V\t");



    // MQTT: convert float to str
    static char tstr[10];
    static char hstr[10];
    static char htstr[10];

    dtostrf(t, 5, 2, tstr);
    dtostrf(h, 5, 2, hstr);
    dtostrf(hic, 5, 2, htstr);


    // MQTT: publish temp

    String topic;
    topic = "esp8266/" + node_id + "/temp";
    client.publish(topic.c_str(), tstr);
    topic = "esp8266/" + node_id + "/hum";
    client.publish(topic.c_str(), hstr);
    topic = "esp8266/" + node_id + "/heat";
    client.publish(topic.c_str(), htstr);

  }

}

void getVolt() {
  static char vstr[10];
  dtostrf(ESP.getVcc(), 5, 2, vstr);
  String topic = "esp8266/" + node_id + "/volt";
  client.publish(topic.c_str(), vstr);
}

void callback(char* topic, byte* payload, unsigned int length) {
  //callback for mqtt
}

