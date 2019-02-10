//
// Alternative firmware for H801 5 channel LED dimmer
// based on https://github.com/open-homeautomation/h801/blob/master/mqtt/mqtt.ino
//      and https://github.com/bruhautomation/ESP-MQTT-JSON-Digital-LEDs/blob/master/ESP_MQTT_Digital_LEDs/ESP_MQTT_Digital_LEDs.ino
//

//IMPORTANT!!!!!!!!!!!!
// inside PubSubClient.h the folloing needs to be changed on line 26:
// PubSubClient.h is inside documents/Arduino/libraries/PubSubclient/src/PubSubClient.h
// #define MQTT_MAX_PACKET_SIZE 128 --> #define MQTT_MAX_PACKET_SIZE 600

#include <string>

#include <ESP8266WiFi.h>
#include <PubSubClient.h>       // MQTT client
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include "Config.h"

#define DEVELOPMENT

WiFiClient wifiClient;
PubSubClient client(wifiClient);

// Get settings from config.h
const char* mqtt_server = mqtt_server_conf;
const char* mqtt_user = mqtt_user_conf;
const char* mqtt_password = mqtt_password_conf;


/********************************** program variables  *****************************************/
char* chip_id = "00000000";
char* myhostname = "esp00000000";
uint8_t reconnect_N = 0;
unsigned long last_publish_ms = 0;

// transitioning variables
float transition_time_s_standard = transition_time_s_conf;
float transition_time_s = transition_time_s_conf;
unsigned long last_transition_loop_ms = 0;
unsigned long last_transition_publish = 0;
uint16_t transition_wait_ms = 1;
uint16_t transitionLoopCount = 0;
boolean transitioning = false;
int16_t transition_stepR = 0;
int16_t transition_stepG = 0; 
int16_t transition_stepB = 0;
int16_t transition_stepW1 = 0;
int16_t transition_stepW2 = 0;
uint8_t t_red_begin = 255;
uint8_t t_green_begin = 255;
uint8_t t_blue_begin = 255;
uint8_t t_rgb_brightness_begin = 255;
uint8_t t_white_brightness_begin = 255;
uint8_t t_w1_begin = 255;
uint8_t t_w2_begin = 255;
boolean t_rgb_state_begin = false;
boolean t_white_state_begin = false;
uint8_t targetR = 255;
uint8_t targetG = 255;
uint8_t targetB = 255;
uint8_t targetW1 = 255;
uint8_t targetW2 = 255;

// buffer used to send/receive data with MQTT
#define JSON_BUFFER_SIZE 600


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

// store the state of the rgb LED strip (colors, brightness, ...)
boolean m_rgb_state = false;
uint8_t m_rgb_brightness = 255;
uint8_t m_rgb_red = 255;
uint8_t m_rgb_green = 255;
uint8_t m_rgb_blue = 255;

// store the state of the white LED strip (colors, brightness, ...)
boolean m_white_state = false;
uint8_t m_white_brightness = 255;
uint16_t m_color_temp = 500;
uint8_t m_w1 = 255;
uint8_t m_w2 = 255;

// store aditional states for combined RGB White light
uint8_t m_combined_brightness = 255;
boolean m_white_mode = true;
String m_effect = "white_mode";

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
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid_conf, wifi_password_conf);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial1.println("Connection Failed! Rebooting...");
    Serial1.print(".");
    delay(30000);
    ESP.restart();
  }

  Serial1.println("");

  if (WiFi.status() == WL_CONNECTED) {
    Serial1.println("");
    Serial1.print("Connected to ");
    Serial1.println(wifi_ssid_conf);
    Serial1.print("IP address: ");
    Serial1.println(WiFi.localIP());
  }

  // init the MQTT connection
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // OTA
  // do not start OTA server if no password has been set
  if (OTA_password != "") {
    //Set up OTA
    ArduinoOTA.setPort(OTA_port);
    ArduinoOTA.setHostname(OTA_hostname);
    ArduinoOTA.setPassword(OTA_password);
    ArduinoOTA.onStart([]() {
      Serial1.println("Start OTA");
    });
    ArduinoOTA.onEnd([]() {
      Serial1.println("End OTA");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial1.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial1.printf("OTA Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
  }

  digitalWrite(RED_PIN, 1);
}


/********************************** LED strip control *****************************************/

void setRGB(uint8_t p_red, uint8_t p_green, uint8_t p_blue) {
  analogWrite(RGB_LIGHT_RED_PIN, p_red);
  analogWrite(RGB_LIGHT_GREEN_PIN, p_green);
  analogWrite(RGB_LIGHT_BLUE_PIN, p_blue);
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
    m_w1 = int(round(1-temp_unit*255));
    m_w2 = 255;
  } else if (temp_unit < 0){
    m_w1 = 255;
    m_w2 = int(round(1+temp_unit*255));
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
  if (m_white_state) {
    uint8_t w1_brightness = map(m_w1, 0, 255, 0, m_white_brightness);
    uint8_t w2_brightness = map(m_w2, 0, 255, 0, m_white_brightness);
    setW1(w1_brightness);
    setW2(w2_brightness);
  } else {
    setW1(0);
    setW2(0);
  }
}


/********************************** Publish states *****************************************/

void publishRGBJsonState() {
  StaticJsonBuffer<JSON_BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();

  root["state"] = (m_rgb_state) ? LIGHT_ON : LIGHT_OFF;
  JsonObject& color = root.createNestedObject("color");
  color["r"] = m_rgb_red;
  color["g"] = m_rgb_green;
  color["b"] = m_rgb_blue;
  
  root["brightness"] = m_rgb_brightness;


  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));

  client.publish(MQTT_JSON_LIGHT_RGB_STATE_TOPIC, buffer, true);
}

void publishWhiteJsonState() {
  StaticJsonBuffer<JSON_BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();

  root["state"] = (m_white_state) ? LIGHT_ON : LIGHT_OFF;
  
  m_color_temp = int(round((float(m_w2)/510 - float(m_w1)/510 + 0.5)*(max_color_temp - min_color_temp)) + min_color_temp);
  root["color_temp"] = m_color_temp;
  
  root["brightness"] = m_white_brightness;


  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));

  client.publish(MQTT_JSON_LIGHT_WHITE_STATE_TOPIC, buffer, true);
}

void publishCombinedJsonState() {
  StaticJsonBuffer<JSON_BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();

  root["state"] = (m_white_state || m_rgb_state) ? LIGHT_ON : LIGHT_OFF;

  m_color_temp = int(round((float(m_w2)/510 - float(m_w1)/510 + 0.5)*(max_color_temp - min_color_temp)) + min_color_temp);
  root["color_temp"] = m_color_temp;

  root["brightness"] = m_combined_brightness;

  JsonObject& color = root.createNestedObject("color");
  if (m_white_state && !m_rgb_state) {
    color["r"] = 255;
    color["g"] = 255;
    color["b"] = 255;
  } else {
    color["r"] = m_rgb_red;
    color["g"] = m_rgb_green;
    color["b"] = m_rgb_blue;
  }

  if (m_white_state && !m_rgb_state) {
    m_effect = "white_mode";
  } else if (!m_white_state && m_rgb_state) {
    m_effect = "color_mode";
  } else if (m_white_state && m_rgb_state) {
    m_effect = "both_mode";
  }
  root["effect"] = m_effect.c_str();

  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));

  client.publish(MQTT_JSON_LIGHT_COMBINED_STATE_TOPIC, buffer, true);
}

void publishJsonSettings() {
  StaticJsonBuffer<JSON_BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();

  root["transition_time_s_standard"] = transition_time_s_standard;

  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));

  client.publish(MQTT_JSON_LIGHT_SETTINGS_STATE_TOPIC, buffer, true);
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
  t_white_brightness_begin = m_white_brightness;
  t_w1_begin = m_w1;
  t_w2_begin = m_w2;

  // Handle RGB commands
  if (String(MQTT_JSON_LIGHT_RGB_COMMAND_TOPIC).equals(p_topic)) {
    if (!processRGBJson(message)) {
      return;
    }
    if (transition_time_s <= 0) {
      setColor();
    } else {
      Transition();
    }
    publishRGBJsonState();
  }

  // Handle White commands
  if (String(MQTT_JSON_LIGHT_WHITE_COMMAND_TOPIC).equals(p_topic)) {
    if (!processWhiteJson(message)) {
      return;
    }
    if (transition_time_s <= 0) {
      setWhite();
    } else {
      Transition();
    }
    publishWhiteJsonState();
  }

  // Handle combined commands
  if (String(MQTT_JSON_LIGHT_COMBINED_COMMAND_TOPIC).equals(p_topic)) {
    if (!processCombinedJson(message)) {
      return;
    }
    if (transition_time_s <= 0) {
      setWhite();
      setColor();
    } else {
      Transition();
    }
    publishCombinedJsonState();
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

  // Flash green LED
  digitalWrite(GREEN_PIN, 0);
  delay(1);
  digitalWrite(GREEN_PIN, 1);
}


/********************************** JSON processing *****************************************/

bool processRGBJson(char* message) {
  StaticJsonBuffer<JSON_BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.parseObject(message);

  if (!root.success()) {
    Serial1.println("parseObject() failed");
    return false;
  }

  // Turn RGB "ON" or "OFF"
  if (root.containsKey("state")) {
    if (strcmp(root["state"], LIGHT_ON) == 0) {
      m_rgb_state = true;
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


bool processWhiteJson(char* message) {
  StaticJsonBuffer<JSON_BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.parseObject(message);

  if (!root.success()) {
    Serial1.println("parseObject() failed");
    return false;
  }

  // Turn White "ON" or "OFF"
  if (root.containsKey("state")) {
    if (strcmp(root["state"], LIGHT_ON) == 0) {
      m_white_state = true;
    }
    else if (strcmp(root["state"], LIGHT_OFF) == 0) {
      m_white_state = false;
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
  StaticJsonBuffer<JSON_BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.parseObject(message);

  if (!root.success()) {
    Serial1.println("parseObject() failed");
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
      } else if (m_effect == "color_mode") {
        m_white_state = false;
        m_rgb_state = true;
        m_white_mode = false;
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

bool processJsonSettings(char* message) {
  StaticJsonBuffer<JSON_BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.parseObject(message);

  if (!root.success()) {
    Serial1.println("parseObject() failed");
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

  return true;
}


/********************************** MQTT connection *****************************************/

void reconnect() {
  // Loop until we're reconnected
  reconnect_N = 0;
  while (!client.connected()) {
    Serial1.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(chip_id, mqtt_user, mqtt_password)) {
      Serial1.println("connected");
      // blink 10 times green LED for success connected
      for (int x=0; x < 10; x++){
        delay(100);
        digitalWrite(GREEN_PIN, 0);
        delay(100);
        digitalWrite(GREEN_PIN, 1);
      }
      
      client.publish(MQTT_UP, chip_id);
      // Once connected, publish an announcement...
      // publish the initial values
      publishCombinedJsonState();
      publishRGBJsonState();
      publishWhiteJsonState();
      publishJsonSettings();
      // ... and resubscribe
      client.subscribe(MQTT_JSON_LIGHT_RGB_COMMAND_TOPIC);
      client.subscribe(MQTT_JSON_LIGHT_WHITE_COMMAND_TOPIC);
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
  if (WiFi.status() == WL_CONNECTED) {
    //Confirm that still connected to MQTT broker
    if (!client.connected()) {
      Serial1.println("Reconnecting to MQTT Broker");
      reconnect();
    }
  } else {
      digitalWrite(RED_PIN, 0);
      // Wait 5 seconds before retrying loop if their is no WIFI connection
      delay(5000);
      digitalWrite(RED_PIN, 1);
      return;
  }

  client.loop();
  
  // process OTA updates
  ArduinoOTA.handle();

  // Post the full status to MQTT every 60000 miliseconds. This is roughly once a minute
  // Usually, clients will store the value internally.
  // This is only used if a client starts up again and did not receive previous messages
  unsigned long now = millis();
  if (now - last_publish_ms > 60000) {
    last_publish_ms = now;
    publishCombinedJsonState();
    publishRGBJsonState();
    publishWhiteJsonState();
  }
  // Process transitions if needed
  Transition_loop(now);
}


/********************************** Fading code *****************************************/

/* Code from: https://www.arduino.cc/en/Tutorial/ColorCrossfader
* 
* The program works like this:
* Imagine a crossfade that moves the red LED from 0-10, 
*   the green from 0-5, and the blue from 10 to 7, in
*   ten steps.
*   We'd want to count the 10 steps and increase or 
*   decrease color values in evenly stepped increments.
*   Imagine a + indicates raising a value by 1, and a -
*   equals lowering it. Our 10 step fade would look like:
* 
*   1 2 3 4 5 6 7 8 9 10
* R + + + + + + + + + +
* G   +   +   +   +   +
* B     -     -     -
* 
* The red rises from 0 to 10 in ten steps, the green from 
* 0-5 in 5 steps, and the blue falls from 10 to 7 in three steps.
* 
* In the real program, the color percentages are converted to 
* 0-255 values, and there are 1020 steps (255*4).
* 
* To figure out how big a step there should be between one up- or
* down-tick of one of the LED values, we call calculateStep(), 
* which calculates the absolute gap between the start and end values, 
* and then divides that gap by 1020 to determine the size of the step  
* between adjustments in the value.
*/

int calculateStep(int startValue, int endValue) {
  int step = endValue - startValue; // What's the overall gap?
  if (step) {                      // If its non-zero, 
    step = 1020/step;              //   divide by 1020
  } 
  return step;
}

/* The next function is calculateVal. When the loop value, i,
*  reaches the step size appropriate for one of the
*  colors, it increases or decreases the value of that color by 1. 
*  (R, G, and B are each calculated separately.)
*/

int calculateVal(int step, int val, uint16_t i) {

  if ((step) && i % step == 0) { // If step is non-zero and its time to change a value,
    if (step > 0) {              //   increment the value if step is positive...
      val += 1;           
    } 
    else if (step < 0) {         //   ...or decrement it if step is negative
      val -= 1;
    } 
  }
  // Defensive driving: make sure val stays in the range 0-255
  if (val > 255) {
    val = 255;
  } 
  else if (val < 0) {
    val = 0;
  }
  return val;
}

/* crossFade() converts the percentage colors to a 
*  0-255 range, then loops 1020 times, checking to see if  
*  the value needs to be updated each time, then writing
*  the color values to the correct pins.
*/

void Transition(void) {
  // Get current value as start, if already transitioning the transition variables will be up to date.
  if (!transitioning) {
    get_transition_state_from_begin();
  }

  // Get the target
  get_target_from_m_state(); 

  // Calculate the interval in steps at which the LED value schould be increased/decreased by 1
  transition_stepR = calculateStep(transition_red, targetR);
  transition_stepG = calculateStep(transition_green, targetG); 
  transition_stepB = calculateStep(transition_blue, targetB);
  transition_stepW1 = calculateStep(transition_w1, targetW1);
  transition_stepW2 = calculateStep(transition_w2, targetW2); 

  // Calculate the delay inside each loop in ms to achieve the total transition time
  transition_wait_ms = int(round(float(transition_time_s)*1000/1020));

  // Set variables for beginning transition
  transitionLoopCount = 0;
  last_transition_loop_ms = 0;
  last_transition_publish = millis();
  transitioning = true;
}

void Transition_loop(unsigned long now) {
  if (transitioning) {
    // Execute the actual transition if it is time within the main-loop
    if (now - last_transition_loop_ms > transition_wait_ms) {
      last_transition_loop_ms = now;
  
      transition_red = calculateVal(transition_stepR, transition_red, transitionLoopCount);
      transition_green = calculateVal(transition_stepG, transition_green, transitionLoopCount);
      transition_blue = calculateVal(transition_stepB, transition_blue, transitionLoopCount);
      transition_w1 = calculateVal(transition_stepW1, transition_w1, transitionLoopCount);
      transition_w2 = calculateVal(transition_stepW2, transition_w2, transitionLoopCount);
  
      // Write current values to LED pins
      analogWrite(RGB_LIGHT_RED_PIN, transition_red);
      analogWrite(RGB_LIGHT_GREEN_PIN, transition_green);      
      analogWrite(RGB_LIGHT_BLUE_PIN, transition_blue);
      analogWrite(W1_PIN, transition_w1);
      analogWrite(W2_PIN, transition_w2);

      transitionLoopCount++; // increase transition loop i

      if (transitionLoopCount > 1020) {
        // at the end of a color transition
        transitioning = false;
        get_m_state_from_transition_state();
        setWhite();
        setColor();
        publishCombinedJsonState();
        publishRGBJsonState();
        publishWhiteJsonState();
      }
    }

    // publish the state during a transition each 15 seconds
    if (now - last_transition_publish > 15000) {
      last_transition_publish = now;
      get_m_state_from_transition_state();
      publishCombinedJsonState();
      publishRGBJsonState();
      publishWhiteJsonState();
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
  if (m_white_state) {
    targetW1 = map(m_w1, 0, 255, 0, m_white_brightness);
    targetW2 = map(m_w2, 0, 255, 0, m_white_brightness);
  } else {
    targetW1 = 0;
    targetW2 = 0;
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
  if (t_white_state_begin) {
    transition_w1 = map(t_w1_begin, 0, 255, 0, t_white_brightness_begin);
    transition_w2 = map(t_w2_begin, 0, 255, 0, t_white_brightness_begin);
  } else {
    transition_w1 = 0;
    transition_w2 = 0;
  }
}

void  get_m_state_from_transition_state(void) {
  // get RGB variables from transition variabels
  if (transition_red == 0 && transition_green == 0 && transition_blue == 0) {
    // now RGB is off
    m_rgb_state = false;
    m_rgb_red = t_red_begin;
    m_rgb_green = t_green_begin;
    m_rgb_blue = t_blue_begin;
    m_rgb_brightness = t_rgb_brightness_begin;
  } else {
    m_rgb_state = true;
    m_rgb_brightness = _max(transition_red, _max(transition_green, transition_blue));
    m_rgb_red = map(transition_red, 0, m_rgb_brightness, 0, 255); 
    m_rgb_green = map(transition_green, 0, m_rgb_brightness, 0, 255); 
    m_rgb_blue = map(transition_blue, 0, m_rgb_brightness, 0, 255); 
  }
  // get white variables from transition variabels
  if (transition_w1 == 0 && transition_w2 == 0) {
    m_white_state = false;
    m_white_brightness = t_white_brightness_begin;
    m_w1 = t_w1_begin;
    m_w2 = t_w2_begin;
  } else {
    m_white_state = true;
    m_white_brightness = _max(transition_w1, transition_w2);
    m_w1 = map(transition_w1, 0, m_white_brightness, 0, 255);
    m_w2 = map(transition_w2, 0, m_white_brightness, 0, 255);
  }
}


