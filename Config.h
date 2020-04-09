//General
#define Module_Name "LED1"

//Wifi settings:
#define wifi_ssid_conf "WIFI_SSID_????"
#define wifi_password_conf "WIFI_PASSWORD_????"

//MQTT broker
#define mqtt_server_conf "192.168.1.???"
#define mqtt_user_conf "MQTT_USERNAME_??"
#define mqtt_password_conf "MQTT_PASSWORD_??"
#define mqtt_port_conf 1883

// MQTT topics
#define Mqtt_Base_Topic "LedStrip/" Module_Name

#define MQTT_JSON_LIGHT_RGB_STATE_TOPIC Mqtt_Base_Topic "/rgb/json_status"
#define MQTT_JSON_LIGHT_RGB_COMMAND_TOPIC Mqtt_Base_Topic "/rgb/json_set"

#define MQTT_JSON_LIGHT_WHITE_STATE_TOPIC Mqtt_Base_Topic "/white/json_status"
#define MQTT_JSON_LIGHT_WHITE_COMMAND_TOPIC Mqtt_Base_Topic "/white/json_set"

#define MQTT_JSON_LIGHT_COMBINED_STATE_TOPIC Mqtt_Base_Topic "/combined/json_status"
#define MQTT_JSON_LIGHT_COMBINED_COMMAND_TOPIC Mqtt_Base_Topic "/combined/json_set"

#define MQTT_JSON_LIGHT_SETTINGS_STATE_TOPIC Mqtt_Base_Topic "/settings/json_status"
#define MQTT_JSON_LIGHT_SETTINGS_COMMAND_TOPIC Mqtt_Base_Topic "/settings/json_set"

#define MQTT_UP  Mqtt_Base_Topic "/active"

//UDP/HDMI stream settings
const IPAddress UDP_IP(000, 00, 00, 0); // UDP multicast IP address, leave at 0 if you do not use UDP/HDMI streams
#define UDP_Port 00000                  // UDP multicast port number, leave at 0 if you do not use UDP/HDMI streams
#define UDP_RGB_offset 0                // offset in bytes in the UDP message where the byte coding for the RED color starts
#define UDP_transition_time_s 0.2       // transition time in seconds between color changes during UDP streams

//OTA update
#define OTA_port 80
#define OTA_hostname "OPA_update_H801_" Module_Name
#define OTA_update_path "/firmware"
#define OTA_username "OPA_USERNAME"
#define OTA_password "OPA_PASSWORD"

//Light setup
#define min_color_temp 153
#define max_color_temp 500
#define transition_time_s_conf 0.5       // transition time in seconds between color/brightness/white changes
uint8_t RGB_mixing[3] = {255,255,255};   // {255,255,255} is standard operation, lower the R, G or B value to change the balance between the colors of your LED strip
