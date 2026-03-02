#  Audience Recorder into Gallery Space
**An interactive sound installation presented at Duke University, August 2025.**

This project is contains the technical setup for a collaborative piece I created during an artist residency at Duke University, together with Malak Mansour.
The core of the piece is an interactive two-location process: an **Input Station** captures audience voices, a **Cloud Bridge** archives them, and a **Gallery Player** 
embedded into Malak's sculpture autonomously syncs and plays the evolving soundscape.

---

##  System Architecture
The installation is divided into three modules to ensure high-fidelity recording and continuous playback, based on connection to WiFi.
It enables the audience to record their dreams by pressing "record" and again to stop, then uploads their recordings to a google drive.
A second ESP32 is located in the gallery, fetches the files and plays them in a loop.
<img src="https://github.com/user-attachments/assets/6dfa10a8-db00-42b9-8481-36bcce689a8f" width="500" alt="System Overview">

Green light when you can press the button to record, red light lights up when recording.

### 1. The Recorder (`/firmware`)
An ESP32-based station that handles audio capture.
* **Core Logic:** Uses FreeRTOS to run a background `uploadTask` on **Core 0** while the I2S recording stays prioritized on **Core 1**.
* **Conflict Prevention:** Uses an `isRecording` flag to prevent the SD card from being accessed by the uploader and recorder simultaneously.
* Based on INMP441_ESP32_RECORDER script by Antarip Kar. Added a background logic for uploading the recordings from SD to Google Drive.

[![Watch the video](https://img.youtube.com/vi/KHkFXbQPWxk/0.jpg)](https://www.youtube.com/watch?v=KHkFXbQPWxk)
*Click the image above to watch the recording and playback loop.*
**Outside view of the recorder space**

### 2. The Bridge (`/cloud_upload`)
A Google Apps Script acting as a lightweight API to bypass complex OAuth2 requirements. 
* **doPost:** Receives binary data and saves it to Google Drive.
* **doGet:** Checks if a file already exists to save bandwidth.
* **Playlist Generator:** Automatically creates a `playlist.txt` manifest for the player.

### 3. The Player (`/playing_from_drive`)
A second ESP32 that drives the gallery speakers using the `ESP8266Audio` library.
* **Smart Sync:** Periodically fetches the cloud playlist, compares it to local SD files, and downloads only what is missing.
* **Playback:** Outputs high-quality audio via I2S to a DAC or Amplifier.
<img src="https://github.com/user-attachments/assets/e4317329-0bc6-4aa7-90e7-7777363c17d5" width="350" alt="Gallery View">
---

##  Hardware Requirements & Pinout

| Component | Purpose | Pins (ESP32) |
| :--- | :--- | :--- |
| **INMP441** | I2S Microphone | WS(22), SCK(33), SD(35) |
| **SD Card Module** | Storage Buffer | CS(5), SCK(18), MISO(19), MOSI(23) |
| **I2S DAC (PCM5102)**| Audio Output | BCK(26), WS(25), DIN(22) |
| **Status LEDs** | UX Feedback | Ready(21), Record(4) |
| **Arcade Button** | Trigger | Pin 26 (Internal Pullup) |

---

## Quick Start Setup

### 1. Cloud Configuration
1. Create a folder in Google Drive named `ESP32_AudioUploads`.
2. Open [script.google.com](https://script.google.com) and paste the code from `/cloud_upload`.
3. **Deploy as Web App**: 
   - Execute as: **Me**
   - Who has access: **Anyone**
4. Copy the **Web App URL** and the **Playlist File ID**.

### 2. Recorder Setup
1. Open `firmware/mic_test.ino`.
2. Update `ssid` and `password`.
3. In `upload_logic.h`, paste your **Web App URL** into the `uploadUrl` variable.
4. Upload to the "Recorder" ESP32.

### 3. Player Setup
1. Open `playing_from_drive/PlayingFromDrive.ino`.
2. Update `ssid` and `password`.
3. Paste your **Playlist File ID** into the `playlist_file_id` variable.
4. Upload to the "Player" ESP32.

---

##  Technical Notes
- **WAV Header:** The system manually generates a 44-byte RIFF header for 8kHz, 16-bit Mono audio.
- **Fail-safe:** If uploads fail, errors are logged to `failed_uploads.txt` on the SD card for post-exhibition debugging.


##  Photos and Work in Progress

<p float="left">
  <img src="https://github.com/user-attachments/assets/6c1c859f-74af-464d-a9dc-0e04e19c6513" width="280" />
  <img src="https://github.com/user-attachments/assets/f0633307-785c-404a-92a2-88b9c4a542ff" width="280" /> 
  <img src="https://github.com/user-attachments/assets/247b2ca1-c316-4afd-93e3-8d79b78526b4" width="280" />
</p>

