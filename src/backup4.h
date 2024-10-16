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
#include <WiFi.h>  // For Wi-Fi connectivity
#include <WebServer.h>  // Simple web server for monitoring
#include <HardwareSerial.h>
#include <ArduinoJson.h>  // JSON parsing for sensor data
#include <TaskScheduler.h>  // Task scheduling library
#include <math.h>
#include <esp_wpa2.h>  // For WPA2 Enterprise networks


// Constants for OLED and LEDs
#define OLED_CLOCK  15          
#define OLED_DATA   4
#define OLED_RESET  16
#define LED_PIN     5
#define NUM_LEDS    48

CRGB g_LEDs[NUM_LEDS] = {5};  // Frame buffer for FastLED

// Mesh network settings
#define MESH_PREFIX "esp32_mesh"
#define MESH_PASSWORD "mesh_password"
#define MESH_PORT 5555

// Wi-Fi credentials
const char* ssid = "eduroam";  // Your Wi-Fi SSID
const char* identity = "londal@bc.edu";    // Your network username
const char* password = "Chris21bc";    // Your network password
int serverPort = 8080; // My server Port

// Web server object
WebServer server(serverPort);
// Variables for tracking connection status
bool wifiConnected = false;
unsigned long lastReconnectAttempt = 0;
unsigned long reconnectInterval = 5000;  // Start with 5 seconds between reconnection attempts

// Extensions
String links[2] = {"/", "/api/readings"};

// OLED Display object
U8G2_SSD1306_128X64_NONAME_F_HW_I2C g_OLED(U8G2_R2, OLED_RESET, OLED_CLOCK, OLED_DATA);
int g_lineHeight = 0;
int g_Brightness = 255;  // LED brightness scale
int g_PowerLimit = 3000;  // Power Limit for LEDs in milliWatts

// Variables to store the last 5 messages (air quality data)
const char * keys[5] = {"PM 1.0", "PM 2.5", "PM 10.0", "Temperature", "Humidity"};  // Keys for the data_map (these match the data positions in datum)
String datum[5] = {"2", "2", "2", "2", "2"};  // pm1.0, pm2.5, pm10.0, temp, hum
String suf[5] = {"ppm", "ppm", "ppm", "F", "%"};  // pm1.0, pm2.5, pm10.0, temp, hum
JsonDocument jsonReadings;  // JSON document to hold readings

//String to send to other nodes with sensor readings
String readings;

Scheduler userScheduler;  // Task scheduler for painlessMesh
painlessMesh mesh;  // Mesh network object

// PMS7003 Serial Communication
HardwareSerial pmsSerial(2);  // Use Serial2 for PMS7003 (TX=17, RX=16)
// PMS7003 Constants
#define FRAME_LENGTH 32  // PMS7003 sends 32-byte data frame

// Function to update the OLED with the last 5 messages
void displayMessages() {
    g_OLED.clearBuffer();  // Clear the screen
    for (int i = 0; i < 5; i++) {
        g_OLED.setCursor(0, g_lineHeight * (i + 1));  // Display each message on a new line
        g_OLED.printf("%s: %s %s", keys[i], datum[i].c_str(), suf[i].c_str());
    }
    g_OLED.sendBuffer();  // Send the updated buffer to the OLED
}

// Function to display the MAC address and node ID on the OLED screen
void displayMac() {
    g_OLED.clearBuffer();  // Clear the screen
    for (int i = 0; i < 5; i++) {
        g_OLED.setCursor(0, g_lineHeight * (i + 1));  // Display each message on a new line
        if (i == 0) {
            g_OLED.printf("MAC Address: ");
        } else if (i == 1) {
            g_OLED.printf("%s", WiFi.macAddress().c_str());  // Display the MAC address
        } else if (i == 2) {
            g_OLED.printf("Node Id: ");
        } else if (i == 3) {
            g_OLED.printf("1");  // Placeholder for node ID (can be dynamic)
        } else if (i == 4) {
            g_OLED.printf("Value");  // Placeholder for sensor value (can be dynamic)
        }
    }
    g_OLED.sendBuffer();  // Send the updated buffer to the OLED
}

void displayLinks() {
    g_OLED.clearBuffer();  // Clear the screen
    for (int i = 0; i < 5; i++) {
        g_OLED.setCursor(0, g_lineHeight * (i + 1));  // Display each message on a new line
        if (i == 0) {
            g_OLED.println("Wi-Fi connected!");
        } else if (i == 1) {
            g_OLED.print("IP Address: ");
        } else if (i == 2) {
            g_OLED.println(WiFi.localIP());  // Print the IP address once connected
        } else if (i == 3) {
            g_OLED.print("Port: ");
        } else if (i == 4) {
            g_OLED.println(serverPort);  // Print the port number
        }
    }
    g_OLED.sendBuffer();  // Send the updated buffer to the OLED
}

// User stub
void sendMessage();  // Prototype for message sending
String getReadings();  // Prototype for fetching sensor readings

// Set up generateLinks
void generateLinks() {
    for (int i = 0; i < (sizeof(links) / sizeof(links[0])); i++) {
        Serial.printf("http://");
        Serial.print(WiFi.localIP());
        Serial.print(":" + String(serverPort));
        Serial.println(links[i]);
    }
}

// Periodic task to send a message every 10 seconds
Task taskSendMessage(TASK_SECOND * 10, TASK_FOREVER, &sendMessage);

// Converts sensor readings to JSON format
String readingsToJSON() {
    for (int i = 0; i < 5; i++) {
        jsonReadings[keys[i]] = datum[i];
    }
    serializeJson(jsonReadings, readings);  // Serialize the JSON object into a string
    return readings;
}

// Sends the sensor readings as a broadcast to the mesh network
void sendMessage() {
    String msg = readingsToJSON();  // Convert readings to JSON
    mesh.sendBroadcast(msg);  // Broadcast the JSON readings to all mesh nodes
}

// Mesh network callback function for receiving messages
void receivedCallback(uint32_t from, String &msg) {
    if (from == mesh.getNodeId()) {
        return;  // Ignore message from self
    }

    Serial.printf("Received from %u msg=%s\n", from, msg.c_str());  // Print received message
    
    const char* json = msg.c_str();
    DeserializationError error = deserializeJson(jsonReadings, json);  // Deserialize the received JSON

    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }

    Serial.print("Node: ");
    Serial.println(from);  // Print the ID of the sender
    
    for (int i = 0; i < 5; i++) {
        datum[i] = jsonReadings[keys[i]].as<String>();  // Update local data with received values
        Serial.print(keys[i]);
        Serial.print(": ");
        Serial.print(datum[i]);
        Serial.print(" ");
        Serial.println(suf[i]);
    }

    // Update the display with new readings
    displayMessages();
}

// Mesh event callbacks
void newConnectionCallback(uint32_t nodeId) {
    Serial.printf("--> startHere: New Connection, nodeId = %u\n", nodeId);
}

void changedConnectionCallback() {
    Serial.printf("Changed connections\n");
}

void nodeTimeAdjustedCallback(int32_t offset) {
    Serial.printf("Adjusted time %u. Offset = %d\n", mesh.getNodeTime(), offset);
}

// Setup function to initialize all components
void setup() {
    // Serial for debugging
    Serial.begin(115200);
    delay(5000);  // Wait 5 seconds for Serial monitor to start

    while (!Serial) { }

    // Initialize OLED display
    g_OLED.begin();
    g_OLED.clear();
    g_OLED.setFont(u8g2_font_profont15_tf);  // Set the font
    g_lineHeight = g_OLED.getFontAscent() - g_OLED.getFontDescent();  // Calculate line height

    // Initialize FastLED
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(g_LEDs, NUM_LEDS);
    FastLED.setBrightness(g_Brightness);
    FastLED.setMaxPowerInMilliWatts(g_PowerLimit);

    // Display MAC address on the OLED screen until connected to the network
    displayMac();

    // Initialize Wi-Fi and start the web server
    setupWiFi();
    
    // Initialize mesh network
    mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
    mesh.setDebugMsgTypes(ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE);  // all types on
    // mesh.setDebugMsgTypes(ERROR | COMMUNICATION | STARTUP);  // Set debug message types
    
    // Set mesh event callbacks
    mesh.onReceive(&receivedCallback);
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onChangedConnections(&changedConnectionCallback);
    mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

    // Schedule the task to send messages every 10 seconds
    userScheduler.addTask(taskSendMessage);
    taskSendMessage.enable();

}

// Main loop to keep the mesh network and web server alive
void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        // If Wi-Fi is disconnected, stop the server and attempt reconnection
        if (wifiConnected) {
            Serial.println("Wi-Fi disconnected. Stopping web server...");
            stopWebServer();
            wifiConnected = false;
        }
        reconnectWiFi();  // Attempt reconnection
    } else {
        // If connected, handle web server requests normally
        server.handleClient();
    }
    mesh.update();  // Update mesh network status
    displayMessages();  // Continuously update the OLED display
}
