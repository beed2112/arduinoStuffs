
/*iGrill BLE Client
In order to setup the iGrill BLE Client or force the device back into configuration mode to update WiFi or MQTT settings you will need to 
press the reset button on the device twice to trigger the double reset detector.

You can configure the Number of seconds to wait for the second reset by changing the  DRD_TIMEOUT variable (Default 10s).

Once the device has booted up in configuration mode you will need to enter a password to enter the configuration/setup panel. 
From this configuration panel you can configure the device to connect to your home WiFi network as well as configure the MQTT Server Information.
NOTE: You can enter in information to connect to two wifi networks, and the device will automatically choose the one with the best signal.
      If you only have a single network you want to use this device from you can leave the second SSID and Password field empty.

The Credentials will then be saved into LittleFS / SPIFFS file and be used to connect to the MQTT Server and publish the iGrill Topics

************************************
* Configuration Portal Information *
************************************
Wifi SSID Name: iGrillClient_<ESP32_Chip_ID>
Wifi Password: igrill_client

NOTE: You can change this from the default by editing the code @ Line 58 before uploading to the device.
Wifi MQTT handling based on the example provided by the ESP_WifiManager Library for handling Wifi/MQTT Setup (https://github.com/khoih-prog/ESP_WiFiManager)
*/

#include "config.h" //iGrill BLE Client Configurable Settings
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h> //for MQTT
#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#include <ESP_WiFiManager.h> //https://github.com/khoih-prog/ESP_WiFiManager
//#include <BLEDevice.h>
#include <LITTLEFS.h> // https://github.com/lorol/LITTLEFS
FS* filesystem = &LITTLEFS;

const int BUTTON_PIN  = 27;
const int RED_LED     = 26;
const int BLUE_LED    = 25;

#include <ESP_DoubleResetDetector.h> //https://github.com/khoih-prog/ESP_DoubleResetDetector
DoubleResetDetector* drd = NULL;

uint32_t timer = millis();
const char* CONFIG_FILE = "/ConfigMQTT.json";

// Indicates whether ESP has WiFi credentials saved from previous session
bool initialConfig = false; //default false

//Setting up variables for MQTT Info in Config Portal
char custom_MQTT_SERVER[custom_MQTT_SERVER_LEN];
char custom_MQTT_SERVERPORT[custom_MQTT_PORT_LEN];
char custom_MQTT_USERNAME[custom_MQTT_USERNAME_LEN];
char custom_MQTT_PASSWORD[custom_MQTT_PASSWORD_LEN];
char custom_MQTT_BASETOPIC[custom_MQTT_BASETOPIC_LEN];

// SSID and PW for Config Portal
//String ssid = "esp32Base_" + String(ESP_getChipId(), HEX);
String ssid = "esp32Base_" + String((uint32_t)ESP.getEfuseMac(), HEX);
const char* password = "esp32Base_client";

// SSID and PW for your Router
String Router_SSID;
String Router_Pass;

typedef struct
{
  char wifi_ssid[SSID_MAX_LEN];
  char wifi_pw  [PASS_MAX_LEN];
}  WiFi_Credentials;

typedef struct
{
  String wifi_ssid;
  String wifi_pw;
}  WiFi_Credentials_String;

typedef struct
{
  WiFi_Credentials  WiFi_Creds [NUM_WIFI_CREDENTIALS];
} WM_Config;

WM_Config         WM_config;

// Use USE_DHCP_IP == true for dynamic DHCP IP, false to use static IP which you have to change accordingly to your network
#if (defined(USE_STATIC_IP_CONFIG_IN_CP) && !USE_STATIC_IP_CONFIG_IN_CP)
  // Force DHCP to be true
  #if defined(USE_DHCP_IP)
    #undef USE_DHCP_IP
  #endif
  #define USE_DHCP_IP     true
#else
  #define USE_DHCP_IP     true
#endif

#if ( USE_DHCP_IP || ( defined(USE_STATIC_IP_CONFIG_IN_CP) && !USE_STATIC_IP_CONFIG_IN_CP ) )
  // Use DHCP
  #warning Using DHCP IP
  IPAddress stationIP   = IPAddress(0, 0, 0, 0);
  IPAddress gatewayIP   = IPAddress(192, 168, 11, 1);
  IPAddress netMask     = IPAddress(255, 255, 255, 0);
#else
  // Use static IP
  #warning Using static IP
  IPAddress stationIP   = IPAddress(192, 168, 2, 232);
  IPAddress gatewayIP   = IPAddress(192, 168, 2, 1);
  IPAddress netMask     = IPAddress(255, 255, 255, 0);
#endif

IPAddress dns1IP      = gatewayIP;
IPAddress dns2IP      = IPAddress(8, 8, 8, 8);

IPAddress APStaticIP  = IPAddress(192, 168, 100, 1);
IPAddress APStaticGW  = IPAddress(192, 168, 100, 1);
IPAddress APStaticSN  = IPAddress(255, 255, 255, 0);

WiFi_AP_IPConfig  WM_AP_IPconfig;
WiFi_STA_IPConfig WM_STA_IPconfig;

// Create an ESP32 WiFiClient class to connect to the MQTT server
WiFiClient *client = NULL;
PubSubClient *mqtt_client = NULL;

// beedlow
void callback(char* topic, byte* payload, unsigned int length) {
  char inMessageHold;

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i=0;i<length;i++) {
    inMessageHold += payload[i];
    Serial.print((char)payload[i]);
  }
  Serial.println();
  
  float inTemp = float(inMessageHold);
  int inTempInt = int(inMessageHold);
  //String(inMessageHold).c_str()

  publishReceivedTemp(inTemp);
  
  String inMessageString = String(inTemp,0);
  publishReceivedTemp2(inMessageString);
  publishReceivedTemp3(inTempInt);
  publishReceivedTemp4(inMessageHold);


   
   

}


//iGrill BLE Client Logging Function
void IGRILLLOGGER(String logMsg, int requiredLVL)
{
  if(requiredLVL <= IGRILL_DEBUG_LVL)
    Serial.printf("[II] %s\n", logMsg.c_str());
}
#pragma region WIFI_Fuctions

void initAPIPConfigStruct(WiFi_AP_IPConfig &in_WM_AP_IPconfig)
{
  in_WM_AP_IPconfig._ap_static_ip   = APStaticIP;
  in_WM_AP_IPconfig._ap_static_gw   = APStaticGW;
  in_WM_AP_IPconfig._ap_static_sn   = APStaticSN;
}

void initSTAIPConfigStruct(WiFi_STA_IPConfig &in_WM_STA_IPconfig)
{
  in_WM_STA_IPconfig._sta_static_ip   = stationIP;
  in_WM_STA_IPconfig._sta_static_gw   = gatewayIP;
  in_WM_STA_IPconfig._sta_static_sn   = netMask;
#if USE_CONFIGURABLE_DNS  
  in_WM_STA_IPconfig._sta_static_dns1 = dns1IP;
  in_WM_STA_IPconfig._sta_static_dns2 = dns2IP;
#endif
}

void displayIPConfigStruct(WiFi_STA_IPConfig in_WM_STA_IPconfig)
{
  LOGERROR3(F("stationIP ="), in_WM_STA_IPconfig._sta_static_ip, ", gatewayIP =", in_WM_STA_IPconfig._sta_static_gw);
  LOGERROR1(F("netMask ="), in_WM_STA_IPconfig._sta_static_sn);
#if USE_CONFIGURABLE_DNS
  LOGERROR3(F("dns1IP ="), in_WM_STA_IPconfig._sta_static_dns1, ", dns2IP =", in_WM_STA_IPconfig._sta_static_dns2);
#endif
}

void configWiFi(WiFi_STA_IPConfig in_WM_STA_IPconfig)
{
  #if USE_CONFIGURABLE_DNS  
    // Set static IP, Gateway, Subnetmask, DNS1 and DNS2. New in v1.0.5
    WiFi.config(in_WM_STA_IPconfig._sta_static_ip, in_WM_STA_IPconfig._sta_static_gw, in_WM_STA_IPconfig._sta_static_sn, in_WM_STA_IPconfig._sta_static_dns1, in_WM_STA_IPconfig._sta_static_dns2);  
  #else
    // Set static IP, Gateway, Subnetmask, Use auto DNS1 and DNS2.
    WiFi.config(in_WM_STA_IPconfig._sta_static_ip, in_WM_STA_IPconfig._sta_static_gw, in_WM_STA_IPconfig._sta_static_sn);
  #endif 
}

uint8_t connectMultiWiFi()
{
  uint8_t status;
  LOGERROR(F("ConnectMultiWiFi with :"));
  if ( (Router_SSID != "") && (Router_Pass != "") )
  {
    LOGERROR3(F("* Flash-stored Router_SSID = "), Router_SSID, F(", Router_Pass = "), Router_Pass );
  }
  for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
  {
    // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
    if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
    {
      LOGERROR3(F("* Additional SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
    }
  }
  LOGERROR(F("Connecting MultiWifi..."));
  WiFi.mode(WIFI_STA);

#if !USE_DHCP_IP
  configWiFi(WM_STA_IPconfig);
#endif

  int i = 0;
  status = wifiMulti.run();
  delay(WIFI_MULTI_1ST_CONNECT_WAITING_MS);

  while ( ( i++ < 20 ) && ( status != WL_CONNECTED ) )
  {
    status = wifiMulti.run();

    if ( status == WL_CONNECTED )
      break;
    else
      delay(WIFI_MULTI_CONNECT_WAITING_MS);
  }

  if ( status == WL_CONNECTED )
  {
    LOGERROR1(F("WiFi connected after time: "), i);
    LOGERROR3(F("SSID:"), WiFi.SSID(), F(",RSSI="), WiFi.RSSI());
    LOGERROR3(F("Channel:"), WiFi.channel(), F(",IP address:"), WiFi.localIP() );
  }
  else
    LOGERROR(F("WiFi not connected"));
  return status;
}

void heartBeatPrint()
{
  static int num = 1;
  if(mqtt_client) //We have to check and see if we have a mqtt client created as this doesnt happen until the device is connected to an igrill device.
  {
    if(!mqtt_client->connected())
    {
      IGRILLLOGGER("MQTT Disconnected",2);
      connectMQTT();
    }
  }

}

void check_WiFi()
{
  if ( (WiFi.status() != WL_CONNECTED) )
  {
    IGRILLLOGGER("WiFi lost. Call connectMultiWiFi in loop",0);
    disconnectMQTT();
    connectMultiWiFi();
  }
}

void wifi_manager()
{
  IGRILLLOGGER("\nConfig Portal requested.", 0);
  digitalWrite(LED_BUILTIN, LED_ON); // turn the LED on by making the voltage LOW to tell us we are in configuration mode.
  ESP_WiFiManager ESP_wifiManager("ESP32_BaseClient");
  
  
  IGRILLLOGGER("Opening Configuration Portal. ", 0);
  Router_SSID = ESP_wifiManager.WiFi_SSID();
  Router_Pass = ESP_wifiManager.WiFi_Pass();
  //Check if there is stored WiFi router/password credentials.
  if ( !initialConfig && (Router_SSID != "") && (Router_Pass != "") )
  {
    //If valid AP credential and not DRD, set timeout 120s.
    ESP_wifiManager.setConfigPortalTimeout(120);
    IGRILLLOGGER("Got stored Credentials. Timeout 120s", 0);
  }
  else
  {
    //If not found, device will remain in configuration mode until switched off via webserver.
    ESP_wifiManager.setConfigPortalTimeout(0);
    IGRILLLOGGER("No timeout : ", 0);
    
    if (initialConfig)
      IGRILLLOGGER("DRD or No stored Credentials..", 0);
    else
      IGRILLLOGGER("No stored Credentials.", 0);
  }
  //Local intialization. Once its business is done, there is no need to keep it around

  // Extra parameters to be configured
  // After connecting, parameter.getValue() will get you the configured value
  // Format: <ID> <Placeholder text> <default value> <length> <custom HTML> <label placement>
  // (*** we are not using <custom HTML> and <label placement> ***)
  ESP_WMParameter MQTT_SERVER_FIELD(MQTT_SERVER_Label, "MQTT SERVER", custom_MQTT_SERVER, custom_MQTT_SERVER_LEN);// MQTT_SERVER
  ESP_WMParameter MQTT_SERVERPORT_FIELD(MQTT_SERVERPORT_Label, "MQTT SERVER PORT", custom_MQTT_SERVERPORT, custom_MQTT_PORT_LEN + 1);// MQTT_SERVERPORT
  ESP_WMParameter MQTT_USERNAME_FIELD(MQTT_USERNAME_Label, "MQTT USERNAME", custom_MQTT_USERNAME, custom_MQTT_USERNAME_LEN);// MQTT_USERNAME
  ESP_WMParameter MQTT_PASSWORD_FIELD(MQTT_PASSWORD_Label, "MQTT PASSWORD", custom_MQTT_PASSWORD, custom_MQTT_PASSWORD_LEN);// MQTT_PASSWORD
  ESP_WMParameter MQTT_BASETOPIC_FIELD(MQTT_BASETOPIC_Label, "MQTT BASE TOPIC", custom_MQTT_BASETOPIC, custom_MQTT_BASETOPIC_LEN);// MQTT_BASETOPIC
  ESP_wifiManager.addParameter(&MQTT_SERVER_FIELD);
  ESP_wifiManager.addParameter(&MQTT_SERVERPORT_FIELD);
  ESP_wifiManager.addParameter(&MQTT_USERNAME_FIELD);
  ESP_wifiManager.addParameter(&MQTT_PASSWORD_FIELD);
  ESP_wifiManager.addParameter(&MQTT_BASETOPIC_FIELD);
  ESP_wifiManager.setMinimumSignalQuality(-1);
  ESP_wifiManager.setConfigPortalChannel(0);  // Set config portal channel, Use 0 => random channel from 1-13

#if !USE_DHCP_IP    
  #if USE_CONFIGURABLE_DNS
    // Set static IP, Gateway, Subnetmask, DNS1 and DNS2. New in v1.0.5
    ESP_wifiManager.setSTAStaticIPConfig(stationIP, gatewayIP, netMask, dns1IP, dns2IP);
  #else
    // Set static IP, Gateway, Subnetmask, Use auto DNS1 and DNS2.
    ESP_wifiManager.setSTAStaticIPConfig(stationIP, gatewayIP, netMask);
  #endif 
#endif  
#if USING_CORS_FEATURE
  ESP_wifiManager.setCORSHeader("Your Access-Control-Allow-Origin");
#endif

  // Start an access point and goes into a blocking loop awaiting configuration.
  // Once the user leaves the portal with the exit button processing will continue
  if (!ESP_wifiManager.startConfigPortal((const char *) ssid.c_str(), password))
  {
    IGRILLLOGGER("Not connected to WiFi but continuing anyway.", 0);
  }
  else
  {
    IGRILLLOGGER("WiFi Connected!", 0);
  }
  // Only clear then save data if CP entered and with new valid Credentials
  // No CP => stored getSSID() = ""
  if ( String(ESP_wifiManager.getSSID(0)) != "" && String(ESP_wifiManager.getSSID(1)) != "" )
  {
    memset(&WM_config, 0, sizeof(WM_config));
    for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
    {
      String tempSSID = ESP_wifiManager.getSSID(i);
      String tempPW   = ESP_wifiManager.getPW(i);
      if (strlen(tempSSID.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1)
        strcpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str());
      else
        strncpy(WM_config.WiFi_Creds[i].wifi_ssid, tempSSID.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_ssid) - 1);
      if (strlen(tempPW.c_str()) < sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1)
        strcpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str());
      else
        strncpy(WM_config.WiFi_Creds[i].wifi_pw, tempPW.c_str(), sizeof(WM_config.WiFi_Creds[i].wifi_pw) - 1);  
      if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
      {
        LOGERROR3(F("* Add SSID = "), WM_config.WiFi_Creds[i].wifi_ssid, F(", PW = "), WM_config.WiFi_Creds[i].wifi_pw );
        wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
      }
    }
    ESP_wifiManager.getSTAStaticIPConfig(WM_STA_IPconfig);
    displayIPConfigStruct(WM_STA_IPconfig);
    saveConfigData();
  }

  // Getting posted form values and overriding local variables parameters
  // Config file is written regardless the connection state
  strcpy(custom_MQTT_SERVER, MQTT_SERVER_FIELD.getValue());
  strcpy(custom_MQTT_SERVERPORT, MQTT_SERVERPORT_FIELD.getValue());
  strcpy(custom_MQTT_USERNAME, MQTT_USERNAME_FIELD.getValue());
  strcpy(custom_MQTT_PASSWORD, MQTT_PASSWORD_FIELD.getValue());
  strcpy(custom_MQTT_BASETOPIC, MQTT_BASETOPIC_FIELD.getValue());
  writeConfigFile();  // Writing JSON config file to flash for next boot
  digitalWrite(LED_BUILTIN, LED_OFF); // Turn LED off as we are not in configuration mode.
}


#pragma endregion
#pragma region MQTT_Related_Functions
void disconnectMQTT()
{
  try
  {
    delete mqtt_client;
    mqtt_client=NULL;
  }
  catch(...)
  {
    Serial.printf("Error disconnecting MQTT\n");
  }
}

//Connect to MQTT Server
void connectMQTT() 
{
  
    //String lastWillTopic = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+String(ESP_getChipId(), HEX)+ "/status";
    String lastWillTopic = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+String(ESP_getChipId(), HEX)+ "/status";
    
    //subscribing to topics 
    String inTemperatureTopic = (String)custom_MQTT_BASETOPIC + "/control/esp32base_"+String(ESP_getChipId(), HEX)+ "/tempin";

    IGRILLLOGGER("Connecting to MQTT...", 0);
    if (!client)
      client = new WiFiClient();
    if(!mqtt_client)
    {
      mqtt_client = new PubSubClient(*client);
      mqtt_client->setBufferSize(1024); //Needed as some JSON messages are too large for the default size
      mqtt_client->setKeepAlive(60); //Added to Stabilize MQTT Connection
      mqtt_client->setSocketTimeout(60); //Added to Stabilize MQTT Connection
      mqtt_client->setServer(custom_MQTT_SERVER, atoi(custom_MQTT_SERVERPORT));
    }
  
    if (!mqtt_client->connect(String(ESP_getChipId(), HEX).c_str(), custom_MQTT_USERNAME, custom_MQTT_PASSWORD, lastWillTopic.c_str(), 1, true, "offline"))
    {
      IGRILLLOGGER("MQTT connection failed: " + String(mqtt_client->state()), 0);
      delete mqtt_client;
      mqtt_client=NULL;
      delay(1*5000); //Delay for 5 seconds after a connection failure
    }
    else
    {
      IGRILLLOGGER("MQTT connected", 0);
      mqtt_client->publish(lastWillTopic.c_str(),"online");
      mqtt_client->subscribe(inTemperatureTopic.c_str());   //subscribe to a topic
     IGRILLLOGGER("MQTT connected " + String(inTemperatureTopic), 0);
      mqtt_client->setCallback(callback);
    }
  
}



//Publish iGrill BLE Client and connected iGrill info to MQTT
/*
bool publishSystemInfo()
{
  if(mqtt_client)
  {
    if(mqtt_client->connected())
    { 
      StaticJsonDocument<512> deviceObj;
      deserializeJson(deviceObj, deviceStr);

      String payload="";
      DynamicJsonDocument sysinfoJSON(1024);
      sysinfoJSON["device"] = deviceObj;
      sysinfoJSON["name"] = "igrill_"+ String(ESP_getChipId(), HEX);
      sysinfoJSON["ESP Id"] = String(ESP_getChipId(), HEX);
      sysinfoJSON["Uptime"] = getSystemUptime();
      sysinfoJSON["Network"] = WiFi.SSID();
      sysinfoJSON["Signal Strength"] = String(WiFi.RSSI());
      sysinfoJSON["IP Address"] = WiFi.localIP().toString();
      serializeJson(sysinfoJSON,payload);
      String topic = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+"/systeminfo";
      mqtt_client->publish(topic.c_str(),payload.c_str());
      mqttAnnounce();
    }
    else
    {
      connectMQTT();
    }
  }
  else
  {
    connectMQTT();
  }
}

//Publish iGrill Temp Probe Values to MQTT
void publishProbeTemp(int probeNum, int temp)
{
  if(mqtt_client)
  {
    if(mqtt_client->connected())
    {
      String topic = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+"/probe_"+ String(probeNum);
      if(temp == -100) //If probe unplugged
        mqtt_client->publish(topic.c_str(),"");
      else
        mqtt_client->publish(topic.c_str(),String(temp).c_str());
    }
  }
  else
  {
    connectMQTT();
  }
}

*/

//Publish iGrill Battery Level to MQTT
void publishBattery(float battPercent)
{
  if(mqtt_client)
  {
    if(mqtt_client->connected())
    {
      String topic = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+"/battery_level";
      mqtt_client->publish(topic.c_str(),String(battPercent).c_str(),true);
    }
  }
  else
  {
    connectMQTT();
  }
}

//beedlow
//Publish received Temp Level to MQTT
void publishReceivedTemp(float inmesg)
{
  String topic = (String)custom_MQTT_BASETOPIC + "/control/esp32base_"+String(ESP_getChipId(), HEX)+ "/tempout";
  if(mqtt_client)
  {
    if(mqtt_client->connected())
    {
      mqtt_client->publish(topic.c_str(),String(inmesg).c_str());
     // mqtt_client->publish(topic.c_str(),String(battPercent).c_str(),true);
    }
  }
  else
  {
    connectMQTT();
  }
}


void publishReceivedTemp2(String inmesg)
{
  String topic = (String)custom_MQTT_BASETOPIC + "/control/esp32base_"+String(ESP_getChipId(), HEX)+ "/tempout2";
  if(mqtt_client)
  {
    if(mqtt_client->connected())
    {
      mqtt_client->publish(topic.c_str(),inmesg.c_str());
     // mqtt_client->publish(topic.c_str(),String(battPercent).c_str(),true);
    }
  }
  else
  {
    connectMQTT();
  }
}

//Publish received Temp Level to MQTT
void publishReceivedTemp3(int inmesg)
{
  String topic = (String)custom_MQTT_BASETOPIC + "/control/esp32base_"+String(ESP_getChipId(), HEX)+ "/tempout3";
  if(mqtt_client)
  {
    if(mqtt_client->connected())
    {
      mqtt_client->publish(topic.c_str(),String(inmesg).c_str());
     // mqtt_client->publish(topic.c_str(),String(battPercent).c_str(),true);
    }
  }
  else
  {
    connectMQTT();
  }
}

//Publish received Temp Level to MQTT
void publishReceivedTemp4(char inmesg)
{
  String topic = (String)custom_MQTT_BASETOPIC + "/control/esp32base_"+String(ESP_getChipId(), HEX)+ "/tempout4";
  String outMesg = String(inmesg);
  if(mqtt_client)
  {
    if(mqtt_client->connected())
    {
      mqtt_client->publish(topic.c_str(),String(outMesg).c_str());
     // mqtt_client->publish(topic.c_str(),String(battPercent).c_str(),true);
    }
  }
  else
  {
    connectMQTT();
  }
}

/*
//Publish MQTT Configuration Topics used by MQTT Auto Discovery
void mqttAnnounce()
{
  String battPayload ="";
  String p1Payload = "";
  String p2Payload = "";
  String p3Payload = "";
  String p4Payload = "";

  StaticJsonDocument<512> deviceObj;
  deserializeJson(deviceObj, deviceStr);

  DynamicJsonDocument battJSON(1024);
  battJSON["device"] = deviceObj;
  battJSON["name"] = "igrill_"+String(ESP_getChipId(), HEX)+" Battery Level";
  battJSON["device_class"] = "battery"; 
  battJSON["unique_id"]   = "igrill_"+String(ESP_getChipId(), HEX)+"_batt";
  battJSON["state_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+String(ESP_getChipId(), HEX)+"/battery_level";
  battJSON["unit_of_measurement"] = "%";
  serializeJson(battJSON,battPayload);

  DynamicJsonDocument probe1JSON(1024);
  probe1JSON["device"] = deviceObj;
  probe1JSON["name"] = "igrill_"+String(ESP_getChipId(), HEX)+" Probe 1";
  probe1JSON["device_class"] = "temperature"; 
  probe1JSON["unique_id"]   = "igrill_"+String(ESP_getChipId(), HEX)+"_probe1";
  probe1JSON["state_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+"/probe_1";
  probe1JSON["unit_of_measurement"] = "°F";
  probe1JSON["availability_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+ "/status";
  probe1JSON["payload_available"] = "online";
  probe1JSON["payload_not_available"] = "offline";
  serializeJson(probe1JSON,p1Payload);

  DynamicJsonDocument probe2JSON(1024);
  probe2JSON["device"] = deviceObj;
  probe2JSON["name"] = "igrill_"+String(ESP_getChipId(), HEX)+" Probe 2";
  probe2JSON["device_class"] = "temperature"; 
  probe2JSON["unique_id"]   = "igrill_"+String(ESP_getChipId(), HEX)+"_probe2";
  probe2JSON["state_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+"/probe_2";
  probe2JSON["unit_of_measurement"] = "°F";
  probe2JSON["availability_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+ "/status";
  probe2JSON["payload_available"] = "online";
  probe2JSON["payload_not_available"] = "offline";
  serializeJson(probe2JSON,p2Payload);

  DynamicJsonDocument probe3JSON(1024);
  probe3JSON["device"] = deviceObj;
  probe3JSON["name"] = "igrill_"+String(ESP_getChipId(), HEX)+" Probe 3";
  probe3JSON["device_class"] = "temperature"; 
  probe3JSON["unique_id"]   = "igrill_"+String(ESP_getChipId(), HEX)+"_probe3";
  probe3JSON["state_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+"/probe_3";
  probe3JSON["unit_of_measurement"] = "°F";
  probe3JSON["availability_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+ "/status";
  probe3JSON["payload_available"] = "online";
  probe3JSON["payload_not_available"] = "offline";
  serializeJson(probe3JSON,p3Payload);

  DynamicJsonDocument probe4JSON(1024);
  probe4JSON["device"] = deviceObj;
  probe4JSON["name"] = "igrill_"+String(ESP_getChipId(), HEX)+" Probe 4";
  probe4JSON["device_class"] = "temperature"; 
  probe4JSON["unique_id"]   = "igrill_"+String(ESP_getChipId(), HEX)+"_probe4";
  probe4JSON["state_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+"/probe_4";
  probe4JSON["unit_of_measurement"] = "°F";
  probe4JSON["availability_topic"] = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+ "/status";
  probe4JSON["payload_available"] = "online";
  probe4JSON["payload_not_available"] = "offline";
  serializeJson(probe4JSON,p4Payload);

  if(mqtt_client)
  {
    if(mqtt_client->connected())
    {
      String battConfigTopic = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+"/battery_level/config";
      mqtt_client->publish(battConfigTopic.c_str(),battPayload.c_str(),true);
      delay(100);
      String probe1ConfigTopic = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+"/probe_1/config";
      mqtt_client->publish(probe1ConfigTopic.c_str(),p1Payload.c_str(),true);
      delay(100);
      String probe2ConfigTopic = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+"/probe_2/config";
      mqtt_client->publish(probe2ConfigTopic.c_str(),p2Payload.c_str(),true);
      delay(100);
      String probe3ConfigTopic = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+"/probe_3/config";
      mqtt_client->publish(probe3ConfigTopic.c_str(),p3Payload.c_str(),true);
      delay(100);
      String probe4ConfigTopic = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+"/probe_4/config";
      mqtt_client->publish(probe4ConfigTopic.c_str(),p4Payload.c_str(),true);
      delay(100);
      //We need to publish a status of online each time we reach here otherwise probes plugged in after the initial mqtt discovery
      //will show as offline/unavailable until they see a new online announcement
      String availTopic = (String)custom_MQTT_BASETOPIC + "/sensor/esp32base_"+ String(ESP_getChipId(), HEX)+"/status";
      mqtt_client->publish(availTopic.c_str(),"online");
      delay(100);
    }
    else
    {
      connectMQTT();
    }
  }
  else
  {
    connectMQTT();
  }
}

*/
#pragma endregion
#pragma region ESP_Filesystem_Functions
bool readConfigFile() 
{
  File f = FileFS.open(CONFIG_FILE, "r");// this opens the config file in read-mode
  if (!f)
  {
    IGRILLLOGGER("Config File not found", 0);
    return false;
  }
  else
  {// we could open the file
    size_t size = f.size();
    std::unique_ptr<char[]> buf(new char[size + 1]); // Allocate a buffer to store contents of the file.
    f.readBytes(buf.get(), size); // Read and store file contents in buf
    f.close();
    DynamicJsonDocument json(1024);
    auto deserializeError = deserializeJson(json, buf.get());    
    if ( deserializeError )
    {
      IGRILLLOGGER("JSON parseObject() failed", 0);
      return false;
    }  
    serializeJson(json, Serial);
    // Parse all config file parameters, override
    // local config variables with parsed values
    if (json.containsKey(MQTT_SERVER_Label))
      strcpy(custom_MQTT_SERVER, json[MQTT_SERVER_Label]);
    if (json.containsKey(MQTT_SERVERPORT_Label))
      strcpy(custom_MQTT_SERVERPORT, json[MQTT_SERVERPORT_Label]);
    if (json.containsKey(MQTT_USERNAME_Label))
      strcpy(custom_MQTT_USERNAME, json[MQTT_USERNAME_Label]);
    if (json.containsKey(MQTT_PASSWORD_Label))
      strcpy(custom_MQTT_PASSWORD, json[MQTT_PASSWORD_Label]);
    if (json.containsKey(MQTT_BASETOPIC_Label))
      strcpy(custom_MQTT_BASETOPIC, json[MQTT_BASETOPIC_Label]);
  }
  IGRILLLOGGER("\nConfig File successfully parsed", 0);
  return true;
}

bool writeConfigFile() 
{
  IGRILLLOGGER("Saving Config File", 0);
  DynamicJsonDocument json(1024);
  // JSONify local configuration parameters
  json[MQTT_SERVER_Label] = custom_MQTT_SERVER;
  json[MQTT_SERVERPORT_Label] = custom_MQTT_SERVERPORT;
  json[MQTT_USERNAME_Label] = custom_MQTT_USERNAME;
  json[MQTT_PASSWORD_Label] = custom_MQTT_PASSWORD;
  json[MQTT_BASETOPIC_Label] = custom_MQTT_BASETOPIC;
  // Open file for writing
  File f = FileFS.open(CONFIG_FILE, "w"); 
  if (!f)
  {
    IGRILLLOGGER("Failed to open Config File for writing", 0);
    return false;
  }
  serializeJsonPretty(json, Serial);
  // Write data to file and close it
  serializeJson(json, f);
  f.close();
  IGRILLLOGGER("\nConfig File successfully saved", 0);
  return true;
}

bool loadConfigData()
{
  File file = FileFS.open(CONFIG_FILENAME, "r");
  LOGERROR(F("LoadWiFiCfgFile "));
  memset(&WM_config,       0, sizeof(WM_config));
  memset(&WM_STA_IPconfig, 0, sizeof(WM_STA_IPconfig));
  if (file)
  {
    file.readBytes((char *) &WM_config,   sizeof(WM_config));
    file.readBytes((char *) &WM_STA_IPconfig, sizeof(WM_STA_IPconfig));
    file.close();
    LOGERROR(F("OK"));
    displayIPConfigStruct(WM_STA_IPconfig);
    return true;
  }
  else
  {
    LOGERROR(F("failed"));
    return false;
  }
}
    
void saveConfigData()
{
  File file = FileFS.open(CONFIG_FILENAME, "w");
  LOGERROR(F("SaveWiFiCfgFile "));
  if (file)
  {
    file.write((uint8_t*) &WM_config,   sizeof(WM_config));
    file.write((uint8_t*) &WM_STA_IPconfig, sizeof(WM_STA_IPconfig));
    file.close();
    LOGERROR(F("OK"));
  }
  else
  {
    LOGERROR(F("failed"));
  }
}

#pragma endregion
#pragma region ESP_Hardware_Related_Functions
String getSystemUptime()
{
  long millisecs = millis();
  int systemUpTimeMn = int((millisecs / (1000 * 60)) % 60);
  int systemUpTimeHr = int((millisecs / (1000 * 60 * 60)) % 24);
  int systemUpTimeDy = int((millisecs / (1000 * 60 * 60 * 24)) % 365);
  return String(systemUpTimeDy)+"d:"+String(systemUpTimeHr)+"h:"+String(systemUpTimeMn)+"m";
}

//Toggle LED State
void toggleLED()
{
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
}

//Event Handler Function for Button Click
static void handleClick() 
{
  IGRILLLOGGER("Button clicked!", 0);
  wifi_manager();
}

//Event Handler Function for Double Button Click
static void handleDoubleClick() 
{
  IGRILLLOGGER("Button double clicked!", 0);
}

//Event Handler Function for Button Hold
static void handleLongPressStop() 
{
  IGRILLLOGGER("Button pressed for long time and then released!", 0);
  newConfigData();
}

// Display Saved Configuration Information (Trigger by pressing and holding the reset button for a few seconds)
void newConfigData() 
{
  IGRILLLOGGER("custom_MQTT_SERVER: " + String(custom_MQTT_SERVER), 0); 
  IGRILLLOGGER("custom_MQTT_SERVERPORT: " + String(custom_MQTT_SERVERPORT), 0); 
  IGRILLLOGGER("custom_MQTT_USERNAME: " + String(custom_MQTT_USERNAME), 0); 
  IGRILLLOGGER("custom_MQTT_PASSWORD: " + String(custom_MQTT_PASSWORD), 0); 
  IGRILLLOGGER("custom_MQTT_BASETOPIC: " + String(custom_MQTT_BASETOPIC), 0); 
}


#pragma endregion

void check_status()
{
  static ulong checkstatus_timeout  = 0;
  static ulong LEDstatus_timeout    = 0;
  static ulong checkwifi_timeout    = 0;
  static ulong igrillheartbeat_timeout = 0;
  
  ulong current_millis = millis();

  // Check WiFi every WIFICHECK_INTERVAL (1) seconds.
  if ((current_millis > checkwifi_timeout) || (checkwifi_timeout == 0))
  {
    check_WiFi();
    checkwifi_timeout = current_millis + WIFICHECK_INTERVAL;
  }

  if ((current_millis > LEDstatus_timeout) || (LEDstatus_timeout == 0))
  {
    // Toggle LED at LED_INTERVAL = 2s
    toggleLED();
    LEDstatus_timeout = current_millis + LED_INTERVAL;
  }

  // Print hearbeat every HEARTBEAT_INTERVAL (10) seconds.
  if ((current_millis > checkstatus_timeout) || (checkstatus_timeout == 0))
  { 
    heartBeatPrint();
    checkstatus_timeout = current_millis + HEARTBEAT_INTERVAL;
  }

  // Print iGrill System Info every IGRILL_HEARTBEAT_INTERVAL (5) minutes.
/*
  if ((current_millis > igrillheartbeat_timeout) || (igrillheartbeat_timeout == 0))
  { 
    publishSystemInfo();
    igrillheartbeat_timeout = current_millis + IGRILL_HEARTBEAT_INTERVAL;
  }
*/
mqtt_client->loop();
}

// Setup function
void setup()
{
  Serial.begin(115200);
  while (!Serial);
  delay(200);
  IGRILLLOGGER("Starting esp32Base Client using " + String(FS_Name) + " on " + String(ARDUINO_BOARD), 0);
  IGRILLLOGGER(String(ESP_WIFIMANAGER_VERSION), 0);
  IGRILLLOGGER(String(ESP_DOUBLE_RESET_DETECTOR_VERSION), 0);
  // Initialize the LED digital pin as an output.
  pinMode(LED_BUILTIN, OUTPUT);
  // Format FileFS if not yet
  if (!FileFS.begin(true))
  {
    IGRILLLOGGER(String(FS_Name) + " failed! AutoFormatting.", 0);
  }

  if (!readConfigFile())
  {
    IGRILLLOGGER("Failed to read configuration file, using default values", 0);
  }

  initAPIPConfigStruct(WM_AP_IPconfig);
  initSTAIPConfigStruct(WM_STA_IPconfig);

  if (!readConfigFile())
  {
    IGRILLLOGGER("Can't read Config File, using default values", 0);
  }

  drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);
  if (!drd)
  {
    IGRILLLOGGER("Can't instantiate. Disable DRD feature", 0);
  }
  else if (drd->detectDoubleReset())
  {
    // DRD, disable timeout.
    IGRILLLOGGER("Open Config Portal without Timeout: Double Reset Detected", 0);
    initialConfig = true;
  }
 
  if (initialConfig)
  {
    wifi_manager();
  }
  else
  {   
    // Load stored data, the addAP ready for MultiWiFi reconnection
    loadConfigData();
    // Pretend CP is necessary as we have no AP Credentials
    initialConfig = true;
    for (uint8_t i = 0; i < NUM_WIFI_CREDENTIALS; i++)
    {
      // Don't permit NULL SSID and password len < MIN_AP_PASSWORD_SIZE (8)
      if ( (String(WM_config.WiFi_Creds[i].wifi_ssid) != "") && (strlen(WM_config.WiFi_Creds[i].wifi_pw) >= MIN_AP_PASSWORD_SIZE) )
      {
        wifiMulti.addAP(WM_config.WiFi_Creds[i].wifi_ssid, WM_config.WiFi_Creds[i].wifi_pw);
        initialConfig = false;
      }
    }

    if (initialConfig)
    {
      IGRILLLOGGER("Open Config Portal without Timeout: No stored WiFi Credentials", 0);
      wifi_manager();
    }
    else if ( WiFi.status() != WL_CONNECTED ) 
    {
      IGRILLLOGGER("ConnectMultiWiFi in setup", 0);
      connectMultiWiFi();
    }
  }
  digitalWrite(LED_BUILTIN, LED_OFF); // Turn led off as we are not in configuration mode.
  connectMQTT();
 
}

// Loop function
void loop()
{
  // Call the double reset detector loop method every so often, so that it can recognise when the timeout expires.
  // You can also call drd.stop() when you wish to no longer consider the next reset as a double reset.
  if (drd)
    drd->loop();
 
float measurement = 0;
float battery_voltage = 0;

measurement = (float) analogRead(35);
battery_voltage = (measurement / 4095.0) * 7.26;

publishBattery(battery_voltage);
delay(10000);
//mqtt_client->loop();
check_status();
}