  #include <SPI.h>
  #include <HardwareSerial.h>
  //define Serial connection to esp32-cam
  HardwareSerial camSerial(1);
  // Define ultrasonic sensor pins
  const int trigPinBin = 12;  // Ultrasonic sensor trigger pin for bin level
  const int echoPinBin = 13;  // Ultrasonic sensor echo pin for bin level

  const int trigPins[4] = {14, 2, 4, 21};  // Ultrasonic sensor trigger pins for surroundings
  const int echoPins[4] = {27, 15, 5, 19};  // Ultrasonic sensor echo pins for surroundings

  // Variables for sensor readings
  long durationBin;
  int distanceBin;
  long durations[4];
  int distances[4];

  void setup() {
    // Initialize Serial Monitor
    Serial.begin(115200);
    delay(3000); //adjust to sync with esp32-cam
    camSerial.begin(115200,SERIAL_8N1, 3, 1);
    
    
    // Initialize sensor pins
    pinMode(trigPinBin, OUTPUT);
    pinMode(echoPinBin, INPUT);

    for (int i = 0; i < 4; i++) {
      pinMode(trigPins[i], OUTPUT);
      pinMode(echoPins[i], INPUT);
    }
  }

  void loop() {
    // Read from bin level ultrasonic sensor
    digitalWrite(trigPinBin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPinBin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPinBin, LOW);
    durationBin = pulseIn(echoPinBin, HIGH);
    distanceBin = durationBin * 0.034 / 2;

    // Determine bin level
    String binLevel=getTankLevel(distanceBin);

    // Read from surrounding ultrasonic sensors
    for (int i = 0; i < 4; i++) {
      digitalWrite(trigPins[i], LOW);
      delayMicroseconds(2);
      digitalWrite(trigPins[i], HIGH);
      delayMicroseconds(10);
      digitalWrite(trigPins[i], LOW);
      durations[i] = pulseIn(echoPins[i], HIGH);
      distances[i] = durations[i] * 0.034 / 2;
    }

    // Check for trash around the bin
    bool trashDetected = false;
    for (int i = 0; i < 4; i++) {
      if (distances[i] < 10) { // Adjust threshold distance as needed
        trashDetected = true;
        break;
      }
    }
    camSerial.println(" - Bin Level: " + binLevel + "%, Trash Detected: "+ trashDetected ? "Yes" : "No" );


    Serial.println("Going to sleep for 12 hours.");
    esp_sleep_enable_timer_wakeup(15000000ULL); // this will set the system to sleep for 15 sec change that to 43200000000ULL to set it sleep for 12 hours in microseconds  
    esp_deep_sleep_start();
  }
String getTankLevel(unsigned int distance) {
  if (distance == 0) return "Error";
  if (distance <= 20) return "100%";
  if (distance <= 40) return "75%";
  if (distance <= 60) return "50%";
  if (distance <= 80) return "25%";
  return "0%";
}