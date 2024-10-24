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
#include <iostream>
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <TaskScheduler.h>
#include <math.h>

// Constants for OLED and LEDs
#define OLED_CLOCK  15          
#define OLED_DATA   4
#define OLED_RESET  16
#define LED_PIN     5
#define NUM_LEDS    5

CRGB g_LEDs[NUM_LEDS] = {0};  // Frame buffer for FastLED

// Mesh network settings
#define MESH_PREFIX "esp32_mesh"
#define MESH_PASSWORD "mesh_password"
#define MESH_PORT 5555

// OLED Display object
U8G2_SSD1306_128X64_NONAME_F_HW_I2C g_OLED(U8G2_R2, OLED_RESET, OLED_CLOCK, OLED_DATA);
int g_lineHeight = 0;
int g_Brightness = 255;  // LED brightness scale
int g_PowerLimit = 3000;  // Power Limit for LEDs in milliWatts

// Variables to store the last 5 messages
const char * keys[5] = {"PM 1.0", "PM 2.5", "PM 10.0", "Temperature", "Humidity"};  // Keys for the data_map (these match the data positions in datum)
String datum[5] = {"2", "2", "2", "2", "2"};  // pm1.0, pm2.5, pm10.0, temp, hum
String suf[5] = {"ppm", "ppm", "ppm", "F", "%"};  // pm1.0, pm2.5, pm10.0, temp, hum
JsonDocument jsonReadings;
int runTry = 0;
int numTry = 0;

//String to send to other nodes with sensor readings
String readings;

Scheduler userScheduler;  // Task scheduler for painlessMesh
painlessMesh mesh;

// PMS7003 Serial Communication
HardwareSerial pmsSerial(2);  // Use Serial2 for PMS7003 (TX=17, RX=16)
// PMS7003 Constants
#define FRAME_LENGTH 32  // PMS7003 sends 32-byte data frame

// Function to update the OLED with the last 4 messages
void displayMessages() {
    g_OLED.clearBuffer();  // Clear the screen
    for (int i = 0; i < 5; i++) {
        g_OLED.setCursor(0, g_lineHeight * (i + 1));  // Display each message on a new line
        g_OLED.printf("%s: %s %s", keys[i], datum[i].c_str(), suf[i].c_str());
    }
    g_OLED.sendBuffer();  // Send the updated buffer to the OLED
}

void readPMS7003Data();

void runOn(int num) {
    if (runTry > num) {
        runTry = 0;
    }
    if (runTry < num) {
        runTry += 1;
    } else {
        readPMS7003Data();
        runTry += 1;
    }
}

// Function to read data from PMS7003 and store in datum array
void readPMS7003Data() {
    if (pmsSerial.available() >= FRAME_LENGTH) {
        uint8_t buffer[FRAME_LENGTH];
        pmsSerial.readBytes(buffer, FRAME_LENGTH);

        // Verify frame start bytes and checksum
        if (buffer[0] == 0x42 && buffer[1] == 0x4d) {
            uint16_t checksum = 0;
            for (int i = 0; i < FRAME_LENGTH - 2; i++) {
                checksum += buffer[i];
            }
            uint16_t frameChecksum = (buffer[30] << 8) | buffer[31];
            if (checksum == frameChecksum) {
                // Extract PM1.0, PM2.5, and PM10.0 values
                uint16_t pm1_0 = (buffer[10] << 8) | buffer[11];
                uint16_t pm2_5 = (buffer[12] << 8) | buffer[13];
                uint16_t pm10_0 = (buffer[14] << 8) | buffer[15];

                // Store the values in the datum array
                datum[0] = String(pm1_0);
                datum[1] = String(pm2_5);
                datum[2] = String(pm10_0);

                displayMessages();  // Update OLED with new values
            }
        }
    }
}

// User stub
void sendMessage() ; // Prototype so PlatformIO doesn't complain
String getReadings(); // Prototype for sending sensor readings

// Periodic task to send a message
Task taskSendMessage(TASK_SECOND * 10 , TASK_FOREVER, &sendMessage);

String readingsToJSON () {
    for (int i = 0; i < 5; i++) {
        jsonReadings[keys[i]] = datum[i];
    }
    
    serializeJson(jsonReadings, readings);
    return readings;
}

void sendMessage () {
    String msg = readingsToJSON();
    mesh.sendBroadcast(msg);
}

// Needed for painless library
void receivedCallback( uint32_t from, String &msg ) {
    // Ignore messages from this node itself
    if (from == mesh.getNodeId()) {
        return;  // Ignore message
    }

    Serial.printf("Received from %u msg=%s\n", from, msg.c_str());
    
    // JSON input string.
    const char* json = msg.c_str();
    // Deserialize the JSON document
    DeserializationError error = deserializeJson(jsonReadings, json);

    // Test if parsing succeeds.
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }

    Serial.print("Node: ");
    Serial.println(from);
    
    for (int i = 0; i < 5; i++) {    
        datum[i] = jsonReadings[keys[i]].as<String>();
        Serial.print(keys[i]);
        Serial.print(": ");
        Serial.print(datum[i]);
        Serial.print(" ");
        Serial.println(suf[i]);
    }

    displayMessages();
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

    // Initialize PMS7003 Serial communication
    // pmsSerial.begin(9600, SERIAL_8N1, 16, 17);  // TX=17, RX=16

    displayMessages();
}

void loop() {
    // Keep the mesh network alive
    mesh.update();
    // runOn(5);
}