#include <SoftwareSerial.h>

// ------------------------
// Pin Configuration
// ------------------------
const int pirPin = 8;           // PIR motion sensor pin
const int buzzerPin = 12;       // Buzzer output pin
const int ledPin = 11;          // LED indicator pin
const int simResetPin = 49;     // SIM800 reset control pin

// Create a software serial connection to the SIM800 module
SoftwareSerial sim800(51, 53);  // RX = 51, TX = 53

// ------------------------
// Alert Message Details
// ------------------------
const char phoneNumber[] = "+254748601059";                // Destination phone number
const char alertMessage[] = "Motion detected at your premises.";  // SMS body

// ------------------------
// State Tracking Variables
// ------------------------
bool messageSent = false;
unsigned long lastHealthCheck = 0;
const unsigned long healthCheckInterval = 60000; // Check SIM800 every 60 seconds
unsigned long lastAlertTime = 0;
const unsigned long alertCooldown = 10000;       // 10 second cooldown between alerts

// ------------------------
// Setup Function
// ------------------------
void setup() {
  Serial.begin(9600);     // Start serial monitor
  sim800.begin(9600);     // Start SIM800 serial communication

  // Set pin modes
  pinMode(pirPin, INPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(simResetPin, OUTPUT);
  digitalWrite(simResetPin, HIGH); // Ensure SIM800 stays powered

  Serial.println("Initializing SIM800...");

  // Perform startup checks
  if (!checkModuleReady()) {
    Serial.println("SIM800L did not respond. Halting.");
    while (true) errorAlert();
  }

  if (!checkSIMReady()) {
    Serial.println("SIM not ready.");
    while (true) errorAlert();
  }

  if (!checkNetworkRegistered()) {
    Serial.println("Not registered to network.");
    while (true) errorAlert();
  }

  if (!checkSignalQuality()) {
    while (true) errorAlert();
  }
}

// ------------------------
// Main Loop Function
// ------------------------
void loop() {
  int motion = digitalRead(pirPin);

  // Motion detected and cooldown passed
  if (detectMotion() && (millis() - lastAlertTime > alertCooldown)) {
    Serial.println("Motion detected");

    digitalWrite(ledPin, HIGH);  // Turn on LED
    buzzAlert();                 // Sound buzzer alert

    // Send SMS
    if (sendSMS(phoneNumber, alertMessage)) {
      Serial.println("✅ SMS sent.");
      messageSent = true;
      lastAlertTime = millis(); // Start cooldown
    } else {
      Serial.println("❌ Failed to send SMS.");
    }
  } 
  // No motion
  else if (motion == LOW) {
    digitalWrite(ledPin, LOW);
    messageSent = false;
  }

  // Periodic health check of SIM800
  if (millis() - lastHealthCheck >= healthCheckInterval) {
    Serial.println("⏱ Performing SIM800 health check...");

    if (!checkAll()) {
      Serial.println("⚠ SIM800 check failed. Attempting recovery...");

      // Retry 3 times before reset
      for (int i = 0; i < 3; i++) {
        if (checkAll()) {
          Serial.println("✅ Recovery successful.");
          break;
        }
        Serial.println("Retrying...");
        delay(2000);
      }

      // Hard reset if recovery fails
      if (!checkAll()) {
        Serial.println("❌ Recovery failed. Resetting SIM800...");
        resetSIM800();
        delay(3000);

        if (!checkAll()) {
          Serial.println("❌ SIM800 failed after reset.");
          errorAlert();
        } else {
          Serial.println("✅ SIM800 recovered after reset.");
        }
      }
    }

    lastHealthCheck = millis(); // Reset timer
  }
}

// ------------------------
// Detect Motion with Debounce
// ------------------------
bool detectMotion() {
  static unsigned long lastMotionTime = 0;
  const unsigned long debounceDuration = 500; // 0.5 seconds

  if (digitalRead(pirPin) == HIGH) {
    if (millis() - lastMotionTime > debounceDuration) {
      lastMotionTime = millis();
      return true;
    }
  }
  return false;
}

// ------------------------
// Send SMS via SIM800
// ------------------------
bool sendSMS(const char* number, const char* message) {
  if (!sendATCommand("AT+CMGF=1", "OK", 2000)) return false;

  sim800.print("AT+CMGS=\"");
  sim800.print(number);
  sim800.println("\"");
  delay(1000);

  sim800.print(message);
  sim800.write(26); // ASCII code for Ctrl+Z to send SMS

  return waitResponse("OK", 7000);
}

// ------------------------
// SIM800 Check Utilities
// ------------------------
bool checkAll() {
  return checkModuleReady() && checkSIMReady() && checkNetworkRegistered();
}

bool checkModuleReady() {
  for (int i = 0; i < 3; i++) {
    if (sendATCommand("AT", "OK", 2000)) {
      Serial.println("SIM800 is responsive.");
      return true;
    }
    delay(1000);
  }
  return false;
}

bool checkSIMReady() {
  for (int i = 0; i < 3; i++) {
    sim800.println("AT+CPIN?");
    if (waitResponse("+CPIN: READY", 3000)) {
      Serial.println("SIM card is ready.");
      return true;
    }
    delay(1000);
  }
  return false;
}

bool checkNetworkRegistered() {
  for (int i = 0; i < 3; i++) {
    sim800.println("AT+CREG?");
    if (waitResponse("+CREG: 0,1", 3000) || waitResponse("+CREG: 0,5", 3000)) {
      Serial.println("Network registration OK.");
      return true;
    }
    delay(1000);
  }
  return false;
}

bool checkSignalQuality() {
  sim800.println("AT+CSQ");
  if (waitResponse("+CSQ:", 2000)) {
    Serial.println("Signal quality checked.");
    return true;
  } else {
    Serial.println("Warning: Unable to check signal quality.");
    return false;
  }
}

// ------------------------
// AT Command Utilities
// ------------------------
bool sendATCommand(const char* cmd, const char* expected, unsigned long timeout) {
  sim800.println(cmd);
  return waitResponse(expected, timeout);
}

bool waitResponse(const char* expected, unsigned long timeout) {
  unsigned long start = millis();
  String resp = "";

  while (millis() - start < timeout) {
    while (sim800.available()) {
      char c = sim800.read();
      resp += c;
    }
    if (resp.indexOf(expected) != -1) return true;
  }
  return false;
}

// ------------------------
// SIM800 Reset Function
// ------------------------
void resetSIM800() {
  digitalWrite(simResetPin, LOW);   // Pull reset pin LOW
  delay(200);
  digitalWrite(simResetPin, HIGH);  // Then HIGH to restart
  Serial.println("🔁 SIM800 reset triggered.");
}

// ------------------------
// Buzzer Alert Function
// ------------------------
void buzzAlert() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(buzzerPin, HIGH);
    delay(200);
    digitalWrite(buzzerPin, LOW);
    delay(200);
  }
}

// ------------------------
// Continuous Error Alert
// ------------------------
void errorAlert() {
  digitalWrite(buzzerPin, HIGH);
  delay(300);
  digitalWrite(buzzerPin, LOW);
  delay(300);
}
