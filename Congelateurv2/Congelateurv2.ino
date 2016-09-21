#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <HomeWifi.h>
#include <ArduinoOTA.h>
#include <Bounce2.h>
#include <DHT.h>
#include <PubSubClient.h>
#include <LED.h>
#include <AnalogSmooth.h>

// Define functions
void wificonnect();
void getTemp();
void getRSSI();
void getVolt();
void pollSensors();
void getHeap();
void ota();
void mqttConnect();
void callback(char* topic, byte* payload, unsigned int length);
float get_smoothed_temp(float temp);

// Define variables
const String node_id = "congelateur";
const char* mqtt_server = "openhab.home.lan";
const int buttonPin = D3;     // the number of the pushbutton pin
int buttonState = 0;         // variable for reading the pushbutton status
Bounce debouncer = Bounce(); 

// define DHT sensor
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
#define PINDHT D7
DHT dht(PINDHT, DHTTYPE);

// define MQTT
WiFiClient espClient;
PubSubClient client(mqtt_server, 1883, callback, espClient);

LED led = LED(BUILTIN_LED);

AnalogSmooth smooth_temp = AnalogSmooth(3);

void setup() {
  // Initialise console output
  Serial.begin(115200);
  Serial.println("Booting");

  // Initialise wifi connection
  wificonnect();

  // Initialise OTA
  ota();

  // Initialise DHT probe
  Serial.println("Initializing DHT22...");
  dht.begin();

  // Initialise push button
  pinMode(buttonPin, INPUT);
  debouncer.attach(buttonPin);
  debouncer.interval(500);

  Serial.println("Setup completed.");
}

void loop() {
  ArduinoOTA.handle();  // Listen to OTA events
  debouncer.update(); // Listen to push button events

  mqttConnect(); // Connect/keepalive to mqtt server

  const unsigned long oneMinute = 60 * 1000UL;
  static unsigned long lastSampleTime = 0 - oneMinute;  // initialize such that a reading is due the first time through loop()
  unsigned long now = millis();
  
  if (now - lastSampleTime >= (oneMinute * 1.5)) {
    lastSampleTime += oneMinute; // save the last time you polled sensors
    pollSensors();
  }
  
  if ( debouncer.fell() ) { // if button state changed
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
  Serial.println("Initializing Wifi...");
  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);

  while (WiFi.status() != WL_CONNECTED) {
    led.blink(500,2);
    //delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("WiFi connected: ");
  Serial.println(WiFi.localIP());
}

void mqttConnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(node_id.c_str())) {
      Serial.println("Connected.");
      //client.subscribe(sub_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      led.blink(5000,5);
      //delay(5000);
    }
  }
  //client.loop(); // listen to subscriptions
}

void callback(char* topic, byte* payload, unsigned int length) {
  //callback for mqtt
}

void getTemp() {
  // Read temperature as Celsius
  float h = dht.readHumidity();
  float rt = dht.readTemperature();
  
  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(rt)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }
  else {
    float t = smooth_temp.smooth(rt);
    // Compute heat index in Celsius (isFahreheit = false)
    float hic = dht.computeHeatIndex(rt, h, false);
    Serial.print("Humidity: ");
    Serial.print(h);
    Serial.print(" %\t");
    Serial.print("Temperature: ");
    Serial.print(rt);
    Serial.print(" *C\t");
    Serial.print("Heat index: ");
    Serial.print(hic);
    Serial.print(" *C\t");
    Serial.print("VCC: ");
    Serial.print(ESP.getVcc());      //this is the raw reading in mV, should something like 3300 if powered by 3.3volts
    Serial.println(" V\t");

    // MQTT: convert float to str
    static char tstr[10];
    static char rtstr[10];
    static char hstr[10];
    static char htstr[10];

    dtostrf(rt, 5, 2, rtstr);
    dtostrf(t, 5, 2, tstr);
    dtostrf(h, 5, 2, hstr);
    dtostrf(hic, 5, 2, htstr);

    // MQTT: publish temp

    String topic;
    topic = "esp8266/" + node_id + "/temp";
    client.publish(topic.c_str(), tstr);
    topic = "esp8266/" + node_id + "/realtemp";
    client.publish(topic.c_str(), rtstr);
    topic = "esp8266/" + node_id + "/hum";
    client.publish(topic.c_str(), hstr);
    topic = "esp8266/" + node_id + "/heat";
    client.publish(topic.c_str(), htstr);
  }
}

void getRSSI() {
  long rssi = WiFi.RSSI();
  Serial.print("RSSI: ");
  Serial.println(rssi);
  String topic;
  topic = "esp8266/" + node_id + "/rssi";
  static char rssistr[10];
  //dtostrf(rssi, 5, 2, rssistr);
  ltoa(rssi, rssistr, 10);
  client.publish(topic.c_str(), rssistr);
}

void getVolt() {
  Serial.print("Volt: ");
  Serial.println(ESP.getVcc());
  static char vstr[10];
  dtostrf(ESP.getVcc(), 5, 2, vstr);
  String topic = "esp8266/" + node_id + "/volt";
  client.publish(topic.c_str(), vstr);
}

void ota() {
  // Init OTA module
  Serial.print("Initializing OTA module...");
  ArduinoOTA.setHostname((char*) node_id.c_str());
  ArduinoOTA.setPassword(ota_password);
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

