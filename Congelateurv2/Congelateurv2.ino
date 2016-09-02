/*
  ESP12e
  - Connect GPIO16 to RST

  - Publish freeheap

*/

#define OTA
//#define THINGSPEAK
//#define DEBUG
#define MQTT
//#define BATTERY
/*
  #ifdef DEBUG
  #ifdef TELNET
    #define Serial.print(str) Serial.print(str); if (serverClient && serverClient.connected()) serverClient.print(str)
    #define Serial.println(str) Serial.println(str); if (serverClient && serverClient.connected()) serverClient.println(str)
  #else
    #define Serial.print(str) Serial.print(str)
    #define Serial.println(str) Serial.println(str)
  #endif
  #else
  #define Serial.print(str)
  #define Serial.println(str)
  #endif
*/
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <HomeWifi.h>
#include <ArduinoOTA.h>
#include <Bounce2.h>

// Define variables

const String node_id = "congelateur";
const int buttonPin = D3;     // the number of the pushbutton pin
int buttonState = 0;         // variable for reading the pushbutton status
Bounce debouncer = Bounce(); 

unsigned long previousMillis = 0;        // will store last time LED was updated
const unsigned long oneMinute = 2 * 60 * 1000;

#ifdef THINGSPEAK
//THINGSPEAK  - 1YS0ZNPLC4IFGZ8D -> Simon
String apiKey = "1YS0ZNPLC4IFGZ8D";
const char* server = "api.THINGSPEAK.com";
void postThingspeak(float t, float h);
WiFiClient thingspeak_client;
#endif


// Import and define DHT sensor
#include <DHT.h>
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
#define PINDHT D7
DHT dht(PINDHT, DHTTYPE);

// Define functions
void wificonnect();
void getTemp();
void getRSSI();
void getVolt();
void pollSensors();
void getHeap();
void ota();

#ifdef MQTT
#include <PubSubClient.h>
void mqttConnect();
void callback(char* topic, byte* payload, unsigned int length);
const char* mqtt_server = "openhab.home.lan";
// Define Pubsubclient
WiFiClient mqtt_client;
// Define MQTT client
PubSubClient client(mqtt_server, 1883, callback, mqtt_client);
#endif

void setup() {
  // Init console output
  Serial.begin(115200);
  Serial.println("Booting");

  // Init wifi connection
  Serial.println("Initializing Wifi...");
  wificonnect();

  ota();

  //Initialise DHT probe
  Serial.println("Initializing DHT22...");
  dht.begin();

  pinMode(buttonPin, INPUT);
  debouncer.attach(buttonPin);
  debouncer.interval(500);

  Serial.println("Setup completed.");
}

void loop() {
  ArduinoOTA.handle();  //Listen to OTA events
  debouncer.update();

  mqttConnect();

  const unsigned long oneMinute = 60 * 1000UL;
  static unsigned long lastSampleTime = 0 - oneMinute;  // initialize such that a reading is due the first time through loop()
  unsigned long now = millis();
  
  if (now - lastSampleTime >= oneMinute) {
    // save the last time you polled sensors
    lastSampleTime += oneMinute;
    pollSensors();
  }
  //button state changed
  if ( debouncer.fell() ) {
      pollSensors();     
   }
}

void pollSensors() {
  Serial.print("Sampling sensors...");
  getTemp(); // Send temp and humidity
  getRSSI(); // Send WIFI RSSI
  //getVolt(); // Send Voltage
  getHeap(); // Send Free Ram
}

void getHeap() {
  unsigned long freeheap = ESP.getFreeHeap();
  static char fhstr[10];

  Serial.printf("Free Heap RAM: %u", freeheap);
  ltoa(freeheap, fhstr, 10);
  //dtostrf(freeheap, 8, 0, fhstr);
  String topic;
  topic = "esp8266/" + node_id + "/freeheap";
  client.publish(topic.c_str(), fhstr);

}

void wificonnect() {
  Serial.println();
  Serial.println();
  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("WiFi connected: ");
  Serial.println(WiFi.localIP());
}


#ifdef MQTT
void mqttConnect() {
  // Loop until we're reconnected
  //if (!client.connected()) {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(node_id.c_str())) {
      Serial.println("Connected.");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
  //}
}

void callback(char* topic, byte* payload, unsigned int length) {
  //callback for mqtt
}
#endif

void getTemp() {
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

#ifdef THINGSPEAK
    // THINGSPEAK public
    postThingspeak(t, h);
#endif
  }

}

void getRSSI() {
  long rssi = WiFi.RSSI();
  Serial.print("RSSI: ");
  Serial.println(rssi);
#ifdef MQTT
  String topic;
  topic = "esp8266/" + node_id + "/rssi";
  static char rssistr[10];
  //dtostrf(rssi, 5, 2, rssistr);
  ltoa(rssi, rssistr, 10);
  client.publish(topic.c_str(), rssistr);
#endif
}

void getVolt() {
#ifdef BATTERY
  Serial.print("Volt: ");
  Serial.println(ESP.getVcc());
#ifdef MQTT
  static char vstr[10];
  dtostrf(ESP.getVcc(), 5, 2, vstr);
  String topic = "esp8266/" + node_id + "/volt";
  client.publish(topic.c_str(), vstr);
#endif
#endif
}

#ifdef THINGSPEAK
void postThingspeak(float t, float h) {
  if (thingspeak_client.connect(server, 80)) {
    String postStr = apiKey;
    postStr += "&field1=";
    postStr += String(t);
    postStr += "&field2=";
    postStr += String(h);
    postStr += "\r\n\r\n";

    thingspeak_client.print("POST /update HTTP/1.1\n");
    thingspeak_client.print("Host: api.THINGSPEAK.com\n");
    thingspeak_client.print("Connection: close\n");
    thingspeak_client.print("X-THINGSPEAKAPIKEY: " + apiKey + "\n");
    thingspeak_client.print("Content-Type: application/x-www-form-urlencoded\n");
    thingspeak_client.print("Content-Length: ");
    thingspeak_client.print(postStr.length());
    thingspeak_client.print("\n\n");
    thingspeak_client.print(postStr);
  }
}
#endif

void ota() {
    // Init OTA module
  Serial.print("Initializing OTA module...");
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
  Serial.println("OTA Ready");
}


