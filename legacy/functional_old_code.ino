#include <TinyGPS++.h>
#include <SoftwareSerial.h>
#include <HardwareSerial.h>

#define GPS_RX 14  // RX pin for GPS module
#define GPS_TX 15  // TX pin for GPS module
#define GSM_RX 16  // RX pin for GSM module
#define GSM_TX 17  // TX pin for GSM module
#define PulseSensor_PIN 36

SoftwareSerial gpsSerial(GPS_RX, GPS_TX);
SoftwareSerial gsmSerial(GSM_RX, GSM_TX);
TinyGPSPlus gps;

#define RST_BUTTON 4    // GPIO pin connected to the RST button on the ESP32
#define LED_PIN     2   // Built-in LED pin on many ESP32 boards

bool smsTriggered = false;

// Hardcoded phone number and message content
const char defaultRemoteNum[] = "+601129513169"; // Replace with your desired phone number
const char defaultMessage[] = "Hello, this is a test message.";

unsigned long previousMillisGetHB = 0;
unsigned long previousMillisResultHB = 0;

const long intervalGetHB = 5;
const long intervalResultHB = 1000;    // set time delay

int timer_Get_BPM = 0;

int PulseSensorSignal;
const int UpperThreshold = 3190;
const int LowerThreshold = 3100;

int cntHB = 0;
bool ThresholdStat = true;
int BPMval = 0;

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(9600);
  gsmSerial.begin(19200);


  // Set up built-in LED pin
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Initialize LED as off

  Serial.println("GPS, GSM, and Heart Rate Integration");
}

void loop() {
  // Read GPS data
  while (gpsSerial.available() > 0) {
    if (gps.encode(gpsSerial.read())) {
      if (gps.location.isValid()) {
        double latitude = gps.location.lat();
        double longitude = gps.location.lng();

        Serial.print("Latitude: ");
        Serial.println(latitude, 6);

        Serial.print("Longitude: ");
        Serial.println(longitude, 6);

        // Create a Google Maps link
        Serial.print("Google Maps link: ");
        Serial.print("https://www.google.com/maps?q=");
        Serial.print(latitude, 6);
        Serial.print(",");
        Serial.println(longitude, 6);

        // Add your GSM functionality here, e.g., send SMS
        if (!smsTriggered) {
          sendSMS(latitude, longitude);
          smsTriggered = true;
        }
      }
    }
  }

  // Heart Rate Monitoring
  GetHeartRate();

  // GSM tasks
  if (digitalRead(RST_BUTTON) == LOW) {
    if (!smsTriggered) {
      sendSMS(0.0, 0.0);  // You can customize the coordinates or ignore them
      smsTriggered = true;
    }
  } else {
    smsTriggered = false;
  }

  // Other tasks
  // ...
}

void sendSMS(double latitude, double longitude) {
  digitalWrite(LED_PIN, HIGH);

  // Set SMS mode to text
  sendCommand("AT+CMGF=1");
  delay(2000);

  // Compose and send SMS
  sendCommand("AT+CMGS=\"" + String(defaultRemoteNum) + "\"");
  delay(2000);

  // Send message content
  String message = "Latitude: " + String(latitude, 6) + "\nLongitude: " + String(longitude, 6) + "\nBPM: " + String(BPMval);
  sendCommand(message);
  delay(2000);

  // CTRL+Z to send SMS
  sendCommand(String((char)26));
  delay(2000);

  digitalWrite(LED_PIN, LOW);
  Serial.println("\nSMS Sent!\n");
}

void GetHeartRate() {
  auto currentMillisGetHB = millis();

  if (currentMillisGetHB - previousMillisGetHB >= intervalGetHB) {
    previousMillisGetHB = currentMillisGetHB;

    PulseSensorSignal = analogRead(PulseSensor_PIN);

    if (PulseSensorSignal > UpperThreshold && ThresholdStat) {
      ++cntHB;
      ThresholdStat = false;
    }

    if (PulseSensorSignal < LowerThreshold) {
      ThresholdStat = true;
    }
  }

  auto currentMillisResultHB = millis();

  if (currentMillisResultHB - previousMillisResultHB >= intervalResultHB) {
    previousMillisResultHB = currentMillisResultHB;

    ++timer_Get_BPM;

    if (timer_Get_BPM > 30) {
      timer_Get_BPM = 1;

      BPMval = cntHB;
      Serial.print("BPM: ");
      Serial.println(BPMval);

      // Add SMS sending condition here
      if (BPMval > 90) {
        if (!smsTriggered) {
          sendSMS(0.0, 0.0);  // You can customize the coordinates or ignore them
          smsTriggered = true;
        }
      }

      cntHB = 0;
    }
  }
}



void sendCommand(String cmd) {
  Serial.print("Sending command: ");
  Serial.println(cmd);
  Serial2.println(cmd);

  delay(2000);  // Increase delay to allow the module to process the command

  // Read and print the response
  Serial.println("Response:");
  while (Serial2.available()) {
    char c = Serial2.read();
    Serial.print(c);
  }
  Serial.println();
}