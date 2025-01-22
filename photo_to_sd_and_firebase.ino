/*********
  Rui Santos
  Complete instructions at: https://RandomNerdTutorials.com/esp32-cam-save-picture-firebase-storage/
  
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

  Based on the example provided by the ESP Firebase Client Library
*********/

#include "Arduino.h"
#include "WiFi.h"
#include "esp_camera.h"
#include "soc/soc.h"           // Disable brownout problems
#include "soc/rtc_cntl_reg.h"  // Disable brownout problems
#include "driver/rtc_io.h"
#include <LittleFS.h>
// #include <FS.h>
#include "SD_MMC.h"            // SD Card ESP32
#include <Firebase_ESP_Client.h>
//Provide the token generation process info.
#include <addons/TokenHelper.h>
#include <EEPROM.h>            // Include EEPROM library
#include "time.h"
#include "esp_sntp.h"
// #include "SD_MMC.h"            // SD Card ESP32


const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.nist.gov";
const long gmtOffset_sec = 19800;  // GMT offset for IST (UTC +5:30) in seconds
const int daylightOffset_sec = 0;  // No daylight saving time in India

const char *time_zone = "IST-5:30";  // TimeZone for India Standard Time (IST)

String getLocalTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("No time available (yet)");
    return "";
  }

  char timeString[20]; // Buffer to hold formatted time
  // Format: YYYY-mm-DD-HH-MM-SS
  strftime(timeString, sizeof(timeString), "%Y-%m-%d-%H-%M-%S", &timeinfo);

  Serial.println(timeString);
  return String(timeString);
}


// Callback function (gets called when time adjusts via NTP)
void timeavailable(struct timeval *t) {
  Serial.println("Got time adjustment from NTP!");
  Serial.println(getLocalTimeString());
}

// WifI credentials
const char* ssid = "YOUR-SSID";
const char* password = ""YOUR-PASSWORD;

// Insert Firebase project API Key
#define API_KEY "YOUR FIREBASE API KEY"

// Insert Authorized Email and Corresponding Password for Firebase
#define USER_EMAIL "your-email@gmail.com"
#define USER_PASSWORD "your-password"

// Insert Firebase storage bucket ID e.g bucket-name.appspot.com
#define STORAGE_BUCKET_ID "your-torage-bucket-id"

// define the number of bytes you want to access
#define EEPROM_SIZE 4
#define COUNTER_ADDR 0

// OV2640 camera module pins (CAMERA_MODEL_AI_THINKER)
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

// For WiFi connection
#define MAX_RETRY 10
// define the number of bytes you want to access
#define EEPROM_SIZE 1
#define EEPROM_ADDR 0

boolean takeNewPhoto = true;

//Define Firebase Data objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig configF;

void fcsUploadCallback(FCS_UploadStatusInfo info);

bool taskCompleted = false;

int pictureNumber = 0;

// Take a picture every one hour
uint64_t deep_sleep_interval = 3600;

void configure_time() {
    /**
   * NTP server address could be acquired via DHCP,
   *
   * NOTE: This call should be made BEFORE esp32 acquires IP address via DHCP,
   * otherwise SNTP option 42 would be rejected by default.
   * NOTE: configTime() function call if made AFTER DHCP-client run
   * will OVERRIDE acquired NTP server address
   */
  esp_sntp_servermode_dhcp(1);  // (optional)

  // set notification call-back function
  sntp_set_time_sync_notification_cb(timeavailable);

  /**
   * This will set configured ntp servers and constant TimeZone/daylightOffset
   * should be OK if your time zone does not need to adjust daylightOffset twice a year,
   * in such a case time adjustment won't be handled automagically.
   */
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
}

// Get the picture filename with local time
String getPictureFilename(void) {

  if(WiFi.status() == WL_CONNECTED){
    String filename = "/cam-madurai-" + getLocalTimeString()  + ".jpg";
    return String(filename);
  } else{

    // initialize EEPROM with predefined size
    EEPROM.begin(EEPROM_SIZE);

    // Get photo number from EEPROM
    pictureNumber = EEPROM.read(EEPROM_ADDR);

    char buffer[50]; // Buffer to hold the formatted filename

    // Create photo name with number
    sprintf(buffer, "/cam-madurai-%06d.jpg", pictureNumber);
    String filename = String(buffer);

    pictureNumber++;

    // Increment photo number and write to EEPROM
    EEPROM.write(EEPROM_ADDR, pictureNumber);
    EEPROM.commit();
    
    return String(filename);

  }
}

// Capture Photo and Save it to LittleFS and micro SD
String capturePhotoSaveLittleFS(void) {

  String file_path = getPictureFilename();

  Serial.print("File name: ");
  Serial.println(file_path);

  // Dispose first pictures because of bad quality
  camera_fb_t* fb = NULL;

  // Skip first 3 frames (increase/decrease number as needed).
  for (int i = 0; i < 4; i++) {
    fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = NULL;
  }
    
  // Take a new photo
  fb = NULL;  
  fb = esp_camera_fb_get();  
  if(!fb) {
    Serial.println("Camera capture failed");
    delay(1000);
    ESP.restart();
  }  

  File file = LittleFS.open(file_path, FILE_WRITE);

  // Insert the data in the photo file
  if (!file) {
    Serial.println("Failed to open file in writing mode");
  }
  else {
    file.write(fb->buf, fb->len); // payload (image), payload length
    Serial.print("The picture has been saved in ");
    Serial.print(file_path);
    Serial.print(" - Size: ");
    Serial.print(fb->len);
    Serial.println(" bytes");
  }
  // Close the file
  file.close();

  // Initialize SD card
  initMicroSDCard();

  // Save photo to SD card 
  save_to_microsd(fb, file_path);

  esp_camera_fb_return(fb);

  return file_path;
}

void initWiFi(){
  WiFi.begin(ssid, password);

  int num_trials = 0;

  while(WiFi.status() != WL_CONNECTED) {
    num_trials++;
    delay(1000);
    Serial.print("Connecting to WiFi, attempt number ");
    Serial.println(num_trials);
    if(num_trials >= MAX_RETRY) {
      break;
    }
  }

  // If wifi not connected, go to sleep
  if(WiFi.status() != WL_CONNECTED) {
      // esp_deep_sleep_start();
      Serial.println("No Wifi, photos will be saved to micro SD");
  } else {
    Serial.println("Hurray!, Wifi connected");
  }
}

void initLittleFS(){
  if (!LittleFS.begin(true)) {
    Serial.println("An Error has occurred while mounting LittleFS");
    ESP.restart();
  }
  else {
    delay(500);
    Serial.println("LittleFS mounted successfully");
  }
}

void deleteSpecificFile(const char *path) {
    File file = LittleFS.open(path, "r");
    if (file) {
        file.close(); // Close the file if it was opened
        Serial.printf("Closed file: %s\n", path);
    }

    // Now try to delete the file
    if (LittleFS.remove(path)) {
        Serial.printf("Successfully deleted: %s\n", path);
    } else {
        Serial.printf("Failed to delete %s\n", path);
    }
}

void removeAllFiles() {
    // Open the root directory
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("Failed to open LittleFS root directory");
        return;
    }

    // Iterate through all files in the directory
    File file = root.openNextFile();
    while (file) {
        String filePath = String("/") + file.name();
        Serial.printf("Deleting file: %s\n", filePath.c_str());
        
        file.close();

        // Remove the file
        if (LittleFS.remove(filePath)) {
            Serial.printf("Successfully deleted %s\n", filePath.c_str());
        } else {
            Serial.printf("Failed to delete %s\n", filePath.c_str());
        }

        // Move to the next file
        file = root.openNextFile();
    }
    Serial.println("All files removed.");
}

// Initialize the micro SD card
void initMicroSDCard(){
  // Start Micro SD card
  Serial.println("Starting SD Card");
  if(!SD_MMC.begin()){
    Serial.println("SD Card Mount Failed");
    return;
  }
  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE){
    Serial.println("No SD Card attached");
    return;
  }
}

void save_to_microsd(camera_fb_t* fb, String filpath){
  // Path where new picture will be saved in SD Card

  // Save picture to microSD card
  fs::FS &fs = SD_MMC; 
  File file = fs.open(filpath.c_str(),FILE_WRITE);
  if(!file){
    Serial.printf("Failed to open file in writing mode");
  } 
  else {
    file.write(fb->buf, fb->len); // payload (image), payload length
    Serial.printf("Saved: %s\n", filpath.c_str());
  }
  file.close();

}

void initCamera(){
 // OV2640 camera module
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

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }
  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    ESP.restart();
  } 
}

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);

  // Enable deep sleep
  esp_sleep_enable_timer_wakeup(deep_sleep_interval * 1000000);

  // initWifi
  initWiFi();
  configure_time();
  delay(5000);

  Serial.println(getLocalTimeString());

  // Init little File System
  initLittleFS();

  // Remove allold file
  removeAllFiles();

  // Turn-off the 'brownout detector'
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Init camera
  initCamera();

  //Firebase
  // Assign the api key
  configF.api_key = API_KEY;
  //Assign the user sign in credentials
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  //Assign the callback function for the long running token generation task
  configF.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
  esp_sleep_enable_timer_wakeup(deep_sleep_interval * 1000000);

}

void loop() {
  
  String file_photo_path = capturePhotoSaveLittleFS();
  delay(1);

  // If WiFi connected, upload to Firebase cloud storage
  if(WiFi.status() == WL_CONNECTED) {
    
    Firebase.begin(&configF, &auth);
    Firebase.reconnectWiFi(true);

    if (Firebase.ready() && !taskCompleted){
      taskCompleted = true;
      Serial.print("Uploading picture... ");

      String bucket_photo = "/data" + file_photo_path;

      //MIME type should be valid to avoid the download problem.
      //The file systems for flash and SD/SDMMC can be changed in FirebaseFS.h.
      if (Firebase.Storage.upload(
        &fbdo,STORAGE_BUCKET_ID /* Firebase Storage bucket id */,
        file_photo_path /* path to local file */,
        mem_storage_type_flash /* memory storage type, mem_storage_type_flash and mem_storage_type_sd */,
        bucket_photo /* path of remote file stored in the bucket */,
        "image/jpeg" /* mime type */,
        fcsUploadCallback)){
        Serial.printf("\nDownload URL: %s\n", fbdo.downloadURL().c_str());
      }
      else{
        Serial.println(fbdo.errorReason());
      }
      // Wait 2 seconds for upload
      delay(2000);
    }
  }

  Serial.printf("Going to deep sleep for %d minutes\n", deep_sleep_interval);
  // Deep sleep to save battery
  esp_deep_sleep_start();
}

// The Firebase Storage upload callback function
void fcsUploadCallback(FCS_UploadStatusInfo info){
    if (info.status == firebase_fcs_upload_status_init){
        Serial.printf("Uploading file %s (%d) to %s\n", info.localFileName.c_str(), info.fileSize, info.remoteFileName.c_str());
    }
    else if (info.status == firebase_fcs_upload_status_upload)
    {
        Serial.printf("Uploaded %d%s, Elapsed time %d ms\n", (int)info.progress, "%", info.elapsedTime);
    }
    else if (info.status == firebase_fcs_upload_status_complete)
    {
        Serial.println("Upload completed\n");
        FileMetaInfo meta = fbdo.metaData();
        Serial.printf("Name: %s\n", meta.name.c_str());
        Serial.printf("Bucket: %s\n", meta.bucket.c_str());
        Serial.printf("contentType: %s\n", meta.contentType.c_str());
        Serial.printf("Size: %d\n", meta.size);
        Serial.printf("Generation: %lu\n", meta.generation);
        Serial.printf("Metageneration: %lu\n", meta.metageneration);
        Serial.printf("ETag: %s\n", meta.etag.c_str());
        Serial.printf("CRC32: %s\n", meta.crc32.c_str());
        Serial.printf("Tokens: %s\n", meta.downloadTokens.c_str());
        Serial.printf("Download URL: %s\n\n", fbdo.downloadURL().c_str());
    }
    else if (info.status == firebase_fcs_upload_status_error){
        Serial.printf("Upload failed, %s\n", info.errorMsg.c_str());
    }
}
