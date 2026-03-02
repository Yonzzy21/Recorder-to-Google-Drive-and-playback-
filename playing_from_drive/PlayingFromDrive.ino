#include <WiFi.h>
#include <HTTPClient.h>
#include <SD.h>
#include <vector>
#include <map>

#include "AudioFileSourceSD.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2S.h"

const char* ssid = "DukeVisitor";
const int SD_CS_PIN = 5;
const char* playlist_file_id = "1jaS_sA9Kaz3v1CZDO0wHFZzSlmt20qN2";

struct TrackInfo {
  String filename;
  String url;
};

std::vector<TrackInfo> trackList;
std::map<String, int> failedAttempts;
bool trackReady = false;
bool isDownloading = false;
int currentTrack = 0;
unsigned long lastPlaylistCheck = 0;
const unsigned long playlistCheckInterval = 600000; // 10 minutes

AudioGeneratorWAV *wav = nullptr;
AudioFileSourceSD *file = nullptr;
AudioOutputI2S *out = nullptr;

void networkDiagnostics() {
  Serial.println("=== NETWORK DIAGNOSTICS ===");
  Serial.println("WiFi Status: " + String(WiFi.status()));
  Serial.println("IP Address: " + WiFi.localIP().toString());
  Serial.println("Gateway: " + WiFi.gatewayIP().toString());
  Serial.println("DNS: " + WiFi.dnsIP().toString());
  Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
  
  // Test basic connectivity
  Serial.println("\n--- Testing DNS Resolution ---");
  HTTPClient http;
  http.setTimeout(5000);
  
  // Test Google connectivity
  http.begin("https://www.google.com");
  int code = http.GET();
  Serial.printf("Google.com response: %d\n", code);
  if (code > 0) {
    Serial.println("Google headers:");
    for (int i = 0; i < http.headers(); i++) {
      Serial.println("  " + http.headerName(i) + ": " + http.header(i));
    }
  }
  http.end();
  
  // Test Google Drive domain
  http.begin("https://drive.google.com");
  code = http.GET();
  Serial.printf("Drive.google.com response: %d\n", code);
  http.end();
  
  Serial.println("=== END DIAGNOSTICS ===\n");
}

String resolveGoogleDriveRedirect(const String& url) {
  Serial.println("Resolving redirect for: " + url);
  
  HTTPClient http;
  
  // Add headers for better compatibility
  http.addHeader("User-Agent", "Mozilla/5.0 (ESP32) AppleWebKit/537.36");
  http.setTimeout(10000); // 10 second timeout
  
  http.begin(url);
  int code = http.GET();
  String redirect = "";

  Serial.printf("Redirect response code: %d\n", code);

  if (code == 303 || code == 302 || code == 301) {
    redirect = http.getLocation();
    Serial.println("Redirect location: [" + redirect + "]");
    
    // Check if redirect location is empty - common Google Drive issue
    if (redirect == "" || redirect.length() < 10) {
      Serial.println(" Empty redirect location! Trying alternative method...");
      
      // Try alternative Google Drive URL format
      String fileId = "";
      int idStart = url.indexOf("id=");
      if (idStart >= 0) {
        int idEnd = url.indexOf("&", idStart);
        if (idEnd == -1) idEnd = url.length();
        fileId = url.substring(idStart + 3, idEnd);
        Serial.println("Extracted file ID: " + fileId);
        
        // Try the alternative format
        redirect = "https://drive.google.com/uc?export=download&confirm=1&id=" + fileId;
        Serial.println("Using alternative URL: " + redirect);
      }
    }
    
  } else if (code == 200) {
    redirect = url;
    Serial.println("Direct access (no redirect needed)");
  } else {
    Serial.printf("Failed to resolve redirect (code %d)\n", code);
    
    // Print all headers for debugging
    Serial.println("Response headers:");
    for (int i = 0; i < http.headers(); i++) {
      Serial.println("  " + http.headerName(i) + ": " + http.header(i));
    }
    
    // Print response for debugging
    String response = http.getString();
    Serial.println("Error response: " + response.substring(0, 300));
  }

  http.end();
  return redirect;
}

void fetchPlaylist() {
  trackList.clear();
  Serial.println("📂 Fetching playlist...");

  String playlistURL = "https://drive.google.com/uc?export=download&id=" + String(playlist_file_id);
  Serial.println("Initial URL: " + playlistURL);
  
  String finalURL = resolveGoogleDriveRedirect(playlistURL);
  if (finalURL == "") {
    Serial.println("❌ Could not resolve final URL");
    return;
  }
  Serial.println("Final URL: " + finalURL);

  HTTPClient http;
  
  // Add headers that some networks/firewalls expect
  http.addHeader("User-Agent", "Mozilla/5.0 (ESP32) AppleWebKit/537.36");
  http.addHeader("Accept", "text/plain,text/csv,*/*");
  
  // Set longer timeout for slower networks
  http.setTimeout(15000); // 15 seconds
  
  http.begin(finalURL);
  int code = http.GET();
  
  Serial.printf("HTTP Response Code: %d\n", code);
  
  if (code != 200) {
    Serial.printf("❌ Failed to download playlist.txt (code %d)\n", code);
    
    // Print response body for debugging
    String response = http.getString();
    Serial.println("Response body: " + response.substring(0, 200)); // First 200 chars
    
    http.end();
    return;
  }

  // Get content length to verify we're getting data
  int contentLength = http.getSize();
  Serial.printf("Content Length: %d bytes\n", contentLength);

  // Try using getString() instead of stream - more reliable with Google Drive
  String fullContent = http.getString();
  Serial.printf("Actually received: %d bytes\n", fullContent.length());
  Serial.println("First 500 chars of content: " + fullContent.substring(0, 500));
  
  if (fullContent.length() == 0) {
    Serial.println("❌ No content received despite positive content length!");
    http.end();
    return;
  }
  
  // Parse the content line by line
  int startIndex = 0;
  int linesProcessed = 0;
  
  while (startIndex < fullContent.length()) {
    int endIndex = fullContent.indexOf('\n', startIndex);
    if (endIndex == -1) {
      endIndex = fullContent.length(); // Last line
    }
    
    String line = fullContent.substring(startIndex, endIndex);
    line.trim(); // Remove \r, \n, and whitespace
    
    if (line.length() > 0) {
      Serial.println("Raw line: [" + line + "]");
    }
    
    if (line.length() > 10 && line.indexOf(',') > 0) {
      int commaIndex = line.indexOf(',');
      String name = line.substring(0, commaIndex);
      String url = line.substring(commaIndex + 1);
      
      name.trim();
      url.trim();
      
      Serial.println("Parsed - Name: [" + name + "] URL: [" + url + "]");
      
      trackList.push_back({name, url});
      Serial.println("✅ Found: " + name);
      linesProcessed++;
    } else if (line.length() > 0) {
      Serial.println("⚠️ Skipped line (too short or no comma): " + line);
    }
    
    startIndex = endIndex + 1;
  }

  http.end();
  
  Serial.printf("✅ Playlist loaded. %d tracks from %d lines processed.\n", trackList.size(), linesProcessed);
  Serial.println("First 500 chars of content: " + fullContent.substring(0, 500));
  
  if (trackList.size() == 0) {
    Serial.println("❌ No tracks found! Check playlist format and network connectivity.");
  }
}

bool downloadTrack(const String& url, const String& saveAs) {
  isDownloading = true;
  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.printf("Attempt %d: Downloading: %s\n", attempt, saveAs.c_str());
    String finalURL = resolveGoogleDriveRedirect(url);
    if (finalURL == "") continue;

    HTTPClient http;
    http.begin(finalURL);
    int code = http.GET();
    Serial.printf("HTTP GET: %d\n", code);
    if (code != 200) {
      http.end();
      continue;
    }

    File f = SD.open(saveAs, FILE_WRITE);
    if (!f) {
      Serial.println("Failed to open file on SD!");
      http.end();
      continue;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buff[256];
    size_t total = 0;
    unsigned long lastDataTime = millis();
    unsigned long downloadStart = millis();

    while (http.connected()) {
      if (millis() - downloadStart > 90000) {
        Serial.println("Download aborted: took too long.");
        break;
      }

      if (stream->available()) {
        size_t len = stream->read(buff, sizeof(buff));
        if (len > 0) {
          f.write(buff, len);
          total += len;
          lastDataTime = millis();
          Serial.print(".");
        }
      } else {
        if (millis() - lastDataTime > 20000) {
          Serial.println("Timeout: no data in 20 sec");
          break;
        }
        delay(1);
      }

      if (wav && wav->isRunning()) wav->loop();
    }

    f.close();
    http.end();
    Serial.printf("\nSaved %s (%u bytes)\n", saveAs.c_str(), (unsigned)total);
    if (total >= 1024) {
      isDownloading = false;
      trackReady = true;  // Force track to be restarted after download
      return true;
    }
    Serial.println("Warning: file too small or incomplete.");
  }

  isDownloading = false;
  return false;
}

void nextTrack() {
  currentTrack++;
  trackReady = true;
}

void startTrack(int index) {
  if (trackList.empty()) return;

  if (index >= trackList.size()) {
    Serial.println("Restarting playlist...");
    currentTrack = 0;
    startTrack(currentTrack);
    return;
  }

  String path = "/" + trackList[index].filename;
  File test = SD.open(path);
  if (!test || test.size() < 1024) {
    unsigned size = test ? test.size() : 0;
    if (test) test.close();
    Serial.printf("File %s too small (%u bytes), retrying...\n", path.c_str(), size);
    if (!downloadTrack(trackList[index].url, path)) {
      Serial.println("Failed to download. Skipping.");
      currentTrack++;
    }
    trackReady = true;
    return;
  }
  test.close();

  if (wav) { delete wav; wav = nullptr; }
  if (file) { delete file; file = nullptr; }
  if (out)  { delete out;  out = nullptr; }

  Serial.println("Now playing: " + path);
  file = new AudioFileSourceSD(path.c_str());
  out = new AudioOutputI2S(0, 0);
  out->SetGain(0.9);
  wav = new AudioGeneratorWAV();

  if (!wav->begin(file, out)) {
    Serial.println("Failed to begin WAV playback. Skipping.");
    currentTrack++;
    trackReady = true;
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi connected!");
  
  // Run network diagnostics after WiFi connection
  networkDiagnostics();

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD card init failed!");
    return;
  }
  Serial.println("SD card initialized.");

  fetchPlaylist();
  for (int i = 0; i < trackList.size(); ++i) {
    String path = "/" + trackList[i].filename;
    if (!SD.exists(path)) {
      downloadTrack(trackList[i].url, path);
    }
  }

  startTrack(0);
  lastPlaylistCheck = millis();
}

void loop() {
  if (wav && wav->isRunning()) {
    if (!wav->loop()) {
      Serial.println("Track finished.");
      nextTrack();
    }
  }

  if ((!wav || !wav->isRunning()) && trackReady && !isDownloading) {
    startTrack(currentTrack);
    trackReady = false;
  }

  if (!isDownloading && millis() - lastPlaylistCheck > playlistCheckInterval) {
    Serial.println("Checking for new playlist entries...");
    fetchPlaylist();
    for (int i = 0; i < trackList.size(); ++i) {
      String path = "/" + trackList[i].filename;
      if (!SD.exists(path)) {
        downloadTrack(trackList[i].url, path);
      }
    }
    lastPlaylistCheck = millis();
  }
}