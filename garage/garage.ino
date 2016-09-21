#include <SPI.h>
#include <DHT.h> 
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Bounce2.h>
#include <ArduinoOTA.h>
#include <HomeWifi.h>
#include <LED.h>

#define DOOR D6 // D5 MAGNETIC SENSOR - Porte de garage
#define RELAY D1   // D6 RELAY - Porte de garage
#define DHTPIN D7 // D7 DHT22 - Garage

#define DOORBOUNCE 150 //Door bounce timer to correct for jumpy reed sensor in ms
#define DOORRELAYDELAY 500 //Door relay trigger duration in ms

#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321

// Callback function header
void callback(char* topic, byte* payload, unsigned int length);
void openGarage();
void wificonnect();
void mqttConnect();
void getVolt();
void getTemp();
void getDoor(bool forcePublish = false);
void ota();
void getHeap();
void pollSensors();
void getRSSI();

//Relay Output bits
#define RELAY_ON 1  // GPIO value to write to turn on attached relay
#define RELAY_OFF 0 // GPIO value to write to turn off attached relay

boolean oldValue = -1;
const String node_id = "garage";
const char* mqtt_server = "openhab.home.lan";
const char* sub_topic = "esp8266/garage/+";
const char* log_topic = "esp8266/garage/log";
char message_buff[100];
  
WiFiClient espClient;
PubSubClient client(mqtt_server, 1883, callback, espClient);
DHT dht(DHTPIN, DHTTYPE);
LED led(BUILTIN_LED);
Bounce debouncer = Bounce(); 

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

  // Then set relay pins in output mode
	pinMode(RELAY, OUTPUT);
  digitalWrite(RELAY,RELAY_OFF); // Ensure relay is off
  //pinMode(BUILTIN_LED, OUTPUT);
  //digitalWrite(BUILTIN_LED,!RELAY_OFF);
	
	// Init Door pin
  pinMode(DOOR,INPUT_PULLUP);
  debouncer.attach(DOOR); // Activate debounce on door
  debouncer.interval(DOORBOUNCE);
}

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
  }

}

void getVolt() {
  static char vstr[10];
  dtostrf(ESP.getVcc(), 5, 2, vstr);
  String topic = "esp8266/" + node_id + "/volt";
  client.publish(topic.c_str(), vstr);
}

void openGarage() {
  Serial.println("Opening Garage");
  led.on();
  digitalWrite(RELAY, RELAY_ON);
  delay(DOORRELAYDELAY);
  digitalWrite(RELAY, RELAY_OFF);
  led.off();
}

void callback(char* topic, byte* payload, unsigned int length) {
  int i = 0;
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    message_buff[i] = payload[i];
  }
  message_buff[i] = '\0';
  Serial.println();
  
  if (strcmp(topic,"esp8266/garage/manette") == 0) {
    if (strcmp(message_buff, "open") == 0) {
        openGarage();
    }
  }
  
  else if (strcmp(topic,"esp8266/garage/update") == 0) {
    if (strcmp(message_buff, "1") == 0) {
        getDoor(true);
    }
  }
  //free(message_buff);
}

void loop() {
  ArduinoOTA.handle();  // Listen to OTA events
  //connect mqtt
  mqttConnect(); // Connect/keepalive to mqtt server
  
  getDoor();
    
  // Fetch temperatures from Dallas sensors
  const unsigned long oneMinute = 60 * 1000UL;
  static unsigned long lastSampleTime = 0 - oneMinute;  // initialize such that a reading is due the first time through loop()
  unsigned long now = millis();
  if (now - lastSampleTime >= oneMinute) {
     lastSampleTime += oneMinute;
     pollSensors();
  }
}

void getDoor(bool forcePublish) {
  // Check for DOOR change value
  debouncer.update();
  int value = debouncer.read();
  //boolean tripped = digitalRead(DOOR) == HIGH; 
  if ((value != oldValue ) || forcePublish) {
    // MQTT: convert float to str
    String topic = "esp8266/" + node_id + "/garagedoor";
    client.publish(topic.c_str(), String(value,DEC).c_str());
    oldValue = value;
  }
}

void ota() {
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
      client.subscribe(sub_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      led.blink(5000,5);
      //delay(5000);
    }
  }
  client.loop(); // listen to subscriptions
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

void pollSensors() {
  Serial.print("Sampling sensors...");
  getTemp(); // Send temp and humidity
  getRSSI(); // Send WIFI RSSI
  //getVolt(); // Send Voltage
  getHeap(); // Send Free Ram
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
