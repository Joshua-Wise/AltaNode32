// Import required libraries
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <mbedtls/aes.h>
#include <esp_system.h>
#include <Arduino.h>

// Constants and global variables
#define KEY_SIZE 16
const size_t JSON_BUFFER_SIZE = 768;

const char* http_username = "admin";
const char* http_password = "altanode";
const int chipSelect = 5; // SD card CS pin for ESP32
const int buttonPins[] = {12, 13, 14, 27};
const char *setupfile = "/config/setup.json";
const char *wififile = "/config/wifi.json";

String ssid, password, apiUrl;
int entryValues[4];

WebServer server(80);
mbedtls_aes_context aes;

String urlEncode(const String& input) {
  const char *hex = "0123456789ABCDEF";
  String output = "";
  for (int i = 0; i < input.length(); i++) {
    char c = input.charAt(i);
    if (isAlphaNumeric(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      output += c;
    } else {
      output += '%';
      output += hex[c >> 4];
      output += hex[c & 0xF];
    }
  }
  return output;
}

String urlDecode(String input) {
  String decoded = "";
  char temp[] = "0x00";
  unsigned int len = input.length();
  unsigned int i = 0;
  while (i < len) {
    if (input[i] == '%') {
      if (i + 2 < len) {
        temp[2] = input[i + 1];
        temp[3] = input[i + 2];
        decoded += (char)strtol(temp, NULL, 16);
        i += 2;
      }
    } else if (input[i] == '+') {
      decoded += ' ';
    } else {
      decoded += input[i];
    }
    i++;
  }
  return decoded;
}

// Function declarations
void getEncryptionKey(uint8_t* key);
void encryptData(char* input, char* output, size_t inputSize);
void decryptData(const char* input, char* output, size_t inputSize);
void writeEncryptedConfig(File& file, const char* config, size_t configSize);
void readEncryptedConfig(File& file, char* config, size_t configSize);
bool isJsonEncrypted(const char* jsonString, size_t length);
void loadWifi();
void saveWifiToFile(const String& new_ssid, const String& new_pass);
void loadSetup();
void saveSetupToFile(const String& new_apiUrl, const int new_entryValues[4]);
void setupWebServer();
void handleRoot();
void handleWifi();
void handleSetup();
void handleSaveWifi();
void handleSaveSetup();
void handleWebRestart();
bool sendAPIRequest(int buttonIndex);
void checkWiFiConnection();

// Encryption functions
void getEncryptionKey(uint8_t* key) {
  for (int i = 0; i < KEY_SIZE; i++) {
    key[i] = EEPROM.read(i);
  }
}

void encryptData(char* input, char* output, size_t inputSize) {
  uint8_t key[KEY_SIZE];
  getEncryptionKey(key);
  
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, KEY_SIZE * 8);
  
  size_t paddedSize = (inputSize + 15) & ~15;
  for (size_t i = 0; i < paddedSize; i += 16) {
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, (uint8_t*)input + i, (uint8_t*)output + i);
  }
  
  mbedtls_aes_free(&aes);
}

void decryptData(const char* input, char* output, size_t inputSize) {
  uint8_t key[KEY_SIZE];
  getEncryptionKey(key);
  
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, key, KEY_SIZE * 8);
  
  size_t paddedSize = (inputSize + 15) & ~15;
  for (size_t i = 0; i < paddedSize; i += 16) {
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, (uint8_t*)input + i, (uint8_t*)output + i);
  }
  
  mbedtls_aes_free(&aes);
  
  while (paddedSize > 0 && output[paddedSize-1] == 0) {
    paddedSize--;
  }
  output[paddedSize] = '\0';
}

void writeEncryptedConfig(File& file, const char* config, size_t configSize) {
  size_t paddedSize = (configSize + 15) & ~15;
  char* paddedConfig = new char[paddedSize];
  memset(paddedConfig, 0, paddedSize);
  memcpy(paddedConfig, config, configSize);
  
  char* encryptedConfig = new char[paddedSize];
  encryptData(paddedConfig, encryptedConfig, paddedSize);
  
  size_t bytesWritten = file.write((uint8_t*)encryptedConfig, paddedSize);
  
  Serial.printf("Original size: %d, Padded size: %d, Bytes written: %d\n", configSize, paddedSize, bytesWritten);
  
  delete[] paddedConfig;
  delete[] encryptedConfig;
}

void readEncryptedConfig(File& file, char* config, size_t configSize) {
  size_t paddedSize = (configSize + 15) & ~15;
  char* encryptedConfig = new char[paddedSize];
  file.read((uint8_t*)encryptedConfig, paddedSize);
  
  decryptData(encryptedConfig, config, paddedSize);
  
  delete[] encryptedConfig;
}

bool isJsonEncrypted(const char* jsonString, size_t length) {
  if (length > 0 && jsonString[0] == '{') {
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, jsonString, length);
    return error != DeserializationError::Ok;
  }
  return true;
}

// WiFi functions
void loadWifi() {
  File dataFile = SD.open(wififile, FILE_READ);
  if (!dataFile) {
    Serial.println("Failed to open wifi.json");
    return;
  }

  size_t fileSize = dataFile.size();
  char* jsonBuffer = new char[fileSize + 1];
  
  size_t bytesRead = dataFile.readBytes(jsonBuffer, fileSize);
  jsonBuffer[bytesRead] = '\0';
  dataFile.close();

  bool encrypted = isJsonEncrypted(jsonBuffer, bytesRead);
  Serial.printf("WiFi data is %s\n", encrypted ? "encrypted" : "unencrypted");

  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  DeserializationError error;

  if (encrypted) {
    char* decryptedJson = new char[fileSize];
    decryptData(jsonBuffer, decryptedJson, fileSize);
    error = deserializeJson(doc, decryptedJson);
    delete[] decryptedJson;
  } else {
    error = deserializeJson(doc, jsonBuffer);
  }

  delete[] jsonBuffer;

  if (error) {
    Serial.print(F("Parsing WiFi JSON failed: "));
    Serial.println(error.c_str());
    return;
  }

  ssid = doc["ssid"].as<String>();
  password = doc["password"].as<String>();

  if (!encrypted) {
    Serial.println("Encrypting and saving WiFi configuration...");
    saveWifiToFile(ssid, password);
  }
}

void saveWifiToFile(const String& new_ssid, const String& new_pass) {
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  doc["ssid"] = new_ssid;
  doc["password"] = new_pass;

  String jsonString;
  serializeJson(doc, jsonString);

  SD.remove(wififile);
  File file = SD.open(wififile, FILE_WRITE);
  if (!file) {
    Serial.println(F("Failed to create WiFi config file"));
    return;
  }

  writeEncryptedConfig(file, jsonString.c_str(), jsonString.length());
  file.close();

  Serial.println("Encrypted WiFi configuration saved to SD card");
}

// Setup functions
void loadSetup() {
  File dataFile = SD.open(setupfile, FILE_READ);
  if (!dataFile) {
    Serial.println("Failed to open setup.json");
    return;
  }

  size_t fileSize = dataFile.size();
  char* jsonBuffer = new char[fileSize + 1];
  
  dataFile.readBytes(jsonBuffer, fileSize);
  jsonBuffer[fileSize] = '\0';
  dataFile.close();

  bool encrypted = isJsonEncrypted(jsonBuffer, fileSize);
  Serial.printf("Setup data is %s\n", encrypted ? "encrypted" : "unencrypted");

  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  DeserializationError error;

  if (encrypted) {
    char* decryptedJson = new char[fileSize];
    decryptData(jsonBuffer, decryptedJson, fileSize);
    error = deserializeJson(doc, decryptedJson);
    delete[] decryptedJson;
  } else {
    error = deserializeJson(doc, jsonBuffer);
  }

  delete[] jsonBuffer;

  if (error) {
    Serial.print(F("Parsing setup JSON failed: "));
    Serial.println(error.c_str());
    return;
  }

  apiUrl = doc["apiurl"].as<String>();
  for (int i = 1; i <= 4; i++) {
    entryValues[i - 1] = doc["entries"][String(i)];
  }

  Serial.print("API URL: ");
  Serial.println(apiUrl);
  for (int i = 0; i < 4; i++) {
    Serial.printf("Entry ID %d: %d\n", i + 1, entryValues[i]);
  }

  if (!encrypted) {
    Serial.println("Encrypting and saving setup configuration...");
    saveSetupToFile(apiUrl, entryValues);
  }
}

void saveSetupToFile(const String& new_apiUrl, const int new_entryValues[4]) {
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  doc["apiurl"] = new_apiUrl;
  JsonObject entries = doc["entries"].to<JsonObject>();
  for (int i = 0; i < 4; i++) {
    entries[String(i + 1)] = new_entryValues[i];
  }

  String jsonString;
  serializeJson(doc, jsonString);

  SD.remove(setupfile);
  File file = SD.open(setupfile, FILE_WRITE);
  if (!file) {
    Serial.println(F("Failed to create setup file"));
    return;
  }

  writeEncryptedConfig(file, jsonString.c_str(), jsonString.length());
  file.close();

  Serial.println("Encrypted setup configuration saved to SD card");
}

// Web server functions
void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/wifi", handleWifi);
  server.on("/setup", handleSetup);
  server.on("/saveWifi", handleSaveWifi);
  server.on("/saveSetup", handleSaveSetup);
  server.on("/webRestart", handleWebRestart);
  server.on("/logout", []() {
    server.send(401, "text/plain", "Unauthorized");
  });

  server.begin();
  Serial.println("Web server started on port 80");
}

void handleRoot() {
  File htmlFile = SD.open("/html/index.html", FILE_READ);
  if (!htmlFile) {
    server.send(500, "text/plain", "Error: Could not open index.html");
    return;
  }
  String htmlContent = htmlFile.readString();
  htmlFile.close();
  server.send(200, "text/html", htmlContent);
}

void handleWifi() {
  if (!server.authenticate(http_username, http_password)) {
    return server.requestAuthentication();
  }
  
  File htmlFile = SD.open("/html/wifi.html", FILE_READ);
  if (!htmlFile) {
    server.send(500, "text/plain", "Error: Could not open wifi.html");
    return;
  }
  
  String htmlContent = htmlFile.readString();
  htmlFile.close();
  
  // Replace placeholders with actual data
  htmlContent.replace("%%SSID%%", ssid);
  htmlContent.replace("%%PASSWORD%%", password);
  
  server.send(200, "text/html", htmlContent);
}

void handleSetup() {
  if (!server.authenticate(http_username, http_password)) {
    return server.requestAuthentication();
  }
  
  File htmlFile = SD.open("/html/setup.html", FILE_READ);
  if (!htmlFile) {
    server.send(500, "text/plain", "Error: Could not open setup.html");
    return;
  }
  
  String htmlContent = htmlFile.readString();
  htmlFile.close();
  
  // Replace placeholders with actual data
  htmlContent.replace("%%API_URL%%", apiUrl);
  for (int i = 0; i < 4; i++) {
    htmlContent.replace("%%ENTRY" + String(i+1) + "%%", String(entryValues[i]));
  }
  
  server.send(200, "text/html", htmlContent);
}

void handleSaveWifi() {
  if (!server.hasArg("webssid") || !server.hasArg("webpass")) {
    server.send(400, "text/plain", "Missing parameters");
    return;
  }
  
  String new_ssid = server.arg("webssid");
  String new_pass = server.arg("webpass");
  
  saveWifiToFile(new_ssid, new_pass);
  
  File htmlFile = SD.open("/html/save.html", FILE_READ);
  if (!htmlFile) {
    server.send(500, "text/plain", "Error: Could not open save.html");
    return;
  }
  String htmlContent = htmlFile.readString();
  htmlFile.close();
  server.send(200, "text/html", htmlContent);
}

void handleSaveSetup() {
  if (!server.hasArg("webapiurl")) {
    server.send(400, "text/plain", "Missing API URL parameter");
    return;
  }
  
  String new_apiUrl = urlDecode(server.arg("webapiurl"));
  
  int new_entryValues[4];
  for (int i = 0; i < 4; i++) {
    String paramName = "webentry" + String(i+1);
    if (server.hasArg(paramName)) {
      new_entryValues[i] = server.arg(paramName).toInt();
    } else {
      new_entryValues[i] = 0;
    }
  }
  
  saveSetupToFile(new_apiUrl, new_entryValues);
  
  Serial.println("Saving new setup configuration:");
  Serial.println("API URL: " + new_apiUrl);
  for (int i = 0; i < 4; i++) {
    Serial.println("Entry " + String(i+1) + ": " + String(new_entryValues[i]));
  }
  
  File htmlFile = SD.open("/html/save.html", FILE_READ);
  if (!htmlFile) {
    server.send(500, "text/plain", "Error: Could not open save.html");
    return;
  }
  String htmlContent = htmlFile.readString();
  htmlFile.close();
  server.send(200, "text/html", htmlContent);
}

void handleWebRestart() {
  Serial.println("Web restart requested");
  server.send(200, "text/plain", "Restarting device...");
  
  // Properly close and cleanup resources
  Serial.println("Closing web server...");
  server.close();
  
  Serial.println("Disconnecting WiFi...");
  WiFi.disconnect(true);
  
  Serial.println("Closing SD card...");
  SD.end();
  
  Serial.println("Cleanup complete, restarting in 2 seconds...");
  delay(2000);
  
  // Use hardware reset instead of ESP.restart()
  esp_restart();
}

// API request function with proper error handling
bool sendAPIRequest(int buttonIndex) {
  WiFiClientSecure *client = new WiFiClientSecure;
  client->setInsecure();
  client->setTimeout(5000); // 5 second timeout
  
  HTTPClient https;
  if (!https.begin(*client, apiUrl)) {
    Serial.println("Failed to begin HTTPS connection");
    delete client;
    return false;
  }
  
  https.addHeader("Content-Type", "application/x-www-form-urlencoded");
  https.setTimeout(5000);
  
  String httpRequestData = "entryId=" + String(entryValues[buttonIndex]);
  int httpResponseCode = https.POST(httpRequestData);
  
  bool success = (httpResponseCode > 0);
  if (success) {
    Serial.printf("Button %d API call successful: %d\n", buttonIndex + 1, httpResponseCode);
  } else {
    Serial.printf("Button %d API call failed: %s\n", buttonIndex + 1, https.errorToString(httpResponseCode).c_str());
  }
  
  https.end();
  delete client;
  return success;
}

// WiFi connection monitoring and reconnection
void checkWiFiConnection() {
  // Only check WiFi if system has been running for at least 10 seconds
  // This prevents TCP stack issues during boot
  static bool systemReady = false;
  if (millis() < 10000) {
    return;
  }
  systemReady = true;
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, attempting reconnect...");
    
    // Disconnect cleanly first
    WiFi.disconnect();
    delay(1000);
    
    // Reconnect with fresh connection
    WiFi.begin(ssid.c_str(), password.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
      Serial.print(".");
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi reconnected");
      Serial.printf("WiFi IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
      Serial.println("WiFi reconnection failed");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Give serial time to initialize
  Serial.println("AltaNode32 starting up...");
  
  EEPROM.begin(KEY_SIZE);

  for (int pin : buttonPins) {
    pinMode(pin, INPUT_PULLUP);
  }

  if (!SD.begin(chipSelect)) {
    Serial.println("SD card initialization failed - system cannot continue");
    Serial.println("Please check SD card connection and contents");
    while(true) {
      delay(5000); // Halt execution but keep serial active
      Serial.println("SD card required for operation");
    }
  }
  Serial.println("SD card initialized.");

  loadWifi();

  // Add WiFi connection timeout to prevent infinite loops
  Serial.println("Attempting WiFi connection...");
  WiFi.begin(ssid.c_str(), password.c_str());

  int wifiAttempts = 0;
  const int maxWifiAttempts = 30; // 30 seconds maximum
  
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < maxWifiAttempts) {
    Serial.printf("Connecting to WiFi... (attempt %d/%d)\n", wifiAttempts + 1, maxWifiAttempts);
    delay(1000);
    wifiAttempts++;
    
    // Feed the watchdog to prevent reset
    yield();
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected");
    Serial.printf("WiFi IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("WiFi MAC: %s\n", WiFi.macAddress().c_str());
  } else {
    Serial.println("WiFi connection failed - continuing in offline mode");
    Serial.println("Device will still function for local configuration");
  }

  loadSetup();

  // Start web server
  Serial.println("Starting web server...");
  setupWebServer();
  
  Serial.println("System initialization complete");
  Serial.println("Ready for operation");
}

void loop() {
  static unsigned long lastButtonPress[4] = {0};
  static unsigned long lastWiFiCheck = 0;
  static unsigned long lastMemoryCheck = 0;
  static bool firstLoop = true;
  const unsigned long debounceDelay = 200;
  const unsigned long wifiCheckInterval = 30000; // Check WiFi every 30 seconds
  const unsigned long memoryCheckInterval = 60000; // Check memory every 60 seconds
  
  unsigned long currentTime = millis();
  
  // Print a message on first loop to confirm we made it this far
  if (firstLoop) {
    Serial.println("Main loop started - system is running");
    firstLoop = false;
  }
  
  // Periodic memory health check
  if (currentTime - lastMemoryCheck > memoryCheckInterval) {
    lastMemoryCheck = currentTime;
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 10000) { // Less than 10KB free
      Serial.printf("Warning: Low memory - %d bytes free\n", freeHeap);
    }
  }
  
  // Handle web server requests with error protection
  yield(); // Feed watchdog before handling requests
  server.handleClient();
  
  // Only start checking WiFi after system has been running for 15 seconds
  if (currentTime > 15000) {
    // Check WiFi connection periodically
    if (currentTime - lastWiFiCheck > wifiCheckInterval) {
      lastWiFiCheck = currentTime;
      checkWiFiConnection();
    }
  }
  
  // Handle button presses
  for (int i = 0; i < 4; i++) {
    if (digitalRead(buttonPins[i]) == LOW) {
      if (currentTime - lastButtonPress[i] > debounceDelay) {
        lastButtonPress[i] = currentTime;
        Serial.printf("Button %d pressed!\n", i + 1);
        
        // Only check WiFi immediately if system has been running long enough
        if (currentTime > 15000 && WiFi.status() != WL_CONNECTED) {
          Serial.println("WiFi not connected, attempting immediate reconnect...");
          yield(); // Feed watchdog during reconnect
          checkWiFiConnection();
        }
        
        if (WiFi.status() == WL_CONNECTED) {
          yield(); // Feed watchdog before API call
          sendAPIRequest(i);
        } else {
          Serial.printf("Button %d press ignored - no WiFi connection\n", i + 1);
        }
      }
    }
  }
  
  // Feed watchdog and yield to system
  yield();
  delay(10); // Small non-blocking delay
}