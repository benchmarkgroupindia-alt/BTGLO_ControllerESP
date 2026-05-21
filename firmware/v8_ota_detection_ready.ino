#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "esp_system.h"
#include "time.h"

#define RELAY_PIN 23

// ======================
// CURRENT FIRMWARE
// ======================

String CURRENT_FIRMWARE = "v8.0";

// ======================
// WIFI
// ======================

const char* ssid = "OnePlus 12R";
const char* password = "12345678";

// ======================
// NTP TIME
// ======================

const char* ntpServer = "pool.ntp.org";

const long gmtOffset_sec = 19800;

const int daylightOffset_sec = 0;

// ======================
// SUPABASE
// ======================

const char* fetchUrl =
"https://zmdqorydvqhhcycofqea.supabase.co/rest/v1/devices?device_id=eq.BTGLO_001&select=*";

const char* updateUrl =
"https://zmdqorydvqhhcycofqea.supabase.co/rest/v1/devices?device_id=eq.BTGLO_001";

const char* supabaseKey =
"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InptZHFvcnlkdnFoaGN5Y29mcWVhIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzkyNzc3MDMsImV4cCI6MjA5NDg1MzcwM30.KupBxcLvTC31LHHRNeA_8N5Fc7hXAtCB_y5MyLlPseM";

// ======================
// TIMERS
// ======================

unsigned long lastRelayFetch = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastTimePrint = 0;
unsigned long lastInternetCheck = 0;
unsigned long lastOTACheck = 0;

// ======================
// TELEMETRY
// ======================

Preferences preferences;

int restartCount = 0;

// ======================
// NETWORK HEALTH
// ======================

int reconnectAttempts = 0;
int internetFailCount = 0;

// ======================
// WATCHDOG PROTECTION
// ======================

String lastRestartTriggered = "";

// ======================
// WIFI CONNECT
// ======================

void connectWiFi() {

  Serial.println("================================");
  Serial.println("CONNECTING TO WIFI");
  Serial.println("================================");

  WiFi.mode(WIFI_STA);

  WiFi.setSleep(false);

  WiFi.begin(ssid, password);

  unsigned long startAttempt = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttempt < 20000) {

    delay(500);

    Serial.print(".");
  }

  Serial.println("");

  if (WiFi.status() == WL_CONNECTED) {

    reconnectAttempts = 0;

    Serial.println("WiFi Connected!");

    Serial.print("IP: ");

    Serial.println(WiFi.localIP());

    return;
  }

  reconnectAttempts++;

  Serial.println("WiFi Connection Failed");

  Serial.print("Reconnect Attempts: ");

  Serial.println(reconnectAttempts);

  if (reconnectAttempts >= 5) {

    Serial.println("ESP RESTARTING...");

    delay(3000);

    ESP.restart();
  }
}

// ======================
// INTERNET CHECK
// ======================

bool checkInternet() {

  if (WiFi.status() != WL_CONNECTED) {

    return false;
  }

  WiFiClientSecure client;

  client.setInsecure();

  HTTPClient http;

  http.setTimeout(5000);

  http.begin(client, fetchUrl);

  http.addHeader("apikey", supabaseKey);

  http.addHeader("Authorization",
  String("Bearer ") + supabaseKey);

  int responseCode = http.GET();

  http.end();

  client.stop();

  if (responseCode == 200) {

    internetFailCount = 0;

    Serial.println("Internet Health OK");

    return true;
  }

  internetFailCount++;

  Serial.println("Internet Health Failed");

  Serial.print("Internet Fail Count: ");

  Serial.println(internetFailCount);

  if (internetFailCount >= 5) {

    Serial.println("INTERNET DEAD");

    Serial.println("ESP RESTARTING...");

    delay(3000);

    ESP.restart();
  }

  return false;
}

// ======================
// OTA CHECK ENGINE
// ======================

void checkOTAUpdate() {

  if (WiFi.status() != WL_CONNECTED) {

    return;
  }

  Serial.println("================================");
  Serial.println("CHECKING FOR OTA UPDATE");
  Serial.println("================================");

  WiFiClientSecure client;

  client.setInsecure();

  HTTPClient http;

  http.setTimeout(5000);

  http.begin(client, fetchUrl);

  http.addHeader("apikey", supabaseKey);

  http.addHeader("Authorization",
  String("Bearer ") + supabaseKey);

  int responseCode = http.GET();

  if (responseCode > 0) {

    String response = http.getString();

    DynamicJsonDocument doc(4096);

    DeserializationError error =
    deserializeJson(doc, response);

    if (error) {

      Serial.println("OTA JSON Parse Failed");

      http.end();

      client.stop();

      return;
    }

    bool otaEnabled =
    doc[0]["ota_enabled"];

    String latestFirmware =
    doc[0]["latest_firmware"];

    String firmwareURL =
    doc[0]["firmware_url"];

    latestFirmware.trim();

    Serial.print("Current Firmware: ");

    Serial.println(CURRENT_FIRMWARE);

    Serial.print("Latest Firmware: ");

    Serial.println(latestFirmware);

    Serial.print("OTA Enabled: ");

    Serial.println(otaEnabled);

    // ======================
    // OTA DETECT
    // ======================

    if (otaEnabled == true &&
        latestFirmware != CURRENT_FIRMWARE) {

      Serial.println("================================");

      Serial.println("NEW OTA UPDATE AVAILABLE");

      Serial.print("DOWNLOAD URL: ");

      Serial.println(firmwareURL);

      Serial.println("================================");
    }

    else {

      Serial.println("Firmware Already Latest");
    }

  } else {

    Serial.print("OTA Check Failed: ");

    Serial.println(responseCode);
  }

  http.end();

  client.stop();
}

// ======================
// TIME INIT
// ======================

void initTime() {

  configTime(gmtOffset_sec,
             daylightOffset_sec,
             ntpServer);

  Serial.println("Syncing Time...");

  struct tm timeinfo;

  while (!getLocalTime(&timeinfo)) {

    Serial.println("Waiting for NTP Time...");

    delay(1000);
  }

  Serial.println("Time Synced!");
}

// ======================
// PRINT TIME
// ======================

void printCurrentTime() {

  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {

    Serial.println("Failed to obtain time");

    return;
  }

  Serial.print("Current Time: ");

  Serial.println(&timeinfo, "%H:%M:%S");
}

// ======================
// BOOT REASON
// ======================

String getBootReason() {

  esp_reset_reason_t reason =
  esp_reset_reason();

  switch(reason) {

    case ESP_RST_POWERON:
      return "POWERON_RESET";

    case ESP_RST_SW:
      return "SW_RESET";

    case ESP_RST_PANIC:
      return "CRASH_RESET";

    case ESP_RST_INT_WDT:
      return "WATCHDOG_RESET";

    case ESP_RST_TASK_WDT:
      return "TASK_WATCHDOG";

    case ESP_RST_BROWNOUT:
      return "BROWNOUT_RESET";

    default:
      return "OTHER_RESET";
  }
}

// ======================
// HEARTBEAT
// ======================

void sendHeartbeat() {

  if (WiFi.status() != WL_CONNECTED) {

    return;
  }

  WiFiClientSecure client;

  client.setInsecure();

  HTTPClient http;

  http.setTimeout(5000);

  http.begin(client, updateUrl);

  http.addHeader("Content-Type",
                 "application/json");

  http.addHeader("apikey", supabaseKey);

  http.addHeader("Authorization",
  String("Bearer ") + supabaseKey);

  int wifiRSSI = WiFi.RSSI();

  unsigned long uptimeSeconds =
  millis() / 1000;

  String jsonData =
  "{"
  "\"online\":true,"
  "\"wifi_rssi\":" + String(wifiRSSI) + ","
  "\"firmware_version\":\"" + CURRENT_FIRMWARE + "\","
  "\"restart_count\":" + String(restartCount) + ","
  "\"uptime\":" + String(uptimeSeconds) + ","
  "\"reconnect_attempts\":" + String(reconnectAttempts) + ","
  "\"internet_fail_count\":" + String(internetFailCount) +
  "}";

  int responseCode =
  http.PATCH(jsonData);

  Serial.print("Heartbeat Response: ");

  Serial.println(responseCode);

  http.end();

  client.stop();
}

// ======================
// WATCHDOG RESTART
// ======================

void performWatchdogRestart() {

  Serial.println("WATCHDOG RESTART INITIATED");

  digitalWrite(RELAY_PIN, HIGH);

  delay(5000);

  digitalWrite(RELAY_PIN, LOW);

  Serial.println("WATCHDOG RESTART COMPLETE");
}

// ======================
// RELAY + SCHEDULER
// ======================

void fetchRelayState() {

  if (WiFi.status() != WL_CONNECTED) {

    return;
  }

  WiFiClientSecure client;

  client.setInsecure();

  HTTPClient http;

  http.setTimeout(5000);

  http.begin(client, fetchUrl);

  http.addHeader("apikey", supabaseKey);

  http.addHeader("Authorization",
  String("Bearer ") + supabaseKey);

  int responseCode = http.GET();

  Serial.println("==========");

  Serial.print("GET Response: ");

  Serial.println(responseCode);

  if (responseCode > 0) {

    String response =
    http.getString();

    DynamicJsonDocument doc(4096);

    DeserializationError error =
    deserializeJson(doc, response);

    if (error) {

      Serial.println("JSON Parse Failed");

      http.end();

      client.stop();

      return;
    }

    String relayState =
    doc[0]["relay_state"];

    bool autoMode =
    doc[0]["auto_mode"];

    String onTime =
    doc[0]["on_time"];

    String offTime =
    doc[0]["off_time"];

    bool watchdogEnabled =
    doc[0]["watchdog_enabled"];

    String restartTime =
    doc[0]["restart_time"];

    relayState.trim();

    relayState.toUpperCase();

    onTime.trim();

    offTime.trim();

    restartTime.trim();

    Serial.print("Relay State: ");

    Serial.println(relayState);

    struct tm timeinfo;

    if (!getLocalTime(&timeinfo)) {

      Serial.println("Failed to get local time");

      http.end();

      client.stop();

      return;
    }

    char currentTime[6];

    strftime(currentTime,
             sizeof(currentTime),
             "%H:%M",
             &timeinfo);

    String current =
    String(currentTime);

    // WATCHDOG

    if (watchdogEnabled == true &&
        current == restartTime &&
        lastRestartTriggered != current) {

      performWatchdogRestart();

      lastRestartTriggered = current;
    }

    if (current != restartTime) {

      lastRestartTriggered = "";
    }

    // AUTO MODE

    if (autoMode == true) {

      if (current == onTime) {

        digitalWrite(RELAY_PIN, LOW);

        Serial.println("SCHEDULED RELAY ON");
      }

      else if (current == offTime) {

        digitalWrite(RELAY_PIN, HIGH);

        Serial.println("SCHEDULED RELAY OFF");
      }
    }

    // MANUAL MODE

    else {

      if (relayState == "ON") {

        digitalWrite(RELAY_PIN, LOW);

        Serial.println("MANUAL RELAY ON");

      } else {

        digitalWrite(RELAY_PIN, HIGH);

        Serial.println("MANUAL RELAY OFF");
      }
    }

  } else {

    Serial.print("GET Error: ");

    Serial.println(responseCode);
  }

  http.end();

  client.stop();
}

// ======================
// SETUP
// ======================

void setup() {

  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, HIGH);

  preferences.begin("btglo", false);

  restartCount =
  preferences.getInt("restarts", 0);

  restartCount++;

  preferences.putInt("restarts",
                     restartCount);

  connectWiFi();

  initTime();

  Serial.println("================================");
  Serial.println("BTGLO CONTROLLER STARTED");
  Serial.println("Firmware Version: " + CURRENT_FIRMWARE);
  Serial.println("================================");
}

// ======================
// LOOP
// ======================

void loop() {

  // WIFI RECOVERY

  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("WiFi Lost!");

    connectWiFi();
  }

  // INTERNET HEALTH

  if (millis() - lastInternetCheck > 30000) {

    checkInternet();

    lastInternetCheck = millis();
  }

  // RELAY FETCH

  if (millis() - lastRelayFetch > 15000) {

    fetchRelayState();

    lastRelayFetch = millis();
  }

  // HEARTBEAT

  if (millis() - lastHeartbeat > 60000) {

    sendHeartbeat();

    lastHeartbeat = millis();
  }

  // OTA CHECK EVERY 2 MIN

  if (millis() - lastOTACheck > 120000) {

    checkOTAUpdate();

    lastOTACheck = millis();
  }

  // TIME PRINT

  if (millis() - lastTimePrint > 10000) {

    printCurrentTime();

    lastTimePrint = millis();
  }

  delay(100);
}
