#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <time.h>

// Konfigurasi MQTT
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_client_id = "ESP32_Sender2";
const char* senderID = "ESP32_Sender2";

// Pin
const int callButton = 33;
const int billButton = 25;
const int resetButton = 26;
const int greenLed = 12;
const int blueLed = 13;
const int wifiLed = 14;

int lastCallState = HIGH;
int lastBillState = HIGH;
int lastResetState = HIGH;

int callCount = 0;
int billCount = 0;
bool wifiConnected = false;

WiFiClient espClient;
PubSubClient client(espClient);
WiFiManager wifiManager;

void setup() {
  Serial.begin(115200);

  pinMode(callButton, INPUT_PULLUP);
  pinMode(billButton, INPUT_PULLUP);
  pinMode(resetButton, INPUT_PULLUP);
  pinMode(greenLed, OUTPUT);
  pinMode(blueLed, OUTPUT);
  pinMode(wifiLed, OUTPUT);

  digitalWrite(greenLed, LOW);
  digitalWrite(blueLed, LOW);
  digitalWrite(wifiLed, HIGH);

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);

  // Sinkron waktu ke WIB (UTC+7)
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  while (time(nullptr) < 100000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\n🕒 Waktu tersinkron!");

  // Reset awal saat boot
  resetSystem();
}

void setup_wifi() {
  wifiManager.setTimeout(180);
  if (!wifiManager.autoConnect("Sender2_AP")) {
    Serial.println("Gagal konek WiFi. Restart...");
    ESP.restart();
  }
  Serial.println("WiFi connected: " + WiFi.localIP().toString());
  wifiConnected = true;
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("MQTT reconnect...");
    if (client.connect(mqtt_client_id)) {
      Serial.println("connected");
      client.subscribe("waitress/reset");                        // Dari Receiver (global reset)
      client.subscribe("waitress/ESP32_Sender2/call");           // Dari Receiver (per type)
      client.subscribe("waitress/ESP32_Sender2/bill");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      delay(5000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("📥 Received on %s: ", topic);
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, message);
  if (error) {
    Serial.println("❌ JSON Parsing Failed");
    return;
  }

  const char* fromID = doc["id"];
  const char* type = doc["type"];
  bool status = doc["status"];

  // Jika pesan berasal dari receiver dan status false (reset)
  if (String(fromID) == "ESP32_Receiver" && status == false) {
    if (String(type) == "call" || String(type) == "bill") {
      resetSystem();
    }
  }
}

void sendMessage(const char* type, bool status, int count) {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char timeString[40];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);

  int rssi = WiFi.RSSI();
  String topic = "waitress/" + String(senderID) + "/" + String(type);

  String payload = "{";
  payload += "\"id\":\"" + String(senderID) + "\",";
  payload += "\"type\":\"" + String(type) + "\",";
  payload += "\"status\":" + String(status ? "true" : "false") + ",";
  payload += "\"count\":" + String(count) + ",";
  payload += "\"rssi\":" + String(rssi) + ",";
  payload += "\"timestamp\":\"" + String(timeString) + "\"";
  payload += "}";

  client.publish(topic.c_str(), payload.c_str(), true);
  Serial.printf("📤 Sent to %s: %s\n", topic.c_str(), payload.c_str());
}

void resetSystem() {
  digitalWrite(greenLed, LOW);
  digitalWrite(blueLed, LOW);

  sendMessage("call", false, callCount);
  sendMessage("bill", false, billCount);
  Serial.println("🔄 Reset System");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
  } else {
    wifiConnected = false;
  }

  static unsigned long lastBlinkTime = 0;
  if (wifiConnected) {
    if (millis() - lastBlinkTime > 500) {
      digitalWrite(wifiLed, !digitalRead(wifiLed));
      lastBlinkTime = millis();
    }
  } else {
    digitalWrite(wifiLed, HIGH);
  }

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  int callState = digitalRead(callButton);
  int billState = digitalRead(billButton);
  int resetState = digitalRead(resetButton);

  if (callState == LOW && lastCallState == HIGH) {
    callCount++;
    digitalWrite(greenLed, HIGH);
    digitalWrite(blueLed, LOW);
    sendMessage("call", true, callCount);
    sendMessage("bill", false, billCount);
    delay(200);
  }

  if (billState == LOW && lastBillState == HIGH) {
    billCount++;
    digitalWrite(greenLed, LOW);
    digitalWrite(blueLed, HIGH);
    sendMessage("call", false, callCount);
    sendMessage("bill", true, billCount);
    delay(200);
  }

  if (resetState == LOW && lastResetState == HIGH) {
    resetSystem();
    delay(200);
  }

  lastCallState = callState;
  lastBillState = billState;
  lastResetState = resetState;
}
