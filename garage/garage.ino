#include <SPI.h>
#include <DHT.h> 
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Bounce2.h>
#include <ArduinoOTA.h>
#include <HomeWifi.h>

#define DOOR D6 // D5 MAGNETIC SENSOR - Porte de garage
#define RELAY D1   // D6 RELAY - Porte de garage
#define DHTPIN D7 // D7 DHT22 - Garage
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321

#define DOORBOUNCE 150 //Door bounce timer to correct for jumpy reed sensor in ms
#define DOORRELAYDELAY 500 //Door relay trigger duration in ms

unsigned long SLEEP_TIME = 30000; // Sleep time between reads (in milliseconds)
DHT dht(DHTPIN, DHTTYPE);
float lastTemp;
float lastHum;
boolean metric = true; 

//Relay Output bits
#define RELAY_ON 1  // GPIO value to write to turn on attached relay
#define RELAY_OFF 0 // GPIO value to write to turn off attached relay

boolean oldValue = -1;

const char* mqtt_server = "openhab.home.lan";

const String node_id = "garage";
const char* sub_topic = "esp8266/garage/+";
const char* log_topic = "esp8266/garage/log";
char message_buff[100];

// Callback function header
void callback(char* topic, byte* payload, unsigned int length);
void openGarage();
void wificonnect();
void reconnect();
void getVolt();
void getTemp();
void getDoor(bool forcePublish = false);
void ota();
String macToStr(const uint8_t* mac);

WiFiClient espClient;
PubSubClient client(espClient);


Bounce debouncer = Bounce(); 

void ota() {
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

void wificonnect() {
  Serial.println();
  Serial.println();
  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
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
    clientName += node_id;
    uint8_t mac[6];
    WiFi.macAddress(mac);
    clientName += "-";
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
      client.subscribe(sub_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup()
{
  Serial.begin(115200); // 19200 is the rate of communication
  // Setup MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
	// Startup Temp Sensors
	dht.begin();
	// Then set relay pins in output mode
	pinMode(RELAY, OUTPUT);
  pinMode(BUILTIN_LED, OUTPUT);
  //Ensure relay is off
  digitalWrite(RELAY,RELAY_OFF); //turn relay off
  digitalWrite(BUILTIN_LED,!RELAY_OFF);
	// Init Door pin
  pinMode(DOOR,INPUT_PULLUP);
  //digitalWrite(DOOR,HIGH);
	
	//Activate debounce on door
  debouncer.attach(DOOR);
  debouncer.interval(DOORBOUNCE);

  ota();
  wificonnect();
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

void openGarage() {
  Serial.println("Opening Garage");
  //trigger relay for 500ms 
  digitalWrite(RELAY, RELAY_ON);
  digitalWrite(BUILTIN_LED, !RELAY_ON);
  delay(DOORRELAYDELAY);
  digitalWrite(RELAY, RELAY_OFF);
  digitalWrite(BUILTIN_LED, !RELAY_OFF);
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

}

void loop()
{
  ArduinoOTA.handle();
  //connect mqtt
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  getDoor();
    
  // Fetch temperatures from Dallas sensors
  const unsigned long oneMinute = 60 * 1000UL;
  static unsigned long lastSampleTime = 0 - oneMinute;  // initialize such that a reading is due the first time through loop()
      
  unsigned long now = millis();
  if (now - lastSampleTime >= oneMinute) {
     lastSampleTime += oneMinute;
     getTemp();
     getVolt();
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

