//+--------------------------------------------------------------------------
//
// Air Quality Monitor - (c) 2024 Chris Londal.  All Rights Reserved.
//
// File:        main.cpp
//
// Description:
//
//   Code for air quality sensor
//
// History:     Sep-30-2024     cfl429      Created
//---------------------------------------------------------------------------

#include <Arduino.h>
#include <U8g2lib.h>  // For text on the OLED
#include <FastLED.h>  // FastLED library for controlling LEDs
#include <painlessMesh.h>  // Mesh networking library
#include <WiFi.h>
#include <iostream>
#include <HardwareSerial.h>

// #include <inclusions.h>
// #include <datum.h>
// #include <file.h>

// Constants for OLED and LEDs
#define OLED_CLOCK  15          
#define OLED_DATA   4
#define OLED_RESET  16
#define LED_PIN     5
#define NUM_LEDS    48

CRGB g_LEDs[NUM_LEDS] = {0};  // Frame buffer for FastLED

// Mesh network settings
#define MESH_PREFIX "esp32_mesh"
#define MESH_PASSWORD "mesh_password"
#define MESH_PORT 5555

Scheduler userScheduler;  // Task scheduler for painlessMesh
painlessMesh mesh;

// OLED Display object
U8G2_SSD1306_128X64_NONAME_F_HW_I2C g_OLED(U8G2_R2, OLED_RESET, OLED_CLOCK, OLED_DATA);
int g_lineHeight = 0;
int g_Brightness = 255;  // LED brightness scale
int g_PowerLimit = 3000;  // Power Limit for LEDs in milliWatts

// Variables to store the last 5 messages
String keys[5] = {"pm1.0", "pm2.5", "pm10.0", "temp", "hum"};  // Keys for the data_map (these match the data positions in datum)
String datum[5] = {"0", "0", "0", "0", "1"};  // pm1.0, pm2.5, pm10.0, temp, hum

//String to send to other nodes with sensor readings
String readings;

// PMS7003 Serial Communication
HardwareSerial pmsSerial(2);  // Use Serial2 for PMS7003 (TX=17, RX=16)
// PMS7003 Constants
#define FRAME_LENGTH 32  // PMS7003 sends 32-byte data frame

// Function to update the OLED with the last 4 messages
void displayMessages() {
    g_OLED.clearBuffer();  // Clear the screen
    for (int i = 0; i < 5; i++) {
        g_OLED.setCursor(0, g_lineHeight * (i + 1));  // Display each message on a new line
        g_OLED.printf("%p %s", keys[i].c_str(), datum[i].c_str());
    }
    g_OLED.sendBuffer();  // Send the updated buffer to the OLED
}

// User stub
void sendMessage() ; // Prototype so PlatformIO doesn't complain
String getReadings(); // Prototype for sending sensor readings

// Periodic task to send a message
Task taskSendMessage(TASK_SECOND * 10 , TASK_FOREVER, &sendMessage);

String readingsToJSON () {
  JSONVar jsonReadings;
  jsonReadings["node"] = mesh.getNodeId();
  jsonReadings["pm1.0"] = datum[0];
  jsonReadings["pm2.5"] = datum[1];
  jsonReadings["pm10.0"] = datum[2];
  jsonReadings["temp"] = datum[3];
  jsonReadings["hum"] = datum[4];
  readings = JSON.stringify(jsonReadings);
  return readings;
}

void sendMessage () {
  String msg = readingsToJSON();
  mesh.sendBroadcast(msg);
}

// Needed for painless library
void receivedCallback( uint32_t from, String &msg ) {
  Serial.printf("Received from %u msg=%s\n", from, msg.c_str());
  JSONVar myObject = JSON.parse(msg.c_str());
  int node = myObject["node"];
  datum[0] = myObject["pm1.0"];
  datum[1] = myObject["pm2.5"];
  datum[2] = myObject["pm10.0"];
  datum[3] = myObject["temp"];
  datum[4] = myObject["hum"];
  Serial.print("Node: ");
  Serial.println(node);
  Serial.print("PM 1.0: ");
  Serial.print(datum[0]);
  Serial.println(" ppm");
  Serial.print("PM 2.5: ");
  Serial.print(datum[1]);
  Serial.println(" ppm");
  Serial.print("PM 10.0: ");
  Serial.print(datum[2]);
  Serial.println(" ppm");
  Serial.print("Temperature: ");
  Serial.print(datum[3]);
  Serial.println(" C");
  Serial.print("Humidity: ");
  Serial.print(datum[4]);
  Serial.println(" %");
}

void newConnectionCallback(uint32_t nodeId) {
  Serial.printf("--> startHere: New Connection, nodeId = %u\n", nodeId);
}

void changedConnectionCallback() {
  Serial.printf("Changed connections\n");
}

void nodeTimeAdjustedCallback(int32_t offset) {
  Serial.printf("Adjusted time %u. Offset = %d\n", mesh.getNodeTime(),offset);
}

void setup() {
    // Serial for debugging
    Serial.begin(115200);

    while (!Serial) { }

    // Initialize OLED display
    g_OLED.begin();
    g_OLED.clear();
    g_OLED.setFont(u8g2_font_profont15_tf);
    g_lineHeight = g_OLED.getFontAscent() - g_OLED.getFontDescent();

    // Initialize FastLED
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(g_LEDs, NUM_LEDS);
    FastLED.setBrightness(g_Brightness);
    FastLED.setMaxPowerInMilliWatts(g_PowerLimit);

    mesh.setDebugMsgTypes( ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE ); // all types on
    // mesh.setDebugMsgTypes( ERROR | STARTUP );  // set before init() so that you can see startup messages

    // Initialize painlessMesh
    mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);

    // Assign all the callback functions to their corresponding events.
    mesh.onReceive(&receivedCallback);  // Set the callback for receiving messages
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onChangedConnections(&changedConnectionCallback);
    mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

    // Add the task to send messages periodically
    userScheduler.addTask(taskSendMessage);
    taskSendMessage.enable();
    displayMessages();
}

void loop() {
    // Keep the mesh network alive
    mesh.update();
}
