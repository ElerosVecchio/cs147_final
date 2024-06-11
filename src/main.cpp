#include <Arduino.h>
#include <HttpClient.h>
#include <WiFi.h>
#include <inttypes.h>
#include <stdio.h>

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "DHT20.h"
#include <TFT_eSPI.h>       // Include the graphics library
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

#define PIR_PIN 2

TFT_eSPI tft = TFT_eSPI();  // Create object "tft"
DHT20 dht;

/*

TODO

- set target temperature - Bluetooth
- user set on/off - Bluetooth
- PID sensing
- LCD Visualization
- Bluetooth commands

*/

const float temp_threshold = 1.1;
float current_temperature = 0.0;
int target_temperature = 22;
bool on_setting = false;
bool ac_on = false;
bool heat_on = false;
bool person_detected = false;
unsigned long person_last_seen = 0;
float low_thresh;
float high_thresh;
bool do_draw = false;
unsigned long last_loop_time = 0;

// Drawing settings
uint16_t temp_color = TFT_GREEN;
uint16_t bg_color = TFT_BLACK;       // This is the background colour used for smoothing (anti-aliasing)

uint16_t temp_x = 0;  // Position of centre of arc
uint16_t temp_y = 0;

uint8_t temp_radius       = 40; // Outer arc radius
uint8_t temp_thickness    = temp_radius / 4;     // Thickness
uint8_t temp_inner_radius = temp_radius - temp_thickness;

// 0 degrees is at 6 o'clock position
uint16_t temp_start_angle = 90; // Start angle must be in range 0 to 360
uint16_t temp_end_angle   = 270; // End angle must be in range 0 to 360

uint16_t goal_start_angle = 90;
uint16_t goal_end_angle = 270;

uint16_t goal_color = TFT_BLUE;
uint16_t goal_x = 0;
uint16_t goal_y = 0;

uint16_t char_start_x = 0;
uint16_t char_start_y = 0;

void set_locations() {
  temp_x = tft.width() / 2 - 70;
  temp_y = tft.height() / 2 - 20;
  goal_x = temp_x;
  goal_y = temp_y + 65;
  char_start_x = tft.width() / 2 + 10;
  char_start_y = temp_y - 35;
}

bool smooth = random(2); // true = smooth sides, false = no smooth sides

// This example downloads the URL "http://arduino.cc/"
char ssid[50]; // your network SSID (name)
char pass[50]; // your network password (use for WPA, or use as key for WEP)
const char kHostname[] = "18.223.20.205";
// Number of milliseconds to wait without receiving any data before we give up
const int kNetworkTimeout = 30 * 1000;
// Number of milliseconds to wait if no data is available before trying again
const int kNetworkDelay = 1000;

void update_thresholds() {
  low_thresh = target_temperature - temp_threshold;
  high_thresh = target_temperature + temp_threshold;
}

void turn_ac_heat_off() {
  heat_on = false;
  ac_on = false;
  do_draw = true;
}

void turn_heat_on() {
  heat_on = true;
  ac_on = false;
  do_draw = true;
}

void turn_ac_on() {
  ac_on = true;
  heat_on = false;
  do_draw = true;
}

float f_to_c(float degrees) {
  return ((degrees - 32) * 5.0) / 9.0;
}

float c_to_f(float degrees) {
  return ((degrees * 9.0) / 5.0) + 32;
}

void draw_current_temperature() {
  float gradient = current_temperature / 40.0;
  float end_location = (gradient * 180.0) + 90.0;
  tft.drawArc(temp_x, temp_y, temp_radius, temp_inner_radius, temp_start_angle, temp_end_angle, TFT_DARKGREY, bg_color, smooth);
  tft.setTextColor(TFT_WHITE);
  tft.drawArc(temp_x, temp_y, temp_radius, temp_inner_radius, temp_start_angle, end_location, temp_color, bg_color, smooth);
  tft.drawNumber(current_temperature,temp_x-temp_thickness-5,temp_y-temp_thickness);
}

void draw_target_temperature() {
  float gradient = target_temperature / 40.0;
  float end_location = (gradient * 180.0) + 90.0;
  tft.drawArc(goal_x, goal_y, temp_radius, temp_inner_radius, goal_start_angle, goal_end_angle, TFT_DARKGREY, bg_color, smooth);
  tft.setTextColor(TFT_WHITE);
  tft.drawArc(goal_x, goal_y, temp_radius, temp_inner_radius, goal_start_angle, end_location, goal_color, bg_color, smooth);
  tft.drawNumber(target_temperature,goal_x-temp_thickness-5,goal_y-temp_thickness);
}

void draw_status() {
  if (ac_on) {
    tft.drawChar('A', char_start_x, char_start_y);
    tft.drawChar('C', char_start_x + 20, char_start_y);
    tft.drawChar('O', char_start_x + 45, char_start_y);
    tft.drawChar('N', char_start_x + 65, char_start_y);    
  } else if (heat_on) {
    tft.drawChar('H', char_start_x, char_start_y);
    tft.drawChar('E', char_start_x + 20, char_start_y);
    tft.drawChar('A', char_start_x + 40, char_start_y);
    tft.drawChar('T', char_start_x + 60, char_start_y);
    tft.drawChar('O', char_start_x, char_start_y + 25);
    tft.drawChar('N', char_start_x + 20, char_start_y + 25);
  } else if (on_setting) {
    tft.drawChar('I', char_start_x, char_start_y);
    tft.drawChar('D', char_start_x + 20, char_start_y);
    tft.drawChar('L', char_start_x + 40, char_start_y);
    tft.drawChar('E', char_start_x + 60, char_start_y);
  } else {
    tft.drawChar('O', char_start_x, char_start_y);
    tft.drawChar('F', char_start_x + 20, char_start_y);
    tft.drawChar('F', char_start_x + 40, char_start_y);
  }
}

void draw_screen() {
  tft.fillScreen(TFT_BLACK);
  draw_current_temperature();
  draw_target_temperature();
  draw_status();
}

void target_up() {
  target_temperature++;
  update_thresholds();
  do_draw = true;
}

void target_down() {
  target_temperature--;
  update_thresholds();
  do_draw = true;
}

class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    
    if (value == "on") {
      on_setting = true;
      do_draw = true;
    } else if (value == "off") {
      on_setting = false;
      do_draw = true;
    } else if (value == "up") {
      target_up();
      do_draw = true;
    } else if (value == "down") {
      target_down();
      do_draw = true;
    }

  }
};

void nvs_access() {
  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  // Open
  Serial.printf("\n");
  Serial.printf("Opening Non-Volatile Storage (NVS) handle... ");
  nvs_handle_t my_handle;
  err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err != ESP_OK) {
    Serial.printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
  } else {
    Serial.printf("Done\n");
    Serial.printf("Retrieving SSID/PASSWD\n");
    size_t ssid_len;
    size_t pass_len;
    err = nvs_get_str(my_handle, "ssid", ssid, &ssid_len);
    err |= nvs_get_str(my_handle, "pass", pass, &pass_len);
    switch (err) {
      case ESP_OK:
        Serial.printf("Done\n");
        //Serial.printf("SSID = %s\n", ssid);
        //Serial.printf("PASSWD = %s\n", pass);
        break;
      case ESP_ERR_NVS_NOT_FOUND:
        Serial.printf("The value is not initialized yet!\n");
        break;
      default:
        Serial.printf("Error (%s) reading!\n", esp_err_to_name(err));
      }
  }

  // Close
  nvs_close(my_handle);
}

void setup() {
  Serial.begin(9600);
  delay(1000);
  // Retrieve SSID/PASSWD from flash before anything else
  nvs_access();
  // We start by connecting to a WiFi network
  delay(1000);
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("MAC address: ");
  Serial.println(WiFi.macAddress());

  BLEDevice::init("Anteater Nest");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE
    );
  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->setValue("Send Commands Here");
  pService->start();
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->start();

  // My stuff
  pinMode(PIR_PIN, INPUT);
  Wire.begin();
  dht.begin();
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(3);
  set_locations();

  update_thresholds();
  dht.read();
  current_temperature = dht.getTemperature();
  draw_screen();
}

void loop() {

  // PIR detection - every 5 sec
  if (millis() - last_loop_time >= 5000) {

    int pir_value = digitalRead(PIR_PIN);
    if (pir_value == HIGH) {
      // person detected
      person_detected = true;
      person_last_seen = millis();
    } else {
      // person not detected
      if (millis() - person_last_seen >= 20000) {
        // buffer in case person comes back soon or something
        person_detected = false;
      }
    }

    last_loop_time = millis();
  }

  // main body - every 1s
  if (millis() - dht.lastRead() >= 1000) {

    dht.read();
    if (current_temperature != dht.getTemperature()) {
      current_temperature = dht.getTemperature();
      do_draw = true;
    }
    Serial.println(current_temperature);

    // check thresholds and change state
    if ((on_setting == false) || (person_detected == false)) {
      turn_ac_heat_off();
    } else if ((on_setting == true) && (low_thresh <= current_temperature) && (current_temperature <= high_thresh)) {
      turn_ac_heat_off();
    } else if ((on_setting == true) && (current_temperature < low_thresh) && (heat_on == false)) {
      turn_heat_on();
    } else if ((on_setting == true) && (current_temperature > high_thresh) && (ac_on == false)) {
      turn_ac_on();
    }
    
    // http request
    std::string vars = "/submit?temp=" + std::to_string(current_temperature);

    int err = 0;
    WiFiClient c;
    HttpClient http(c);
    //err = http.get(kHostname, kPath);
    err = http.get(kHostname, 5000, vars.c_str(), NULL);
    if (err == 0) {
      Serial.println("startedRequest ok");
      err = http.responseStatusCode();
      if (err >= 0) {
        Serial.print("Got status code: ");
        Serial.println(err);
        // Usually you'd check that the response code is 200 or a
        // similar "success" code (200-299) before carrying on,
        // but we'll print out whatever response we get
        err = http.skipResponseHeaders();
        if (err >= 0) {
          int bodyLen = http.contentLength();
          Serial.print("Content length is: ");
          Serial.println(bodyLen);
          Serial.println();
          Serial.println("Body returned follows:");
          // Now we've got to the body, so we can print it out
          unsigned long timeoutStart = millis();
          char c;
          // Whilst we haven't timed out & haven't reached the end of the body
          while ((http.connected() || http.available()) && ((millis() - timeoutStart) < kNetworkTimeout)) {
            if (http.available()) {
              c = http.read();
              // Print out this character
              Serial.print(c);
              bodyLen--;
              // We read something, reset the timeout counter
              timeoutStart = millis();
            } else {
              // We haven't got any data, so let's pause to allow some to
              // arrive
              delay(kNetworkDelay);
            }
          }
        } else {
          Serial.print("Failed to skip response headers: ");
          Serial.println(err);
        }
      } else {
        Serial.print("Getting response failed: ");
        Serial.println(err);
      }
    } else {
      Serial.print("Connect failed: ");
      Serial.println(err);
    }
    http.stop();
    // And just stop, now that we've tried a download
  }

  // LEAVE THIS AT THE VERY BOTTOM
  if (do_draw == true) {
    draw_screen();
    do_draw = false;
  }
}