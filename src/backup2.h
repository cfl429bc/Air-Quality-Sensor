//+--------------------------------------------------------------------------
//
// Air Quality Monitor - (c) 2024 Chris Londal.  All Rights Reserved.
//
// File:        backup2.h
//
// Description:
//
//   Backup code for air quality sensor
//
// History:     Oct-12-2024     cfl429      Created
//---------------------------------------------------------------------------

#include <Arduino.h>
#include <U8g2lib.h>  // For text on the OLED
#include <FastLED.h>  // FastLED library for controlling LEDs
#include <painlessMesh.h>  // Mesh networking library
#include <WiFi.h>  // For Wi-Fi connectivity
#include <WebServer.h>  // Simple web server for monitoring
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <TaskScheduler.h>
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

void displayMac() {
    g_OLED.clearBuffer();  // Clear the screen
    for (int i = 0; i < 5; i++) {
        g_OLED.setCursor(0, g_lineHeight * (i + 1));  // Display each message on a new line
        if (i == 0) {
            g_OLED.printf("MAC Address: ");
        } else if (i == 1) {
            g_OLED.printf("%s", WiFi.macAddress().c_str());
        } else if (i == 2) {
            g_OLED.printf("Node Id: ");
        } else if (i == 3) {
            g_OLED.printf("1");
        }
    }
    g_OLED.sendBuffer();  // Send the updated buffer to the OLED
}

// User stub
void sendMessage();  // Prototype so PlatformIO doesn't complain
String getReadings();  // Prototype for sending sensor readings

// Periodic task to send a message
Task taskSendMessage(TASK_SECOND * 10, TASK_FOREVER, &sendMessage);

String readingsToJSON() {
    for (int i = 0; i < 5; i++) {
        jsonReadings[keys[i]] = datum[i];
    }
    
    serializeJson(jsonReadings, readings);
    return readings;
}

void sendMessage() {
    String msg = readingsToJSON();
    mesh.sendBroadcast(msg);
}

// Needed for painless library
void receivedCallback(uint32_t from, String &msg) {
    if (from == mesh.getNodeId()) {
        return;  // Ignore message from self
    }

    Serial.printf("Received from %u msg=%s\n", from, msg.c_str());
    
    const char* json = msg.c_str();
    DeserializationError error = deserializeJson(jsonReadings, json);

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

    // Update the display
    displayMessages();
}

// Wi-Fi initialization and web server setup
void setupWiFi() {
    Serial.print("Connecting to Wi-Fi...");

    // Disconnect from any previous Wi-Fi connections
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);  // Set to Station mode

    // WPA2 Enterprise requires EAP (Extensible Authentication Protocol)
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)identity, strlen(identity));  // Identity (username)
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)identity, strlen(identity));  // Some networks need this too
    esp_wifi_sta_wpa2_ent_set_password((uint8_t *)password, strlen(password));  // Password

    // WPA2 Enterprise setup
    esp_wifi_sta_wpa2_ent_enable();  // Enable WPA2 Enterprise authentication

    // Start the connection
    WiFi.begin(ssid);

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    // Once connected, print the IP address
    Serial.println("Wi-Fi connected!");
    Serial.println("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.println("Port: ");
    Serial.println(serverPort);
}



// Web page with auto-refresh
void handleRoot() {
    String html = "<html><head><title>Mesh Network Monitor</title>";
    html += "<meta http-equiv=\"refresh\" content=\"30\">";  // Auto-refresh every 30 seconds
    html += "</head><body><h1>Sensor Readings</h1><ul>";

    for (int i = 0; i < 5; i++) {
        html += "<li>" + String(keys[i]) + ": " + String(datum[i]) + " " + String(suf[i]) + "</li>";
    }
    html += "<ul></body></html>";
    server.send(200, "text/html", html);
}

// JSON endpoint for readings
void handleJson() {
    String jsonOutput;
    readingsToJSON();
    serializeJson(jsonReadings, jsonOutput);
    server.send(200, "application/json", jsonOutput);
}

// Start the web server
void startWebServer() {
    server.on("/", handleRoot);  // Serve web page at the root
    server.on("/api/readings", handleJson);  // Serve JSON at /api/readings
    server.begin();  // Start the server
    Serial.println("Web server started!");
}

void newConnectionCallback(uint32_t nodeId) {
    Serial.printf("--> startHere: New Connection, nodeId = %u\n", nodeId);
}

void changedConnectionCallback() {
    Serial.printf("Changed connections\n");
}

void nodeTimeAdjustedCallback(int32_t offset) {
    Serial.printf("Adjusted time %u. Offset = %d\n", mesh.getNodeTime(), offset);
}

void setup() {
    // Serial for debugging
    Serial.begin(115200);

    delay(5000);

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

    // Turn the display on and display MAC address until connected to the internet
    displayMac();

    // Initialize Wi-Fi and web server
    setupWiFi();
    startWebServer();

    // Initialize painlessMesh
    mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
    // mesh.setDebugMsgTypes(ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE);  // all types on
    mesh.setDebugMsgTypes( ERROR | STARTUP );  // set before init() so that you can see startup messages
    
    // Set the mesh callbacks
    mesh.onReceive(&receivedCallback);
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onChangedConnections(&changedConnectionCallback);
    mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

    // Add the task to send messages periodically
    userScheduler.addTask(taskSendMessage);
    taskSendMessage.enable();

    // Turn the display 
    displayMessages();

}

void loop() {
    // Keep the mesh network alive
    mesh.update();

    // Handle web server requests
    displayMessages();
    server.handleClient();
}