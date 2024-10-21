#include <Arduino.h>
#include <painlessMesh.h>
#include <FastLED.h>
#include <U8g2lib.h>
#include <TaskScheduler.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <map>
#include <esp_wpa2.h>  // For WPA2 Enterprise networks

// Constants for OLED and LEDs
#define OLED_CLOCK  15          
#define OLED_DATA   4
#define OLED_RESET  16
#define LED_PIN     5
#define NUM_LEDS    5  // Updated number of LEDs

// LED configuration
CRGB g_LEDs[NUM_LEDS] = {0};  // Updated frame buffer for FastLED

// Mesh network settings
#define MESH_PREFIX "esp32_mesh"
#define MESH_PASSWORD "mesh_password"
#define MESH_PORT 5555

// Wi-Fi credentials
const char* ssid = "eduroam";  // Your Wi-Fi SSID
const char* identity = "londal@bc.edu";    // Your network username
const char* password = "Chris21bc";    // Your network password
const char* thingSpeakAPI = "MFUTYNZY4VNR4JNA";
const char* server = "https://api.thingspeak.com/update?api_key=MFUTYNZY4VNR4JNA&field1=0";

// Variables for tracking connection status
bool wifiConnected = false;
unsigned long lastReconnectAttempt = 0;
unsigned long reconnectInterval = 5000;  // Start with 5 seconds between reconnection attempts

// OLED configuration
U8G2_SSD1306_128X64_NONAME_F_HW_I2C g_OLED(U8G2_R2, OLED_RESET, OLED_CLOCK, OLED_DATA);
int g_lineHeight = 0;  // Height for text lines on OLED
int g_Brightness = 255;  // LED brightness scale
int g_PowerLimit = 3000;  // Power Limit for LEDs in milliWatts

// Keys, data, and suffix arrays for sensor values
String keys[5] = {"PM 1.0", "PM 2.5", "PM 10.0", "Temperature", "Humidity"};  // Keys for data
String datum[5] = {"1", "2", "3", "4", "5"};  // pm1.0, pm2.5, pm10.0, temp, hum (placeholder values)
String suf[5] = {"ppm", "ppm", "ppm", "F", "%"};  // Suffixes for readings

// Scheduler delay in seconds
int schedulerDelay = 10;

// Mesh network object
Scheduler userScheduler;
painlessMesh mesh;

// User stubs
void sendMessage();
void uploadMessage();
void updateLEDs();
void updateOLED();
void setupWiFi();

// Task Scheduler objects
Task taskSendMessage(TASK_SECOND * schedulerDelay, TASK_FOREVER, &sendMessage);
Task taskUploadMessage(TASK_SECOND * schedulerDelay, TASK_FOREVER, &uploadMessage); // Send data every 10 seconds
Task taskUpdateOLED(TASK_SECOND * (schedulerDelay / 2), TASK_FOREVER, &updateOLED);  // Update OLED every 5 seconds
Task taskUpdateLEDs(TASK_SECOND * (schedulerDelay / 5), TASK_FOREVER, &updateLEDs);  // Update LEDs every 2 seconds

// JSON handling
StaticJsonDocument<1024> jsonReadings;

// Map to store sensor data from other nodes
std::map<uint32_t, String[5]> data_map;  // Map of node IDs to sensor data arrays

// Function to convert sensor readings to JSON format
String readingsToJSON() {
    StaticJsonDocument<1024> doc;
    
    // Add this node's own data to the JSON
    doc["nodeId"] = mesh.getNodeId();
    JsonObject nodeData = doc.createNestedObject("data");
    for (int i = 0; i < 5; i++) {
        nodeData[keys[i]] = datum[i];
    }
    
    // Add data from other nodes (from data_map)
    JsonObject otherNodes = doc.createNestedObject("otherNodes");
    for (auto const& entry : data_map) {
        JsonObject node = otherNodes.createNestedObject(String(entry.first));
        for (int i = 0; i < 5; i++) {
            node[keys[i]] = entry.second[i];  // Add the data for each node
        }
    }
    
    String json;
    serializeJson(doc, json);
    Serial.println(json);
    return json;
}

// Function to update the OLED with the last 5 messages
void displayMessages() {
    g_OLED.clearBuffer();  // Clear the screen
    for (int i = 0; i < 5; i++) {
        g_OLED.setCursor(0, g_lineHeight * (i + 1));  // Display each message on a new line
        g_OLED.printf("%s: %s %s", keys[i].c_str(), datum[i].c_str(), suf[i].c_str());
    }
    g_OLED.sendBuffer();  // Send the updated buffer to the OLED
}

// Sends the sensor readings as a message to each node in the mesh network
void sendMessage() {
    Serial.print("1");
    String msg = readingsToJSON();

    // Get the list of connected nodes
    std::list<uint32_t> nodes = mesh.getNodeList();

    // Send the message to each node using sendSingle
    for (auto nodeId : nodes) {
        Serial.print("2");
        mesh.sendSingle(nodeId, msg);  // Send message to each connected node
        Serial.printf("Sent message to node: %u\n", nodeId);
    }
}

void uploadMessage() {
    setupWiFi();
    Serial.println("Setup Done");
    if(WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        // Send the first field to ThingSpeak
        String url = String(server) + "/update?api_key=" + thingSpeakAPI +
                     "&field1=" + String(datum[0]) +  // PM 1.0
                     "&field2=" + String(datum[1]) +  // PM 2.5
                     "&field3=" + String(datum[2]) +  // PM 10.0
                     "&field4=" + String(datum[3]) +  // Temperature
                     "&field5=" + String(datum[4]);   // Humidity

        http.begin(url);
        int httpCode = http.GET();  // Send the request

        if (httpCode > 0) {
            String payload = http.getString();  // Get the response payload
            Serial.println("HTTP Response code: " + String(httpCode));
            Serial.println("Payload: " + payload);
        } else {
            Serial.println("Error on HTTP request");
        }
        
        http.end();  // Close connection
    } else {
        Serial.println("WiFi not connected");
    }
}

// Function to update LED status
void updateLEDs() {
    // 5 leds total, one for each sensor data, which will be green below a certain threshhold, yellow above that, orange above that, red above that, and purple above that
}

// Function to update LED status
void updateOLED() {
    displayMessages();
    // Screen code 
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

    // Extract the node's sensor data and store it in the data_map
    JsonObject nodeData = jsonReadings["data"];
    for (int i = 0; i < 5; i++) {
        data_map[from][i] = nodeData[keys[i]].as<String>();  // Store the received data
    }

    // Print the received data
    Serial.print("From: ");
    Serial.println(from);
    for (int i = 0; i < 5; i++) {
        Serial.print(keys[i]);
        Serial.print(": ");
        Serial.print(data_map[from][i]);
        Serial.print(" ");
        Serial.println(suf[i]);
    }

    // Update the display with new readings from other nodes (if needed)
    displayMessages();
}

// Function to initialize Wi-Fi and connect to the WPA2 Enterprise network
void setupWiFi() {
    Serial.print("Connecting to Wi-Fi...");

    WiFi.disconnect(true);  // Ensure a clean start
    WiFi.mode(WIFI_STA);  // Set Wi-Fi to station mode

    // WPA2 Enterprise setup
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)identity, strlen(identity));
    esp_wifi_sta_wpa2_ent_set_username((uint8_t *)identity, strlen(identity));
    esp_wifi_sta_wpa2_ent_set_password((uint8_t *)password, strlen(password));
    esp_wifi_sta_wpa2_ent_enable();
    WiFi.begin(ssid);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWi-Fi connected!");
        wifiConnected = true;
    } else {
        Serial.println("\nFailed to connect to Wi-Fi");
        wifiConnected = false;
    }
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
    
    // Initialize mesh network
    mesh.setDebugMsgTypes(ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE);  // all types on
    mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
    
    // Set mesh event callbacks
    mesh.onReceive(&receivedCallback);
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onChangedConnections(&changedConnectionCallback);
    mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);
    
    // Connect to eduroam with WPA2-Enterprise
    setupWiFi();

    // Schedule the task to send messages every 10 seconds
    userScheduler.addTask(taskSendMessage);
    // userScheduler.addTask(taskUploadMessage);
    taskSendMessage.enable();
    // delay((TASK_SECOND * schedulerDelay) / 2);
    // taskUploadMessage.enable();
}

void loop() {
    // Mesh network task
    mesh.update();
}
