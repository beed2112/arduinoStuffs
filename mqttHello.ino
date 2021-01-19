//this was the base used
// https://learn.sparkfun.com/tutorials/introduction-to-mqtt/all
//
//could not get this example to compile
// https://raw.githubusercontent.com/RuiSantosdotme/Random-Nerd-Tutorials/master/Projects/ESP32-MQTT/ESP32_MQTT_Publish_Subscribe.ino
// try subscribe code.

#include <WiFi.h>
#include <PubSubClient.h>

const char *ssid =  "SSID";   // name of your WiFi network
const char *password =  "secretCode"; // password of the WiFi network

const int SWITCH_PIN = 2;           // Pin to control the light with  onboard blue led nodemcu
const char *ID = "esp32HW";  // Name of our device, must be unique
const char *TOPIC = "home/hello";  // Topic to subcribe to

IPAddress broker(192,168,11,0); // IP address of your MQTT broker eg. 192.168.1.50
WiFiClient wclient;

PubSubClient client(wclient); // Setup MQTT client
bool state=0;

// Connect to WiFi network
void setup_wifi() {
  Serial.print("\nConnecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password); // Connect to network

  while (WiFi.status() != WL_CONNECTED) { // Wait for connection
    digitalWrite(SWITCH_PIN,LOW);
    delay(500);
    Serial.print(".");
    digitalWrite(SWITCH_PIN,HIGH);
    delay(500);
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// Reconnect to client
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {

    digitalWrite(SWITCH_PIN,LOW);  // enable pull-up resistor (active low)
    Serial.print("Led Change....... ");
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(ID)) {
      Serial.println("connected");
      Serial.print("Publishing to: ");
      Serial.println(TOPIC);
      Serial.println('\n');

    } else {
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
   Serial.print("Led Change....... ");
   digitalWrite(SWITCH_PIN,HIGH);  // enable pull-up resistor (active low)
}

void setup() {
  Serial.begin(115200); // Start serial communication at 115200 baud
  pinMode(SWITCH_PIN,OUTPUT);  // Configure SWITCH_Pin as an input
  digitalWrite(SWITCH_PIN,HIGH);  // enable pull-up resistor (active low)
  delay(100);
   digitalWrite(SWITCH_PIN,LOW);  // enable pull-up resistor (active low)
  setup_wifi(); // Connect to network
  client.setServer(broker, 1883);
}

void loop() 
{
   //delay(2000);
  if (!client.connected())  // Reconnect if connection is lost
  {
    reconnect();
  }
  client.loop();

  // if the switch is being pressed
      // Wait 5 seconds before retrying
      delay(5000);
      client.publish(TOPIC, "DELAY");
      Serial.println((String)TOPIC + " => DELAY");

    state = !state; //toggle state
    if(state == 1) // ON
    {
     // client.publish(TOPIC, "on");
     // Serial.println((String)TOPIC + " => on");
      digitalWrite(SWITCH_PIN, LOW);
    }
    else // OFF
    {
     // client.publish(TOPIC, "off");
      //Serial.println((String)TOPIC + " => off");
      digitalWrite(SWITCH_PIN, HIGH);
    }

/*     while(digitalRead(SWITCH_PIN) == 0) // Wait for switch to be released
    {
      // Let the ESP handle some behind the scenes stuff if it needs to
      yield(); 
      delay(20);
    } */
  
}
