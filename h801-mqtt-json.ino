//
// Alternative firmware for H801 5 channel LED dimmer
// based on https://github.com/open-homeautomation/h801/blob/master/mqtt/mqtt.ino
//      and https://github.com/bruhautomation/ESP-MQTT-JSON-Digital-LEDs/blob/master/ESP_MQTT_Digital_LEDs/ESP_MQTT_Digital_LEDs.ino
//

//IMPORTANT!!!!!!!!!!!!
// inside PubSubClient.h the folloing needs to be changed on line 26:
// PubSubClient.h is inside documents/Arduino/libraries/PubSubclient/src/PubSubClient.h
// #define MQTT_MAX_PACKET_SIZE 128 --> #define MQTT_MAX_PACKET_SIZE 800

#define MQTT_MAX_PACKET_SIZE 800
#define FIRMWARE_VERSION "2.1.1"
#define MANUFACTURER "Huacanxing"

#include <string>

#include <ESP8266WiFi.h>
#include <PubSubClient.h>             // MQTT client
#include <WiFiUDP.h>                  // UDP
#include <ESP8266WebServer.h>         // OTA
#include <ESP8266mDNS.h>              // OTA
#include <ESP8266HTTPUpdateServer.h>  // OTA
#include <ArduinoJson.h>
#include "Config.h"

#define DEVELOPMENT

// Get settings from config.h
const char* mqtt_server = mqtt_server_conf;
const char* mqtt_user = mqtt_user_conf;
const char* mqtt_password = mqtt_password_conf;

// Initial setup
WiFiClient wifiClient;
PubSubClient client(wifiClient);
WiFiUDP Udp;
ESP8266WebServer httpServer(OTA_port);
ESP8266HTTPUpdateServer httpUpdater;

/********************************** program variables  *****************************************/
char chip_id[9] = "00000000";
char client_id[16] = "00000000_00000";
char myhostname[] = "esp00000000";
IPAddress ip;
uint8_t reconnect_N = 0;
unsigned long last_publish_ms = 0;
unsigned long last_mqtt_connected = 0;
bool mqtt_proccesed = false;
// transitioning variables
float transition_time_s_standard = transition_time_s_conf;
float transition_time_s = transition_time_s_conf;
unsigned long now;
unsigned long last_transition_publish = 0;
unsigned long start_transition_loop_ms = 0;
unsigned long transition_ms = 0;
uint8_t transition_increment = 1;
int rest_step[5] = {0};
float step_time_ms[5] = {1};
int n_step[5] = {0};
uint16_t transitionStepCount[5] = {0};
boolean transitioning = false;
uint8_t t_red_begin = 255;
uint8_t t_green_begin = 255;
uint8_t t_blue_begin = 255;
uint8_t t_rgb_brightness_begin = 255;
uint8_t t_white_brightness_begin = 255;
uint8_t t_w1_begin = 255;
uint8_t t_w2_begin = 255;
boolean t_rgb_state_begin = false;
boolean t_white_state_begin = false;
boolean t_white_single_mode_begin = false;
boolean t_white_single1_state_begin = false;
boolean t_white_single2_state_begin = false;
uint8_t targetR = 255;
uint8_t targetG = 255;
uint8_t targetB = 255;
uint8_t targetW1 = 255;
uint8_t targetW2 = 255;
// UDP/HDMI variables
boolean UDP_stream = false;
boolean UDP_stream_begin = false;
uint16_t UDP_packetSize = 0;

// buffer used to send/receive data with MQTT
#define JSON_BUFFER_SIZE 800
#define MQTT_UP_online "online"
#define MQTT_UP_offline "offline"


/********************************** Light variables  *****************************************/
// the payload that represents enabled/disabled state, by default
const char* LIGHT_ON = "ON";
const char* LIGHT_OFF = "OFF";

#define RGB_LIGHT_RED_PIN    15
#define RGB_LIGHT_GREEN_PIN  13
#define RGB_LIGHT_BLUE_PIN   12
#define W1_PIN               14
#define W2_PIN               4

#define GREEN_PIN    1
#define RED_PIN      5

// GREEN PIN state
bool GREEN_state = false;
unsigned long GREEN_off_ms = 0;

// store the state of the rgb LED strip (colors, brightness, ...)
boolean m_rgb_state = false;
uint8_t m_rgb_brightness = 255;
uint8_t m_rgb_red = 255;
uint8_t m_rgb_green = 255;
uint8_t m_rgb_blue = 255;

// store the state of the white LED strip (colors, brightness, ...)
boolean m_white_state = false;
uint8_t m_white_brightness = 255;
uint16_t m_color_temp = max_color_temp;
uint8_t m_w1 = 255;
uint8_t m_w2 = 255;

// store the state of the white single LED strips
boolean m_white_single_mode = false;
boolean m_white_single1_state = false;
boolean m_white_single2_state = false;

// store aditional states for combined RGB White light
uint8_t m_combined_brightness = 255;
boolean m_white_mode = true;
String m_effect = "white_mode";
String m_color_mode = "color_temp";

// store state during transitioning
uint8_t transition_red = 255;
uint8_t transition_green = 255;
uint8_t transition_blue = 255;
uint8_t transition_w1 = 255;
uint8_t transition_w2 = 255;


/********************************** Setup *****************************************/

void setup()
{
  pinMode(RGB_LIGHT_RED_PIN, OUTPUT);
  pinMode(RGB_LIGHT_GREEN_PIN, OUTPUT);
  pinMode(RGB_LIGHT_BLUE_PIN, OUTPUT);
  analogWriteRange(255);
  setRGB(0, 0, 0);
  pinMode(W1_PIN, OUTPUT);
  setW1(0);
  pinMode(W2_PIN, OUTPUT);
  setW2(0);

  pinMode(GREEN_PIN, OUTPUT);
  pinMode(RED_PIN, OUTPUT);
  digitalWrite(RED_PIN, 0);
  digitalWrite(GREEN_PIN, 1);

  convert_color_temp();

  sprintf(chip_id, "%08X", ESP.getChipId());
  sprintf(myhostname, "esp%08X", ESP.getChipId());

  // Setup console
  Serial1.begin(115200);
  delay(10);
  Serial1.println();
  Serial1.println();

  // Setup WIFI
  //WiFi.setSleepMode(WIFI_NONE_SLEEP);
  //WiFi.setOutputPower(20.5); // default is 20.5 which is max, 16.5 less interference, step size 0.25
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid_conf, wifi_password_conf);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial1.println("Connection Failed! Rebooting...");
    Serial1.print(".");
    delay(30000);
    ESP.restart();
  }

  Serial1.println("");

  ip = WiFi.localIP();

  if (WiFi.status() == WL_CONNECTED) {
    Serial1.println("");
    Serial1.print("Connected to ");
    Serial1.println(wifi_ssid_conf);
    Serial1.print("IP address: ");
    Serial1.println(ip);
  }

  // init the MQTT connection
  client.setBufferSize(MQTT_MAX_PACKET_SIZE);
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // OTA
  // do not start OTA server if no password has been set
  if (OTA_password != "") {
    MDNS.begin(OTA_hostname);
    httpUpdater.setup(&httpServer, OTA_update_path, OTA_username, OTA_password);
    httpServer.begin();
    MDNS.addService("http", "tcp", OTA_port);
    Serial.printf("HTTPUpdateServer ready! Open http://%s.local%s in your browser and login", OTA_hostname, OTA_update_path);
  }

  digitalWrite(RED_PIN, 1);
}


/********************************** LED strip control *****************************************/

void setRGB(uint8_t p_red, uint8_t p_green, uint8_t p_blue) {
  analogWrite(RGB_LIGHT_RED_PIN, map(p_red, 0, 255, 0, RGB_mixing[0]));
  analogWrite(RGB_LIGHT_GREEN_PIN, map(p_green, 0, 255, 0, RGB_mixing[1]));
  analogWrite(RGB_LIGHT_BLUE_PIN, map(p_blue, 0, 255, 0, RGB_mixing[2]));
}

void setColor(void) {
  if (m_rgb_state) {
    setRGB(map(m_rgb_red, 0, 255, 0, m_rgb_brightness), map(m_rgb_green, 0, 255, 0, m_rgb_brightness), map(m_rgb_blue, 0, 255, 0, m_rgb_brightness));
  } else {
    setRGB(0, 0, 0);
  }
}

void convert_color_temp(void) {
  float temp_unit = 2*(float(m_color_temp) - min_color_temp)/(max_color_temp - min_color_temp) - 1;
  if (temp_unit == 0){
    m_w1 = 255;
    m_w2 = 255;
  } else if (temp_unit > 0) {
    m_w1 = int(round((1.0-temp_unit)*255));
    m_w2 = 255;
  } else if (temp_unit < 0){
    m_w1 = 255;
    m_w2 = int(round((1.0+temp_unit)*255));
  } else {
    return;
  }
}

void setW1(uint8_t brightness) {
  // convert the brightness in % (0-100%) into a digital value (0-255)
  analogWrite(W1_PIN, brightness);
}

void setW2(uint8_t brightness) {
  // convert the brightness in % (0-100%) into a digital value (0-255)
  analogWrite(W2_PIN, brightness);
}

void setWhite(void) {
  if (!m_white_single_mode) {
    if (m_white_state) {
      uint8_t w1_brightness = map(m_w1, 0, 255, 0, m_white_brightness);
      uint8_t w2_brightness = map(m_w2, 0, 255, 0, m_white_brightness);
      setW1(w1_brightness);
      setW2(w2_brightness);
    } else {
      setW1(0);
      setW2(0);
    }
  } else {
    if (m_white_single1_state) {
      setW1(m_w1);
    } else {
      setW1(0);
    }
    if (m_white_single2_state) {
      setW2(m_w2);
    } else {
      setW2(0);
    }
  }
}

void setLEDpin(int LED_pin, uint8_t LED_value){
  // map the color value from (0-255) to the value range (0-RGB_mixing)
  if (LED_pin == RGB_LIGHT_RED_PIN) {
    LED_value = map(LED_value, 0, 255, 0, RGB_mixing[0]);  
  } else if (LED_pin == RGB_LIGHT_GREEN_PIN) {
    LED_value = map(LED_value, 0, 255, 0, RGB_mixing[1]); 
  } else if (LED_pin == RGB_LIGHT_BLUE_PIN) {
    LED_value = map(LED_value, 0, 255, 0, RGB_mixing[2]); 
  }
  analogWrite(LED_pin, LED_value);
}

void Flash_GREEN(void) {
  // Flash green LED for 0.2 seconds
  digitalWrite(GREEN_PIN, 0);
  GREEN_state = true;
  GREEN_off_ms = now;
}

/********************************** Publish states *****************************************/

void publishRGBJsonState() {
  if (transitioning) {
    return;  // let the transition publish intermediate states
  }
  publishRGBJsonStateVal(m_rgb_state, m_rgb_red, m_rgb_green, m_rgb_blue, m_rgb_brightness);
}
void publishRGBJsonStateVal(boolean p_rgb_state, uint8_t p_rgb_red, uint8_t p_rgb_green, uint8_t p_rgb_blue, uint8_t p_rgb_brightness) {
  StaticJsonDocument<JSON_BUFFER_SIZE> root;

  root["state"] = (p_rgb_state) ? LIGHT_ON : LIGHT_OFF;
  JsonObject color = root.createNestedObject("color");
  color["r"] = p_rgb_red;
  color["g"] = p_rgb_green;
  color["b"] = p_rgb_blue;

  root["color_mode"] = "rgb";
  root["brightness"] = p_rgb_brightness;

  if (UDP_stream) {
    m_effect = "HDMI";
    root["state"] = LIGHT_ON;
  } else {
    m_effect = "color_mode";
  }
  root["effect"] = m_effect.c_str();

  char buffer[measureJson(root) + 1];
  serializeJson(root, buffer, sizeof(buffer));

  client.publish(MQTT_JSON_LIGHT_RGB_STATE_TOPIC, buffer, true);
}

void publishWhiteJsonState() {
  if (transitioning) {
    return;  // let the transition publish intermediate states
  }
  publishWhiteJsonStateVal(m_white_state, m_w1, m_w2, m_white_brightness);
}
void publishWhiteJsonStateVal(boolean p_white_state, uint8_t p_w1, uint8_t p_w2, uint8_t p_white_brightness) {
  StaticJsonDocument<JSON_BUFFER_SIZE> root;

  root["state"] = (p_white_state) ? LIGHT_ON : LIGHT_OFF;
  
  m_color_temp = int(round((float(p_w2)/510 - float(p_w1)/510 + 0.5)*(max_color_temp - min_color_temp)) + min_color_temp);
  root["color_temp"] = m_color_temp;
  root["color_mode"] = "color_temp";
  
  root["brightness"] = p_white_brightness;


  char buffer[measureJson(root) + 1];
  serializeJson(root, buffer, sizeof(buffer));

  client.publish(MQTT_JSON_LIGHT_WHITE_STATE_TOPIC, buffer, true);
}

void publishWhiteSingle1JsonState() {
  if (transitioning) {
    return;  // let the transition publish intermediate states
  }
  publishWhiteSingle1JsonStateVal(m_white_single1_state, m_w1);
}
void publishWhiteSingle1JsonStateVal(boolean p_w1_state, uint8_t p_w1) {
  StaticJsonDocument<JSON_BUFFER_SIZE> root;

  root["state"] = (p_w1_state) ? LIGHT_ON : LIGHT_OFF;
  root["brightness"] = p_w1;
  root["color_mode"] = "brightness";

  char buffer[measureJson(root) + 1];
  serializeJson(root, buffer, sizeof(buffer));

  client.publish(MQTT_JSON_LIGHT_WHITE_SINGLE_1_STATE_TOPIC, buffer, true);
}

void publishWhiteSingle2JsonState() {
  if (transitioning) {
    return;  // let the transition publish intermediate states
  }
  publishWhiteSingle2JsonStateVal(m_white_single2_state, m_w2);
}
void publishWhiteSingle2JsonStateVal(boolean p_w2_state, uint8_t p_w2) {
  StaticJsonDocument<JSON_BUFFER_SIZE> root;

  root["state"] = (p_w2_state) ? LIGHT_ON : LIGHT_OFF;
  root["brightness"] = p_w2;
  root["color_mode"] = "brightness";

  char buffer[measureJson(root) + 1];
  serializeJson(root, buffer, sizeof(buffer));

  client.publish(MQTT_JSON_LIGHT_WHITE_SINGLE_2_STATE_TOPIC, buffer, true);
}

void publishCombinedJsonState() {
  if (transitioning) {
    return;  // let the transition publish intermediate states
  }
  publishCombinedJsonStateVal(m_white_state, m_w1, m_w2, m_rgb_state, m_rgb_red, m_rgb_green, m_rgb_blue, m_combined_brightness);
}
void publishCombinedJsonStateVal(boolean p_white_state, uint8_t p_w1, uint8_t p_w2, boolean p_rgb_state, uint8_t p_rgb_red, uint8_t p_rgb_green, uint8_t p_rgb_blue, uint8_t p_combined_brightness) {
  StaticJsonDocument<JSON_BUFFER_SIZE> root;

  root["state"] = (p_white_state || p_rgb_state) ? LIGHT_ON : LIGHT_OFF;

  m_color_temp = int(round((float(p_w2)/510 - float(p_w1)/510 + 0.5)*(max_color_temp - min_color_temp)) + min_color_temp);
  root["color_temp"] = m_color_temp;

  root["brightness"] = p_combined_brightness;

  JsonObject color = root.createNestedObject("color");
  if (p_white_state && !p_rgb_state) {
    color["r"] = 255;
    color["g"] = 255;
    color["b"] = 255;
  } else {
    color["r"] = p_rgb_red;
    color["g"] = p_rgb_green;
    color["b"] = p_rgb_blue;
  }

  if (UDP_stream) {
    m_effect = "HDMI";
    m_color_mode = "rgb";
    root["state"] = LIGHT_ON;
  } else if (p_white_state && !p_rgb_state) {
    m_effect = "white_mode";
    m_color_mode = "color_temp";
  } else if (!p_white_state && p_rgb_state) {
    m_effect = "color_mode";
    m_color_mode = "rgb";
  } else if (p_white_state && p_rgb_state) {
    m_effect = "both_mode";
    m_color_mode = "rgb";
  }
  root["effect"] = m_effect.c_str();
  root["color_mode"] = m_color_mode.c_str();

  char buffer[measureJson(root) + 1];
  serializeJson(root, buffer, sizeof(buffer));

  client.publish(MQTT_JSON_LIGHT_COMBINED_STATE_TOPIC, buffer, true);
}

void publishJsonSettings() {
  StaticJsonDocument<JSON_BUFFER_SIZE> root;

  root["transition_time_s_standard"] = transition_time_s_standard;
  JsonObject rgb_mix = root.createNestedObject("RGB_mixing");
  rgb_mix["r"] = RGB_mixing[0];
  rgb_mix["g"] = RGB_mixing[1];
  rgb_mix["b"] = RGB_mixing[2];
  root["chip_id"] = myhostname;
  root["client_id"] = client_id;
  root["IP"] = ip.toString();
  root["RSSI_dBm"] = WiFi.RSSI();

  char buffer[measureJson(root) + 1];
  serializeJson(root, buffer, sizeof(buffer));

  client.publish(MQTT_JSON_LIGHT_SETTINGS_STATE_TOPIC, buffer, true);
}

void publishJsonDiscovery() {
  publishJsonDiscovery_entity("combined", true, true);
  publishJsonDiscovery_entity("rgb", false, true);
  publishJsonDiscovery_entity("white", true, false);
  publishJsonDiscovery_entity("white_single", false, false);
  publishJsonDiscovery_entity("white_single_2", false, false);
}

void publishJsonDiscovery_entity(const char type[], bool sup_color_temp, bool sup_rgb) {
  StaticJsonDocument<JSON_BUFFER_SIZE> root;
  char idendifier[15], unique_id[29], entity_name[16], stat_t[29], cmd_t[29];
  char conf_url[strlen(OTA_update_path)+25];
  sprintf(idendifier, "H801_%s", chip_id);
  sprintf(unique_id, "H801_%s_%s", chip_id, type);
  sprintf(conf_url, "http://%s%s", ip.toString().c_str(), OTA_update_path);
  sprintf(entity_name, "%s", type);

  sprintf(stat_t, "~/%s/json_status", type);
  sprintf(cmd_t, "~/%s/json_set", type);

  root["~"] = Mqtt_Base_Topic;
  root["name"] = entity_name;
  root["unique_id"] = unique_id;
  root["schema"] = "json";
  root["stat_t"] = stat_t;
  root["cmd_t"] = cmd_t;
  root["avty_t"] = "~/active";
  root["brightness"] = true;
  JsonArray sup_col_modes;
  sup_col_modes = root.createNestedArray("supported_color_modes");
  if(sup_color_temp) {
    sup_col_modes.add("color_temp");
    root["min_mireds"] = min_color_temp;
    root["max_mireds"] = max_color_temp;
  }
  if(sup_rgb) {
    sup_col_modes.add("rgb");
    root["effect"] = true;
    JsonArray effect_list = root.createNestedArray("effect_list");
    if(sup_color_temp) {
      effect_list.add("white_mode");
    }
    effect_list.add("color_mode");
    if(sup_color_temp) {
      effect_list.add("both_mode");
    }
    if (UDP_Port != 0) {
      effect_list.add("HDMI");
    }
  }
  if(!sup_color_temp && !sup_rgb) {
    sup_col_modes.add("brightness");
  }
  root["optimistic"] = false;
  JsonObject device = root.createNestedObject("device");
  device["configuration_url"] = conf_url;
  JsonArray identifier_arr = device.createNestedArray("identifiers");
  identifier_arr.add(idendifier);
  device["model"] = "H801";
  device["manufacturer"] = MANUFACTURER;
  device["name"] = Module_Name;
  device["sw_version"] = FIRMWARE_VERSION;
  
  char buffer[measureJson(root) + 1];
  serializeJson(root, buffer, sizeof(buffer));

  char mqtt_discovery_topic[strlen(MQTT_HOMEASSISTANT_DISCOVERY_PREFIX) + 50];
  sprintf(mqtt_discovery_topic, "%s/light/H801_%s/%s/config", MQTT_HOMEASSISTANT_DISCOVERY_PREFIX, chip_id, type);
  client.publish(mqtt_discovery_topic, buffer, true);
}


/********************************** MQTT calback *****************************************/

// function called when a MQTT message arrived
void callback(char* p_topic, byte* p_payload, unsigned int p_length) {
  // Parse received message
  char message[p_length + 1];
  for (int i = 0; i < p_length; i++) {
    message[i] = (char)p_payload[i];
  }
  message[p_length] = '\0';

  // Save RGB begin state
  t_rgb_state_begin = m_rgb_state;
  t_rgb_brightness_begin = m_rgb_brightness;
  t_red_begin = m_rgb_red;
  t_green_begin = m_rgb_green;
  t_blue_begin = m_rgb_blue;
  // Save White begin state
  t_white_state_begin = m_white_state;
  t_white_single_mode_begin = m_white_single_mode;
  t_white_single1_state_begin = m_white_single1_state;
  t_white_single2_state_begin = m_white_single2_state;
  t_white_brightness_begin = m_white_brightness;
  t_w1_begin = m_w1;
  t_w2_begin = m_w2;
  // Save UDP begin state
  UDP_stream_begin = UDP_stream;

  // Handle RGB commands
  if (String(MQTT_JSON_LIGHT_RGB_COMMAND_TOPIC).equals(p_topic)) {
    if (!processRGBJson(message)) {
      return;
    }
    if (transition_time_s <= 0) {
      setColor();
      publishRGBJsonState();
    } else {
      Transition();
      // Let transition publish each 15s and @end
      mqtt_proccesed = true;
    }
  }

  // Handle White commands
  if (String(MQTT_JSON_LIGHT_WHITE_COMMAND_TOPIC).equals(p_topic)) {
    m_white_single_mode = false;
    if (!processWhiteJson(message, false, false)) {
      return;
    }
    if (transition_time_s <= 0) {
      setWhite();
      publishWhiteJsonState();
    } else {
      Transition();
      // Let transition publish each 15s and @end
      mqtt_proccesed = true;
    }
  }

  // Handle White single 1 commands
  if (String(MQTT_JSON_LIGHT_WHITE_SINGLE_1_COMMAND_TOPIC).equals(p_topic)) {
    m_white_single_mode = true;
    if (!processWhiteJson(message, true, false)) {
      return;
    }
    if (transition_time_s <= 0) {
      setWhite();
      publishWhiteSingle1JsonState();
    } else {
      Transition();
      // Let transition publish each 15s and @end
      mqtt_proccesed = true;
    }
  }

  // Handle White single 2 commands
  if (String(MQTT_JSON_LIGHT_WHITE_SINGLE_2_COMMAND_TOPIC).equals(p_topic)) {
    m_white_single_mode = true;
    if (!processWhiteJson(message, false, true)) {
      return;
    }
    if (transition_time_s <= 0) {
      setWhite();
      publishWhiteSingle2JsonState();
    } else {
      Transition();
      // Let transition publish each 15s and @end
      mqtt_proccesed = true;
    }
  }

  // Handle combined commands
  if (String(MQTT_JSON_LIGHT_COMBINED_COMMAND_TOPIC).equals(p_topic)) {
    m_white_single_mode = false;
    if (!processCombinedJson(message)) {
      return;
    }
    if (transition_time_s <= 0) {
      setWhite();
      setColor();
      publishCombinedJsonState();
    } else {
      Transition();
      // Let transition publish each 15s and @end
    }
  }

  // Handle settings commands
  if (String(MQTT_JSON_LIGHT_SETTINGS_COMMAND_TOPIC).equals(p_topic)) {
    if (!processJsonSettings(message)) {
      return;
    }
    publishJsonSettings();
  }

  // Reset the transition time for the next transition
  transition_time_s = transition_time_s_standard;

  // Apply UDP stream changes
  UDP_start_stop();

  // Flash green LED
  Flash_GREEN();
}


/********************************** JSON processing *****************************************/

bool processRGBJson(char* message) {
  StaticJsonDocument<JSON_BUFFER_SIZE> root;

  DeserializationError error = deserializeJson(root, message);

  if (error) {
    Serial1.println("deserializeJson() failed");
    return false;
  }

  // Turn RGB "ON" or "OFF"
  if (root.containsKey("state")) {
    if (strcmp(root["state"], LIGHT_ON) == 0) {
      m_rgb_state = true;
      //check brightness and color not to be 0
      if (m_rgb_brightness == 0) {
        m_rgb_brightness = 255;
      }
      if (m_rgb_red == 0 && m_rgb_green == 0 && m_rgb_blue == 0) {
        m_rgb_red = 255;
        m_rgb_green = 255;
        m_rgb_blue = 255;
      }
    }
    else if (strcmp(root["state"], LIGHT_OFF) == 0) {
      m_rgb_state = false;
    }
  }

  // Change RGB brightness
  if (root.containsKey("brightness")) {
    uint8_t brightness = int(root["brightness"]);
    if (brightness < 0 || brightness > 255) {
      Serial1.println("Invalid brightness");
      return false;
    } else {
      m_rgb_brightness = brightness;
    }
  }

  // Change RGB color
  if (root.containsKey("color")) {
    uint8_t rgb_red = int(root["color"]["r"]);
    if (rgb_red < 0 || rgb_red > 255) {
      Serial1.println("Invalid red color value");
      return false;
    } else {
      m_rgb_red = rgb_red;
    }

    uint8_t rgb_green = int(root["color"]["g"]);
    if (rgb_green < 0 || rgb_green > 255) {
      Serial1.println("Invalid green color value");
      return false;
    } else {
      m_rgb_green = rgb_green;
    }

    uint8_t rgb_blue = int(root["color"]["b"]);
    if (rgb_blue < 0 || rgb_blue > 255) {
      Serial1.println("Invalid blue color value");
      return false;
    } else {
      m_rgb_blue = rgb_blue;
    }
  }

  // Change LED strip mode
  if (root.containsKey("effect")) {
      const char* effect = root["effect"];
      m_effect = effect;
      if (m_effect == "UDP" || m_effect == "HDMI") {
        UDP_stream = true;
      } else {
        UDP_stream = false;
      }
  } else {
    UDP_stream = false;
  }

  // Check transition time
  if (root.containsKey("transition")) {
    float trans_time = float(root["transition"]);
    if (trans_time > 0) {
      transition_time_s = trans_time;
    } else {
      transition_time_s = 0;
    }
  }

  return true;
}


bool processWhiteJson(char* message, bool single1, bool single2) {
  StaticJsonDocument<JSON_BUFFER_SIZE> root;

  DeserializationError error = deserializeJson(root, message);

  if (error) {
    Serial1.println("deserializeJson() failed");
    return false;
  }

  // Turn White "ON" or "OFF"
  if (root.containsKey("state")) {
    if (strcmp(root["state"], LIGHT_ON) == 0) {
      m_white_state = true;
      //check brightness not to be 0
      if (m_white_brightness == 0) {
        m_white_brightness = 255;
      }
      // process single mode
      if (m_white_single_mode && single1){
        m_white_single1_state = true;
        if (m_w1 == 0) { //check brightness not to be 0
          m_w1 = 255;
        }
      }
      if (m_white_single_mode && single2){
        m_white_single2_state = true;
        if (m_w2 == 0) { //check brightness not to be 0
          m_w2 = 255;
        }
      }
    }
    else if (strcmp(root["state"], LIGHT_OFF) == 0) {
      if (!m_white_single_mode){
        m_white_state = false;
        m_white_single1_state = false;
        m_white_single2_state = false;
      } else {
        if (single1){
          m_white_single1_state = false;
        }
        if (single2){
          m_white_single2_state = false;
        }
        m_white_state = m_white_single1_state || m_white_single2_state;
      }
    }
  }

  // Change White brightness
  if (root.containsKey("brightness")) {
    uint8_t brightness = int(root["brightness"]);
    if (brightness < 0 || brightness > 255) {
      Serial1.println("Invalid brightness");
      return false;
    } else {
      m_white_brightness = brightness;
      if (single1){
        m_w1 = brightness;
      }
      if (single2){
        m_w2 = brightness;
      }
    }
  }

  // Change White color temperature
  if (root.containsKey("color_temp")) {
    uint16_t color_temp = int(root["color_temp"]);
    if (color_temp < min_color_temp || color_temp > max_color_temp) {
      Serial1.println("Invalid color temperature");
      return false;
    } else {
      m_color_temp = color_temp;
      convert_color_temp();
    }
  }

  // Check transition time
  if (root.containsKey("transition")) {
    float trans_time = float(root["transition"]);
    if (trans_time > 0) {
      transition_time_s = trans_time;
    } else {
      transition_time_s = 0;
    }
  }

  return true;
}

bool processCombinedJson(char* message) {
  StaticJsonDocument<JSON_BUFFER_SIZE> root;

  DeserializationError error = deserializeJson(root, message);

  if (error) {
    Serial1.println("deserializeJson() failed");
    return false;
  }

  // Turn LED strip "ON" or "OFF"
  if (root.containsKey("state")) {
    if (strcmp(root["state"], LIGHT_ON) == 0) {
      if (m_white_mode) { 
        m_white_state = true;
        m_rgb_state = false;
      } else {
        m_white_state = false;
        m_rgb_state = true;
      }
      //check brightness and color not to be 0
      if (m_rgb_brightness == 0) {
        m_rgb_brightness = 255;
      }
      if (m_white_brightness == 0) {
        m_white_brightness = 255;
      }
      if (m_combined_brightness == 0) {
        m_combined_brightness = 255;
      }
      if (m_rgb_red == 0 && m_rgb_green == 0 && m_rgb_blue == 0) {
        m_rgb_red = 255;
        m_rgb_green = 255;
        m_rgb_blue = 255;
      }
    }
    else if (strcmp(root["state"], LIGHT_OFF) == 0) {
      if (m_white_state) {
        m_white_mode = true;
      } else if (m_rgb_state) {
        m_white_mode = false;
      } 
      m_rgb_state = false;
      m_white_state = false;
    }
  }

  // Change LED strip brightness
  if (root.containsKey("brightness")) {
    uint8_t brightness = int(root["brightness"]);
    if (brightness < 0 || brightness > 255) {
      Serial1.println("Invalid brightness");
      return false;
    } else {
      m_rgb_brightness = brightness;
      m_white_brightness = brightness;
      m_combined_brightness = brightness;
    }
  }

  // Change LED strip color
  if (root.containsKey("color")) {
    uint8_t rgb_red = int(root["color"]["r"]);
    if (rgb_red < 0 || rgb_red > 255) {
      Serial1.println("Invalid red color value");
      return false;
    } else {
      m_rgb_red = rgb_red;
    }

    uint8_t rgb_green = int(root["color"]["g"]);
    if (rgb_green < 0 || rgb_green > 255) {
      Serial1.println("Invalid green color value");
      return false;
    } else {
      m_rgb_green = rgb_green;
    }

    uint8_t rgb_blue = int(root["color"]["b"]);
    if (rgb_blue < 0 || rgb_blue > 255) {
      Serial1.println("Invalid blue color value");
      return false;
    } else {
      m_rgb_blue = rgb_blue;
    }

    m_rgb_state = true;
    m_white_state = false;
    m_white_mode = false;
  }

  // Change LED strip color temperature
  if (root.containsKey("color_temp")) {
    uint16_t color_temp = int(root["color_temp"]);
    if (color_temp < min_color_temp || color_temp > max_color_temp) {
      Serial1.println("Invalid color temperature");
      return false;
    } else {
      m_color_temp = color_temp;
      convert_color_temp();
    }

    m_white_state = true;
    m_rgb_state = false;
    m_white_mode = true;
  }

  // Change LED strip mode
  if (root.containsKey("effect")) {
      const char* effect = root["effect"];
      m_effect = effect;
      if (m_effect == "white_mode") {
        m_white_state = true;
        m_rgb_state = false;
        m_white_mode = true;
        UDP_stream = false;
      } else if (m_effect == "color_mode") {
        m_white_state = false;
        m_rgb_state = true;
        m_white_mode = false;
        UDP_stream = false;
      } else if (m_effect == "UDP" || m_effect == "HDMI") {
        UDP_stream = true;
      } else {
        UDP_stream = false;
      }
  } else {
    UDP_stream = false;
  }

  // Check transition time
  if (root.containsKey("transition")) {
    float trans_time = float(root["transition"]);
    if (trans_time > 0) {
      transition_time_s = trans_time;
    } else {
      transition_time_s = 0;
    }
  }

  return true;
}

bool processJsonSettings(char* message) {
  StaticJsonDocument<JSON_BUFFER_SIZE> root;

  DeserializationError error = deserializeJson(root, message);

  if (error) {
    Serial1.println("deserializeJson() failed");
    return false;
  }

  // Check transition time
  if (root.containsKey("transition_time_s")) {
    float trans_time = float(root["transition_time_s"]);
    if (trans_time > 0) {
      transition_time_s_standard = trans_time;
    } else {
      transition_time_s_standard = 0;
    }
  }

  // Check RGB_mixing
  if (root.containsKey("RGB_mixing")) {
    uint8_t mixing_red = int(root["RGB_mixing"]["r"]);
    if (mixing_red < 0 || mixing_red > 255) {
      Serial1.println("Invalid red mixing value");
      return false;
    } else {
      RGB_mixing[0] = mixing_red;
    }

    uint8_t mixing_green = int(root["RGB_mixing"]["g"]);
    if (mixing_green < 0 || mixing_green > 255) {
      Serial1.println("Invalid green mixing value");
      return false;
    } else {
      RGB_mixing[1] = mixing_green;
    }

    uint8_t mixing_blue = int(root["RGB_mixing"]["b"]);
    if (mixing_blue < 0 || mixing_blue > 255) {
      Serial1.println("Invalid blue mixing value");
      return false;
    } else {
      RGB_mixing[2] = mixing_blue;
    }
  }

  return true;
}


/********************************** MQTT connection *****************************************/

void reconnect() {
  // Loop until we're reconnected
  reconnect_N = 0;
  while (!client.connected()) {
    Serial1.print("Attempting MQTT connection...");
    // Attempt to connect
    sprintf(client_id, "%s_%05d", chip_id, random(1, 99999));
    if (client.connect(client_id, mqtt_user, mqtt_password, MQTT_UP, 2, true, MQTT_UP_offline)) {
      Serial1.println("connected");
      // blink 10 times green LED for success connected
      for (int x=0; x < 10; x++){
        delay(100);
        digitalWrite(GREEN_PIN, 0);
        delay(100);
        digitalWrite(GREEN_PIN, 1);
      }
      
      ip = WiFi.localIP();
      client.publish(MQTT_UP, MQTT_UP_online, true);
      // Once connected, publish an announcement...
      // publish the initial values
      publishCombinedJsonState();
      publishRGBJsonState();
      publishWhiteJsonState();
      publishWhiteSingle1JsonState();
      publishWhiteSingle2JsonState();
      publishJsonSettings();
      publishJsonDiscovery();
      // ... and resubscribe
      client.subscribe(MQTT_JSON_LIGHT_RGB_COMMAND_TOPIC);
      client.subscribe(MQTT_JSON_LIGHT_WHITE_COMMAND_TOPIC);
      client.subscribe(MQTT_JSON_LIGHT_WHITE_SINGLE_1_COMMAND_TOPIC);
      client.subscribe(MQTT_JSON_LIGHT_WHITE_SINGLE_2_COMMAND_TOPIC);
      client.subscribe(MQTT_JSON_LIGHT_COMBINED_COMMAND_TOPIC);
      client.subscribe(MQTT_JSON_LIGHT_SETTINGS_COMMAND_TOPIC);
    } else {
      Serial1.print("failed, rc=");
      Serial1.print(client.state());
      Serial1.print(", mqtt_ip=");
      Serial1.print(mqtt_server);
      Serial1.println(" try again in 5 seconds");
      reconnect_N = reconnect_N + 1;
      // Wait about 5 seconds (10 x 500ms) before retrying
      for (int x=0; x < 10; x++){
        delay(400);
        digitalWrite(RED_PIN, 0);
        delay(100);
        digitalWrite(RED_PIN, 1);
      }
      if (reconnect_N > 60) {
        // exit after 5 minutes, go back to the main loop
        return;
      }
    }
  }
}


/********************************** Main Loop *****************************************/

void loop()
{  
  now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    //Confirm that still connected to MQTT broker
    if (!client.connected()) {
      if (now - last_mqtt_connected > 15000) {
        Serial1.println("Reconnecting to MQTT Broker");
        reconnect();
      }
    } else {
      last_mqtt_connected = now;
    }
  } else {
      digitalWrite(RED_PIN, 0);
      // Wait 5 seconds before retrying loop if their is no WIFI connection
      delay(5000);
      digitalWrite(RED_PIN, 1);
      return;
  }

  client.loop();
  if (mqtt_proccesed) {
    // potentially process up to 3 commands before starting transition simultaniously
    mqtt_proccesed = false;
    client.loop();
    if (mqtt_proccesed) {
      client.loop();
    }
    mqtt_proccesed = false;
  }
  
  // process OTA updates
  httpServer.handleClient();
  MDNS.update();

  // Post the full status to MQTT every 60000 miliseconds. This is roughly once a minute
  // Usually, clients will store the value internally.
  // This is only used if a client starts up again and did not receive previous messages
  if (now - last_publish_ms > 60000) {
    last_publish_ms = now;
    publishCombinedJsonState();
    publishRGBJsonState();
    publishWhiteJsonState();
    publishWhiteSingle1JsonState();
    publishWhiteSingle2JsonState();
    client.publish(MQTT_UP, MQTT_UP_online, true);
  }
  // Process UDP messages if needed
  UDP_loop();
  // Process transitions if needed
  Transition_loop();
  // Process Green/Red LED if needed
  Indicator_LED_loop();
}

void Indicator_LED_loop(void) {
  if (GREEN_state && now - GREEN_off_ms > 200) {
    digitalWrite(GREEN_PIN, 1);
  }
}

/********************************** Fading code *****************************************/

/* Inspired by: https://www.arduino.cc/en/Tutorial/ColorCrossfader */

void calculateNstep(int LED_index, int start_value, int target_value) {
  float N_step_float = (float(target_value - start_value))/transition_increment;
  n_step[LED_index] = (int) N_step_float;
  rest_step[LED_index] = (N_step_float-n_step[LED_index])*transition_increment;
  return;
}

float calculateStepTime(int N_step) {
  float Step_time_ms;
  if (abs(N_step)>1) {                                         // Do not divide by 0, 
    Step_time_ms = float(transition_time_s)*1000/(abs(N_step)-1); // sec --> ms, divide by number of steps minus first one (imediatly done)
  } else {
    Step_time_ms = 0;
  }      
  return Step_time_ms;
}

uint8_t ExecuteTransition(int LED_index, uint8_t LED_value, int LED_pin) {
  // Determine if it is time to do a transition step
  if (transition_ms>step_time_ms[LED_index]*(transitionStepCount[LED_index])+1){
    transitionStepCount[LED_index] = transitionStepCount[LED_index]+1;
    
    if (n_step[LED_index] > 0 && transitionStepCount[LED_index]<=abs(n_step[LED_index])){
        LED_value = LED_value+transition_increment;
    } else if (n_step[LED_index] < 0 && transitionStepCount[LED_index]<=abs(n_step[LED_index])){
        LED_value = LED_value-transition_increment;
    }

    // Defensive driving: make sure val stays in the range 0-255
    if (LED_value > 255) {
      LED_value = 255;
    } 
    else if (LED_value < 0) {
      LED_value = 0;
    }

    // Write current values to LED pins
    setLEDpin(LED_pin, LED_value);
  }
  
  return LED_value;
}

void Transition(void) {
  // Get current value as start, if already transitioning the transition variables will be up to date.
  if (!transitioning && !UDP_stream) {
    get_transition_state_from_begin();
  }

  // Get the target
  get_target_from_m_state(); 

  // Determine the transition_increment (if the transition is faster than 100ms make bigger steps to keep up)
  if (transition_time_s > 0.1) {
      transition_increment = 1;
  } else {
      transition_increment = round(0.2/transition_time_s);
  }
  if (transition_increment>255){
      transition_increment = 255;
  }
  if (transition_increment<1){
      transition_increment = 1;
  }

  // Calculte the number of steps and put it in an array [R,G,B,W1,W2]
  calculateNstep(0, transition_red, targetR);
  calculateNstep(1, transition_green, targetG);
  calculateNstep(2, transition_blue, targetB);
  calculateNstep(3, transition_w1, targetW1);
  calculateNstep(4, transition_w2, targetW2);
  
  for (int i=0; i < 5; i++){
    // Calculte the time between steps at which the LED value schould be increased/decreased by 1
    step_time_ms[i] = calculateStepTime(n_step[i]);
    // Set the intial step counters
    transitionStepCount[i] = 0;
  }
  
  // Do the first transition step (remainder) if needed, to ensure the end value will be correct if a transition_increment>1 is used.
  transition_red = transition_red + rest_step[0];
  transition_green = transition_green + rest_step[1];
  transition_blue = transition_blue + rest_step[2];
  transition_w1 = transition_w1 + rest_step[3];
  transition_w2 = transition_w2 + rest_step[4];
  setRGB(transition_red, transition_green, transition_blue);
  setW1(transition_w1);
  setW2(transition_w2);
  
  // Set variables for beginning transition
  start_transition_loop_ms = now;
  last_transition_publish = now;
  transitioning = true;
}

void Transition_loop(void) {
  if (transitioning) {
    transition_ms = now - start_transition_loop_ms;
    
    // Execute the actual transition if it is time within the main-loop for each of the LEDS [R,G,B,W1,W2]
    transition_red = ExecuteTransition(0, transition_red, RGB_LIGHT_RED_PIN);
    transition_green = ExecuteTransition(1, transition_green, RGB_LIGHT_GREEN_PIN);
    transition_blue = ExecuteTransition(2, transition_blue, RGB_LIGHT_BLUE_PIN);
    transition_w1 = ExecuteTransition(3, transition_w1, W1_PIN);
    transition_w2 = ExecuteTransition(4, transition_w2, W2_PIN);

    // at the end of a color transition
    if (transitionStepCount[0]>=abs(n_step[0]) && transitionStepCount[1]>=abs(n_step[1]) && transitionStepCount[2]>=abs(n_step[2]) && transitionStepCount[3]>=abs(n_step[3]) && transitionStepCount[4]>=abs(n_step[4])) {
      transitioning = false;
      if(!UDP_stream) {
        setWhite();
        setColor();
        last_publish_ms = now;
        publishCombinedJsonState();
        publishRGBJsonState();
        publishWhiteJsonState();
        publishWhiteSingle1JsonState();
        publishWhiteSingle2JsonState();
      }
    }

    // publish the state during a transition each 15 seconds
    if (now - last_transition_publish > 15000) {
      last_transition_publish = now;
      if(!UDP_stream) {
        last_publish_ms = now;
        publish_from_transition_state();
      }
    }
  }
}

void  get_target_from_m_state(void) {
  // get RGB state for the transition from the normal state variables
  if (m_rgb_state) {
    targetR = map(m_rgb_red, 0, 255, 0, m_rgb_brightness); 
    targetG = map(m_rgb_green, 0, 255, 0, m_rgb_brightness); 
    targetB = map(m_rgb_blue, 0, 255, 0, m_rgb_brightness);
  } else {
    targetR = 0;
    targetG = 0;
    targetB = 0;
  }
  // get White state for the transition from the normal state variables
  if (!m_white_single_mode) {
    if (m_white_state) {
      targetW1 = map(m_w1, 0, 255, 0, m_white_brightness);
      targetW2 = map(m_w2, 0, 255, 0, m_white_brightness);
    } else {
      targetW1 = 0;
      targetW2 = 0;
    }
  } else {
    if (m_white_single1_state) {
      targetW1 = m_w1;
    } else {
      targetW1 = 0;
    }
    if (m_white_single2_state) {
      targetW2 = m_w2;
    } else {
      targetW2 = 0;
    }
  }
}

void  get_transition_state_from_begin(void) {
  // get RGB state for the transition from the normal state variables
  if (t_rgb_state_begin) {
    transition_red = map(t_red_begin, 0, 255, 0, t_rgb_brightness_begin); 
    transition_green = map(t_green_begin, 0, 255, 0, t_rgb_brightness_begin); 
    transition_blue = map(t_blue_begin, 0, 255, 0, t_rgb_brightness_begin);
  } else {
    transition_red = 0;
    transition_green = 0;
    transition_blue = 0;
  }
  // get White state for the transition from the normal state variables
  if (!t_white_single_mode_begin){
    if (t_white_state_begin) {
      transition_w1 = map(t_w1_begin, 0, 255, 0, t_white_brightness_begin);
      transition_w2 = map(t_w2_begin, 0, 255, 0, t_white_brightness_begin);
    } else {
      transition_w1 = 0;
      transition_w2 = 0;
    }
  } else {
    if (t_white_single1_state_begin) {
      transition_w1 = t_w1_begin;
    } else {
      transition_w1 = 0;
    }
    if (t_white_single2_state_begin) {
      transition_w2 = t_w2_begin;
    } else {
      transition_w2 = 0;
    }
  }
}

void  publish_from_transition_state(void) {
  boolean p_white_state, p_rgb_state, p_s1_state, p_s2_state;
  uint8_t p_w1, p_w2, p_s1, p_s2, p_rgb_red, p_rgb_green, p_rgb_blue, p_white_brightness, p_rgb_brightness, p_combined_brightness;
  
  // get RGB variables from transition variabels
  if (transition_red == 0 && transition_green == 0 && transition_blue == 0) {
    // now RGB is off
    p_rgb_state = false;
    p_rgb_red = t_red_begin;
    p_rgb_green = t_green_begin;
    p_rgb_blue = t_blue_begin;
    p_rgb_brightness = t_rgb_brightness_begin;
  } else {
    p_rgb_state = true;
    p_rgb_brightness = std::max(transition_red, std::max(transition_green, transition_blue));
    p_rgb_red = map(transition_red, 0, m_rgb_brightness, 0, 255); 
    p_rgb_green = map(transition_green, 0, m_rgb_brightness, 0, 255); 
    p_rgb_blue = map(transition_blue, 0, m_rgb_brightness, 0, 255); 
  }
  // get white variables from transition variabels
  if (transition_w1 == 0 && transition_w2 == 0) {
    p_white_state = false;
    p_white_brightness = t_white_brightness_begin;
    p_w1 = t_w1_begin;
    p_w2 = t_w2_begin;
  } else {
    p_white_state = true;
    p_white_brightness = std::max(transition_w1, transition_w2);
    p_w1 = map(transition_w1, 0, p_white_brightness, 0, 255);
    p_w2 = map(transition_w2, 0, p_white_brightness, 0, 255);
  }

  if (transition_w1 == 0) {
    p_s1_state = false;
    p_s1 = t_w1_begin;
  } else {
    p_s1_state = true;
    p_s1 = transition_w1;
  }
  if (transition_w2 == 0) {
    p_s2_state = false;
    p_s2 = t_w2_begin;
  } else {
    p_s2_state = true;
    p_s2 = transition_w2;
  }

  p_combined_brightness = std::max(p_white_brightness, p_rgb_brightness);

  publishCombinedJsonStateVal(p_white_state, p_w1, p_w2, p_rgb_state, p_rgb_red, p_rgb_green, p_rgb_blue, p_combined_brightness);
  publishRGBJsonStateVal(p_rgb_state, p_rgb_red, p_rgb_green, p_rgb_blue, p_rgb_brightness);
  publishWhiteJsonStateVal(p_white_state, p_w1, p_w2, p_white_brightness);
  publishWhiteSingle1JsonStateVal(p_s1_state, p_s1);
  publishWhiteSingle2JsonStateVal(p_s2_state, p_s2);
}

/********************************** UDP/HDMI code *****************************************/
void  UDP_start_stop(void) {
  // check if the UDP multicast needs to be started or stopped
  if (UDP_stream == true && UDP_stream_begin == false) {
    ip = WiFi.localIP();
    Udp.beginMulticast(ip, UDP_IP, UDP_Port);
    transition_time_s = UDP_transition_time_s;
  } else if (UDP_stream == false && UDP_stream_begin == true) {
    Udp.stop();
  }
}

void  UDP_loop() {
  if (UDP_stream == true) {
    // check if there is an UDP message available
    if(Udp.parsePacket()) {
      // Get the lenght of the message
      UDP_packetSize = Udp.available();
      byte UDP_message[UDP_packetSize];
      // Read the message from the buffer
      Udp.read(UDP_message, UDP_packetSize);
      
      // Check if the message is long enough to extract the RGB value
      if (UDP_packetSize >= UDP_RGB_offset+3) {
        m_rgb_state = true;
        m_rgb_brightness = 255;
        m_rgb_red = UDP_message[UDP_RGB_offset];
        m_rgb_green = UDP_message[UDP_RGB_offset+1];
        m_rgb_blue = UDP_message[UDP_RGB_offset+2];
        if (transition_time_s <= 0) {
          setColor();
        } else {
          Transition();
        }
      }
    }
    if (now - last_publish_ms > 10000) {
      last_publish_ms = now;
      publishCombinedJsonState();
      publishRGBJsonState();
      publishWhiteJsonState();
      publishWhiteSingle1JsonState();
      publishWhiteSingle2JsonState();
    }
  }
}
