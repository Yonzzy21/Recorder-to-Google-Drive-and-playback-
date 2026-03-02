#define VERSION "\nak esp32 INMP441"
#include "upload_logic.h"
#include <SD.h>
#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid = "DukeVisitor";
#define LED_READY 21   // Green LED
#define LED_RECORD 4   // Red LED
#define SD_CS_PIN 5 
#define LED 2
#define RECORD_BTN 26

bool I2S_Record_Init();
bool Record_Start(String filename);
bool Record_Available(String filename, float* audiolength_sec);

bool isRecording = false;
bool lastButtonState = HIGH;
String currentAudioFile = "";

String getNextAudioFile() {
  int fileNum = 1;
  String fileName;
  do {
    fileName = "/audio" + String(fileNum) + ".wav";
    fileNum++;
  } while (SD.exists(fileName));
  return fileName;
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(100);
  Serial.println("[DEBUG] Starting setup...");

  pinMode(LED_READY, OUTPUT);
  pinMode(LED_RECORD, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(RECORD_BTN, INPUT_PULLUP);

  digitalWrite(LED_READY, LOW);
  digitalWrite(LED_RECORD, LOW);

  WiFi.begin(ssid);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi!");

  initUploadTask();

  Serial.println("[DEBUG] Pins initialized: LED (2), Button (26)");
  Serial.println(VERSION);

  Serial.println("[DEBUG] Initializing SD card...");
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("[ERROR] SD Card initialization failed!");
    return;
  }

  Serial.println("[DEBUG] Initializing I2S recording...");
  if (!I2S_Record_Init()) {
    Serial.println("[ERROR] I2S recording initialization failed!");
    return;
  }

  Serial.println("[DEBUG] Setup complete. Waiting for button press to start/stop recording...");
  digitalWrite(LED_READY, HIGH);  // System ready
}

void loop() {
  bool currentButtonState = digitalRead(RECORD_BTN);

  if (currentButtonState == LOW && lastButtonState == HIGH) {
    Serial.println("[DEBUG] Button pressed!");

    // ✅ Immediate feedback: blink green LED before attempting anything heavy
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_READY, HIGH);
      delay(150);
      digitalWrite(LED_READY, LOW);
      delay(150);
    }

    if (!isRecording) {
      currentAudioFile = getNextAudioFile();
      isRecording = true;

      Serial.println("[DEBUG] Attempting to start recording...");

      // Retry starting the recording
      bool started = false;
      for (int i = 0; i < 10 && !started; i++) {
        started = Record_Start(currentAudioFile);
        delay(100);
      }

      if (started) {
        digitalWrite(LED_RECORD, HIGH);
        Serial.println("[DEBUG] Recording started successfully");
      } else {
        isRecording = false;
        digitalWrite(LED, LOW);
        digitalWrite(LED_READY, HIGH);  // return to ready
        Serial.println("[ERROR] Failed to start recording");
      }

    } else {
      Serial.println("[DEBUG] Stopping recording...");
      isRecording = false;
      digitalWrite(LED, LOW);
      digitalWrite(LED_RECORD, LOW);
      digitalWrite(LED_READY, HIGH);
      delay(300);

      float recorded_seconds;
      if (Record_Available(currentAudioFile, &recorded_seconds)) {
        Serial.printf("[DEBUG] Recording saved to %s. Duration: %.2f seconds\n", currentAudioFile.c_str(), recorded_seconds);
        if (recorded_seconds > 0.4) {
          queueFileForUpload(currentAudioFile);
        } else {
          Serial.println("[DEBUG] Recording too short (<=0.4s), ignoring");
        }
      } else {
        Serial.println("[ERROR] Recording not available or failed to save!");
      }

      currentAudioFile = "";
    }

    delay(100); // debounce
  }

  if (isRecording) {
    if (!Record_Start(currentAudioFile)) {
      Serial.println("[ERROR] Failed to append I2S data!");
      isRecording = false;
      digitalWrite(LED, LOW);
      digitalWrite(LED_RECORD, LOW);
      digitalWrite(LED_READY, HIGH);
    }
  }

  lastButtonState = currentButtonState;
}
