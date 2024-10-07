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
String information = "0 0 0 0 1";   // String form of the data
String messages[5] = {"", "", "", "", ""};  // Buffer to store the last 5 messages
String keys[5] = {"pm1.0", "pm2.5", "pm10.0", "temp", "humidity"};  // Keys for the data_map (these match the data positions in datum)
String datum[5] = {"0", "0", "0", "0", "1"};  // pm1.0, pm2.5, pm10.0, temp, humidity
std::map<String, String> data_map = {
    { "pm1.0", "0" },
    { "pm2.5", "0" },
    { "pm10.0", "0" },
    { "temp", "0" },
    { "humidity", "1" }
};

//String to send to other nodes with sensor readings
String readings;

int last = 0;

// PMS7003 Serial Communication
HardwareSerial pmsSerial(2);  // Use Serial2 for PMS7003 (TX=17, RX=16)
// PMS7003 Constants
#define FRAME_LENGTH 32  // PMS7003 sends 32-byte data frame

void readPMS7003Data() {
    if (pmsSerial.available() >= FRAME_LENGTH) {
        uint8_t buffer[FRAME_LENGTH];
        pmsSerial.readBytes(buffer, FRAME_LENGTH);  // Read 32 bytes from PMS7003
        
        // Validate the data frame
        if (buffer[0] == 0x42 && buffer[1] == 0x4d) {  // Frame header "BM"
            float pm1_0 = (buffer[10] << 8) | buffer[11];
            float pm2_5 = (buffer[12] << 8) | buffer[13];
            float pm10_0 = (buffer[14] << 8) | buffer[15];
            float temp = (buffer[16] << 8) | buffer[17];
            float humidity = (buffer[18] << 8) | buffer[19];
            
            // Update the `datum` array with sensor values (convert to String)
            datum[0] = String(pm1_0);
            datum[1] = String(pm2_5);
            datum[2] = String(pm10_0);
            datum[3] = String(temp);
            datum[4] = String(humidity);
        }
    }
}

void recieveDatum() {
    // declaring temp string to store the curr "word" up to del
    String temp = "";
    String del = " ";
    int index = 0;
    for (int i = 0; i < information.length(); i++) {
        // If the current char is not del, append it to the current "word",
        // otherwise, you have completed the word, print it, and start a new word.
        if (information[i] != del[0]) {
            temp += information[i];
        } else {
            datum[index] = temp;
            index += 1;
            temp = "";
        }
    }

    for (int i = 0; i < 5; i++) {
        String word = datum[i];
        data_map[keys[i]] = word;
    }

    for (int i = 0; i < 5; i++) {
        String word = data_map[keys[i]];
        datum[i] = word;
    }

}

void sendDatum() {
    // declaring temp string to store the curr "word" up to del
    String full = "";
    information = full;
    String temp = " ";
    for (int i = 0; i < 5; i++) {
        full = full + datum[i];
        full = full + temp;
    }
    information = full;
}

// Function to update the OLED with the last 4 messages
void displayMessages() {
    g_OLED.clearBuffer();  // Clear the screen
    for (int i = 0; i < split; i++) {
        g_OLED.setCursor(0, g_lineHeight * (i + 1));  // Display each message on a new line
        g_OLED.printf("%s", messages[i].c_str());
    }
    for (int i = split; i < (split + split2); i++) {
        g_OLED.setCursor(0, g_lineHeight * (i + 1));  // Display each message on a new line
        g_OLED.println("Info: " + information);
    }
    for (int i = (split + split2); i < 5; i++) {
        g_OLED.setCursor(0, g_lineHeight * (i + 1));  // Display each message on a new line
        g_OLED.printf("%s: %s", String(keys[i - (split + split2)]), String(data_map[keys[i - (split + split2)]]));
    }
    g_OLED.sendBuffer();  // Send the updated buffer to the OLED
}


// Function to handle incoming mesh messages
void receivedCallback(uint32_t from, String &msg) {
    // Ignore messages from this node itself
    // if (from == mesh.getNodeId()) {
    //     return;  // Ignore message
    // }
    
    Serial.printf("Message received from %u: %s\n", from, msg.c_str());

    if (action == actions) {
        action = 0;
    
    }
    
    if (action == 0) {   // For first message
        // Shift the existing messages down and add the new message at the top
        for (int i = 4; i > 0; i--) {
        messages[i] = messages[i - 1];
        }
        messages[0] = msg;  // Store the new message at the top

        action += 1;
    
    } else if (action == 1) {   // For second message
        int data = msg.toInt();

        for (int i = 4; i > 0; i--) {
            if (datum[i - 1].toInt() >= datum[4].toInt()) {
                datum[i] = String(datum[i].toInt() + 1);
                datum[i - 1] = String(datum[i - 1].toInt() - datum[i - 1].toInt());
            }
        }
        datum[0] = String(datum[0].toInt() + data);


        action += 1;


    } else if (action == 2) {   // For third message
        information = msg;
        recieveDatum();
        
        action += 1;
    }

    // Used to automatically respond to incoming message
    // delay(5);
    // userScheduler.startNow();
    // Serial.printf("Sent message:\n");
    

    displayMessages();  // Update the OLED display with the new messages
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


// Periodic task to send a message
Task sendMessageTask(TASK_SECOND * 10, TASK_FOREVER, []() {
    String msg = "" + String(WiFi.macAddress());
    mesh.sendBroadcast(msg);
    Serial.println("Broadcasting message: " + msg);
    receivedCallback(mesh.getNodeId(), msg);
    delay(1);

    String data = String(datum[4]);
    mesh.sendBroadcast(data);
    Serial.println("Broadcasting Data: " + data);
    receivedCallback(mesh.getNodeId(), data);
    delay(1);

    sendDatum();
    String info = information;
    mesh.sendBroadcast(info);
    Serial.println("Broadcasting Info: " + info);
    receivedCallback(mesh.getNodeId(), info);

    // Simulate receiving your own message
});

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

    //mesh.setDebugMsgTypes( ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE ); // all types on
    mesh.setDebugMsgTypes( ERROR | STARTUP );  // set before init() so that you can see startup messages

    // Initialize painlessMesh
    mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);

    // Assign all the callback functions to their corresponding events.
    mesh.onReceive(&receivedCallback);  // Set the callback for receiving messages
    mesh.onNewConnection(&newConnectionCallback);
    mesh.onChangedConnections(&changedConnectionCallback);
    mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

    // Add the task to send messages periodically
    userScheduler.addTask(sendMessageTask);
    sendMessageTask.enable();
    displayMessages();
}

void loop() {
    // Keep the mesh network alive
    mesh.update();
}
