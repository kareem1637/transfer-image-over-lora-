#include "esp_camera.h"
#include <HardwareSerial.h>

// LoRa Configuration
String lora_band = "865000000"; // Enter band as per your country
String lora_networkid = "5";    // Enter LoRa Network ID
String lora_address = "1";      // Enter LoRa address
String lora_RX_address = "2";   // Enter LoRa RX address
HardwareSerial LoRaSerial(1);
HardwareSerial esp32Serial(2);
// Camera model
#define CAMERA_MODEL_AI_THINKER

#if defined(CAMERA_MODEL_AI_THINKER)
  #define PWDN_GPIO_NUM     32
  #define RESET_GPIO_NUM    -1
  #define XCLK_GPIO_NUM      0
  #define SIOD_GPIO_NUM     26
  #define SIOC_GPIO_NUM     27
  #define Y9_GPIO_NUM       35
  #define Y8_GPIO_NUM       34
  #define Y7_GPIO_NUM       39
  #define Y6_GPIO_NUM       36
  #define Y5_GPIO_NUM       21
  #define Y4_GPIO_NUM       19
  #define Y3_GPIO_NUM       18
  #define Y2_GPIO_NUM        5
  #define VSYNC_GPIO_NUM    25
  #define HREF_GPIO_NUM     23
  #define PCLK_GPIO_NUM     22
#else
  #error "Camera model not selected"
#endif

// Function to initialize the camera
bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = CAMERA_GRAB_LATEST;
  
    if(psramFound()){
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 10;
        config.fb_count = 1;
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }

    // Initialize camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x", err);
        return false;
    }
    return true;
}

void setup() {
    Serial.begin(115200);
    LoRaSerial.begin(115200, SERIAL_8N1, 3, 1); // UART1: TX=1, RX=3
    esp32Serial.begin(115200, SERIAL_8N1, 16, 17); // UART1: TX=1, RX=16
    if (!initCamera()) {
        Serial.println("Camera initialization failed");
        while (1);
    }

    // Initialize the LoRa module
    delay(1500);
    LoRaSerial.println("AT+BAND=" + lora_band);
    delay(500);
    LoRaSerial.println("AT+ADDRESS=" + lora_address);
    delay(500);
    LoRaSerial.println("AT+NETWORKID=" + lora_networkid);

    Serial.println("Setup complete.");
}

void loop() {
    // Capture an image
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return;
    }

    // Convert the captured image to hex
    String hexData = binaryToHex(fb->buf, fb->len);
    esp_camera_fb_return(fb);

    // Transmit the image via LoRa
    transmitImage((uint8_t *)hexData.c_str(), hexData.length());

    // Read incoming data from Serial
    if (esp32Serial.available() > 0) {
        String incomingData = Serial.readString();
        Serial.print("Received data: ");
        Serial.println(incomingData);

        // Send data via LoRa
        String command = "AT+SEND="  + String(lora_RX_address) + String(",") + String(incomingData.length()) + String(",") + String(incomingData);
        LoRaSerial.println(command);
        delay(1500);
       
    }

    Serial.println("Going to sleep for 12 hours.");
    esp_sleep_enable_timer_wakeup(15000000ULL); // this will set the system to sleep for 15 sec change that to 43200000000ULL to set it sleep for 12 hours in microseconds  
    esp_deep_sleep_start();
}

String binaryToHex(const uint8_t *data, size_t length) {
    String hexString = "";
    for (size_t i = 0; i < length; i++) {
        if (data[i] < 16) hexString += "0";
        hexString += String(data[i], HEX);
    }
    return hexString;
}

void transmitImage(const uint8_t *image, size_t length) {
    const int packetSize = 200;  // Define packet size (less than 240 to allow overhead)
    int totalPackets = (length + packetSize - 1) / packetSize; // Calculate total number of packets

    for (int i = 0; i < totalPackets; i++) {
        size_t chunkSize = min(packetSize, static_cast<int>(length - i * packetSize)); // Cast length to int
        bool ackReceived = false;

        while (!ackReceived) {
            sendLoRaPacket(image + i * packetSize, chunkSize, i, totalPackets);
            ackReceived = waitForAck(i);
        }
    }
}

void sendLoRaPacket(const uint8_t *data, size_t length, int sequenceNumber, int totalPackets) {
    // Convert the data to an ASCII string
    String command = "AT+SEND=" + String(lora_RX_address) + "," + String(length * 2 + 20) + ","; // 20 extra bytes for sequence and total packets
    LoRaSerial.print(command);

    // Add sequence number and total packets to the command
    LoRaSerial.printf("%04d%04d", sequenceNumber, totalPackets);

    for (size_t i = 0; i < length; i++) {
        if (data[i] < 16) LoRaSerial.print("0"); // Add leading zero for single hex digits
        LoRaSerial.printf("%02X", data[i]);
    }
    LoRaSerial.println();
}
bool waitForAck(int sequenceNumber) {
    unsigned long startTime = millis();
    while (millis() - startTime < 5000) { // Wait for 5 seconds for an ACK
        if (LoRaSerial.available()) {
            String response = LoRaSerial.readStringUntil('\n'); // Read until newline
            int ackIndex = response.indexOf("ACK");
            if (ackIndex != -1) {
                // Extract ACK number from response
                int start = response.indexOf(':', ackIndex) + 1;
                int end = response.indexOf(',', start);
                if (start != -1 && end != -1) {
                    String ackNumberStr = response.substring(start, end);
                    ackNumberStr.trim(); // Trim leading and trailing whitespace
                    int ackNumber = ackNumberStr.toInt();
                    if (ackNumber == sequenceNumber) {
                        while (LoRaSerial.available()) LoRaSerial.read(); // Clear remaining serial data
                        return true; // ACK received
                    }
                }
            }
        }
    }
    return false; // Timeout
}





