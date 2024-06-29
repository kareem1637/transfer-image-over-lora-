#include <HardwareSerial.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESP_Mail_Client.h>
#include <HTTPClient.h>
#include <UrlEncode.h>

// Replace with your network credentials
const char* ssid = "SSID";
const char* password = "pass";

//WHATSAPP credentials
const char* apiKey = "YourAPIKey"; //replace with your whtassapp api key
const char* phoneNumber1 = "PhoneNumber1"; // replace with the number you want to send the message to   
// Add more phone numbers if needed

// SMTP server settings
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465

// Sender credentials
#define AUTHOR_EMAIL "AUTHOR_EMAIL"
#define AUTHOR_PASSWORD "gmail app password"

// Recipient email
#define RECIPIENT_EMAIL "RECIPIENT_EMAIL"

// Email subject
#define EMAIL_SUBJECT "Image from ESP32"

// Email file path
#define FILE_PHOTO_PATH "/captured_image.jpg"

// LoRa RYLR998 configuration
String lora_band = "865000000"; // Enter band as per your country
String lora_networkid = "5";    // Enter LoRa Network ID
String lora_address = "2";      // Enter LoRa address
String lora_RX_address = "1";   // Enter LoRa RX address
HardwareSerial LoRaSerial(2);
const int packetSize = 220; // Define packet size

// Buffer for storing image data
uint8_t *imageBuffer = nullptr;
size_t imageBufferLength = 0;
int totalPacketsExpected = 0;
int totalPacketsReceived = 0;

SMTPSession smtp;

void setup() {
    Serial.begin(115200);
    LoRaSerial.begin(115200, SERIAL_8N1, 16, 17);

    // Initialize WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");

    // Initialize LittleFS
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount LittleFS");
        return;
    }

    // Initialize the LoRa module
    delay(500);
    LoRaSerial.println("AT+BAND=" + lora_band);
    delay(500);
    LoRaSerial.println("AT+NETWORKID=" + lora_networkid);
    delay(500);
    LoRaSerial.println("AT+ADDRESS=" + lora_address);
}

void loop() {
    if (LoRaSerial.available()) {
        String command = LoRaSerial.readStringUntil('\n');
        if (command.startsWith("+RCV=")) {
            // Extract the length and data from the command
            int firstComma = command.indexOf(',');
            int secondComma = command.indexOf(',', firstComma + 1);
            int thirdComma = command.indexOf(',', secondComma + 1);
            int fourthComma= command.indexOf(',', thirdComma + 1);
            String lengthStr = command.substring(firstComma + 1, secondComma);
            int length = lengthStr.toInt();
            if(length<50){
              String LoRaData = command.substring(secondComma + 1,thirdComma);
              sendWhatsAppMessage(phoneNumber1, LoRaData);
            }
            else{
            String dataStr = command.substring(secondComma + 1,thirdComma);
            handleReceivedData(dataStr.c_str(), length);}
        }
    }
    Serial.println("Going to sleep for 12 hours.");
    esp_sleep_enable_timer_wakeup(15000000ULL); // this will set the system to sleep for 15 sec change that to 43200000000ULL to set it sleep for 12 hours in microseconds  
    esp_deep_sleep_start();}

void handleReceivedData(const char *data, int length) {
    // Extract sequence number and total packets from the data
    int sequenceNumber = atoi(String(data).substring(0, 4).c_str());
    totalPacketsExpected = atoi(String(data).substring(4, 8).c_str());

    // Allocate buffer for the image if not already done
    if (imageBufferLength == 0) {
        imageBufferLength = totalPacketsExpected * packetSize;
        imageBuffer = (uint8_t *)malloc(imageBufferLength);
    }

    // Copy the received data into the buffer at the correct position
    for (int i = 0; i < length - 8; i += 2) {
        uint8_t byte = strtol(String(data).substring(8 + i, 8 + i + 2).c_str(), NULL, 16);
        imageBuffer[sequenceNumber * packetSize + i / 2] = byte;
    }

    totalPacketsReceived++;
    String ackCommand = "ACK:" + String(sequenceNumber);
    // Send acknowledgment back to sender
    LoRaSerial.println("AT+SEND=" + lora_RX_address + ","+ackCommand.length()+","+ ackCommand);

    // Check if all packets have been received
    if (totalPacketsReceived == totalPacketsExpected) {
        // Convert hex data to JPEG image
        String hexData((char *)imageBuffer, imageBufferLength);
        if (hexToJPEG(hexData, FILE_PHOTO_PATH)) {
            // Send the JPEG image via email
            sendEmail();
        } else {
            Serial.println("Failed to convert hex to JPEG");
        }

        // Free the buffer and reset counters
        free(imageBuffer);
        imageBuffer = nullptr;
        imageBufferLength = 0;
        totalPacketsExpected = 0;
        totalPacketsReceived = 0;
    }
}

bool hexToJPEG(String hexData, const char* outputFilePath) {
    size_t len = hexData.length();
    if (len % 2 != 0) {
        Serial.println("Odd-length hex string");
        return false;
    }

    File file = LittleFS.open(outputFilePath, "wb");
    if (!file) {
        Serial.println("Failed to create file");
        return false;
    }

    for (size_t i = 0; i < len; i += 2) {
        String byteString = hexData.substring(i, i + 2);
        char byte = (char) strtol(byteString.c_str(), nullptr, 16);
        file.write((uint8_t)byte);
    }

    file.close();
    return true;
}

void sendEmail() {
    smtp.debug(1);
    smtp.callback(smtpCallback);

    SMTP_Message message;
    message.sender.name = "ESP32";
    message.sender.email = AUTHOR_EMAIL;
    message.subject = EMAIL_SUBJECT;
    message.addRecipient("Recipient", RECIPIENT_EMAIL);

    String htmlMsg = "<h2>Photo captured with ESP32 and attached in this email.</h2>";
    message.html.content = htmlMsg.c_str();
    message.html.charSet = "utf-8";
    message.html.transfer_encoding = Content_Transfer_Encoding::enc_qp;

    SMTP_Attachment att;
    att.descr.filename = "captured_image.jpg";
    att.descr.mime = "image/jpeg";
    att.file.path = FILE_PHOTO_PATH;
    att.file.storage_type = esp_mail_file_storage_type_flash;
    att.descr.transfer_encoding = Content_Transfer_Encoding::enc_base64;

    message.addAttachment(att);

    Session_Config config;
    config.server.host_name = SMTP_HOST;
    config.server.port = SMTP_PORT;
    config.login.email = AUTHOR_EMAIL;
    config.login.password = AUTHOR_PASSWORD;

    if (!smtp.connect(&config)) {
        Serial.println("Failed to connect to email server");
        return;
    }

    if (!MailClient.sendMail(&smtp, &message, true)) {
        Serial.println("Error sending Email, " + smtp.errorReason());
    }
}

void smtpCallback(SMTP_Status status) {
    Serial.println(status.info());
    if (status.success()) {
        Serial.println("Email sent successfully");
    }
}

void sendWhatsAppMessage(const char* phoneNumber, const String& message) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String encodedMessage = urlEncode(message); // URL encode the message
    String serverPath = "https://api.callmebot.com/whatsapp.php?phone=" + String(phoneNumber) + "&text=" + encodedMessage + "&apikey=" + apiKey;

    http.begin(serverPath.c_str());
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println(httpResponseCode);
      Serial.println(response);
    } else {
      Serial.print("Error on sending GET request: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}

