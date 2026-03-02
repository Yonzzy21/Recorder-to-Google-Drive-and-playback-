#ifndef UPLOAD_LOGIC_H
#define UPLOAD_LOGIC_H

#include <WiFi.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SD.h>

#define MAX_QUEUE_SIZE 10
String uploadQueue[MAX_QUEUE_SIZE];
int queueStart = 0;
int queueEnd = 0;

// Add recording state flag to prevent SD conflicts
extern bool isRecording;  // This should be declared in your main file

const char* uploadUrl = "https://script.google.com/macros/s/AKfycbyZhb5y4ph_BxGbJkMGoZ4zVxCEKG1naSchuiLCJxhoPtA4OZ1SPrMZNvdT3QwIJYdX/exec";

#define MAX_UPLOAD_RETRIES 3
#define UPLOAD_RETRY_DELAY_MS 2000  // 2 seconds delay between retries

// === Queue Management ===
bool isQueueFull() {
  return ((queueEnd + 1) % MAX_QUEUE_SIZE) == queueStart;
}

bool isQueueEmpty() {
  return queueStart == queueEnd;
}

void queueFileForUpload(const String& file) {
  if (isQueueFull()) {
    Serial.println("[UPLOAD QUEUE] Full! File not queued: " + file);
    return;
  }
  uploadQueue[queueEnd] = file;
  queueEnd = (queueEnd + 1) % MAX_QUEUE_SIZE;
  Serial.println("[UPLOAD QUEUE] Queued file: " + file);
}

bool dequeueFile(String& file) {
  if (isQueueEmpty()) return false;
  file = uploadQueue[queueStart];
  queueStart = (queueStart + 1) % MAX_QUEUE_SIZE;
  return true;
}

// === File Existence Check ===
bool checkFileExists(String filename) {
  // Don't check during recording to avoid SD conflicts
  if (isRecording) return false;
  
  HTTPClient http;
  String url = String(uploadUrl) + "?filename=" + filename;
  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String response = http.getString();
    http.end();
    return (response == "EXISTS");
  }
  http.end();
  return false;
}

// === Upload Function with Retry ===
void uploadSingleFile(const String& path) {
  // Wait if recording is active to avoid SD card conflicts
  if (isRecording) {
    Serial.println("[UPLOAD] Waiting - recording in progress...");
    return;  // Skip this upload attempt
  }
  
  File file = SD.open(path);
  if (!file) {
    Serial.println("[UPLOAD] Failed to open file: " + path);
    return;
  }

  String filename = path;
  filename.remove(0, 1); // remove leading '/'

  if (checkFileExists(filename)) {
    Serial.println("[UPLOAD] File exists on server, skipping: " + filename);
    file.close();
    return;
  }

  String currentUrl = String(uploadUrl) + "?filename=" + filename;

  int attempt = 0;
  int httpCode = -1;
  String response = "";
  bool success = false;

  while (attempt < MAX_UPLOAD_RETRIES && !success && !isRecording) {
    file.seek(0); // rewind file to start for retry
    Serial.printf("[UPLOAD] Attempt %d uploading %s (size %d bytes) to %s\n",
                  attempt + 1, filename.c_str(), file.size(), currentUrl.c_str());

    HTTPClient http;
    http.begin(currentUrl);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("Content-Type", "audio/wav");
    http.setTimeout(15000);  // 15 seconds timeout

    httpCode = http.sendRequest("POST", &file, file.size());
    response = http.getString();
    http.end();

    if (httpCode == 200 || httpCode == 302) {
      Serial.printf("[UPLOAD] Success: %s -> HTTP %d, Response: %s\n",
                    filename.c_str(), httpCode, response.c_str());
      success = true;
    } else {
      Serial.printf("[UPLOAD] Failed attempt %d: HTTP %d, Response: %s\n",
                    attempt + 1, httpCode, response.c_str());
      if (!isRecording) {  // Only delay if not recording
        delay(UPLOAD_RETRY_DELAY_MS);
      }
      attempt++;
    }
  }

  file.close();

  if (success) {
    Serial.println("[UPLOAD] Upload completed successfully, keeping local file: " + path);
  } else {
    Serial.println("[UPLOAD] Upload failed after max retries! Logging to /failed_uploads.txt");
    if (!isRecording) {  // Only write log if not recording
      File logFile = SD.open("/failed_uploads.txt", FILE_APPEND);
      if (logFile) {
        logFile.printf("Failed to upload: %s, last HTTP code: %d\n", filename.c_str(), httpCode);
        logFile.close();
      } else {
        Serial.println("[UPLOAD] Failed to open log file for writing.");
      }
    }
  }
}

void uploadTask(void* param) {
  while (true) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(10000 / portTICK_PERIOD_MS);  // wait longer if no WiFi
      continue;
    }

    // Only process uploads when not recording
    if (!isRecording && !isQueueEmpty()) {
      String nextFile;
      if (dequeueFile(nextFile)) {
        Serial.println("[UPLOAD TASK] Dequeued file: " + nextFile);
        uploadSingleFile(nextFile);
      }
    }

    // Shorter delay when not recording, longer when recording
    int delayMs = isRecording ? 500 : 5000;  // 0.5s when recording, 5s when not
    vTaskDelay(delayMs / portTICK_PERIOD_MS);
  }
}

void initUploadTask() {
  xTaskCreatePinnedToCore(
    uploadTask,
    "Uploader",
    8192,
    NULL,
    1,
    NULL,
    0  // Run on core 0 (different from main loop on core 1)
  );
}

#endif