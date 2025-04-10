#include <WiFiManager.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DFRobotDFPlayerMini.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <BlynkSimpleEsp32.h>

// === OLED Setup ===
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// === DFPlayer Setup ===
HardwareSerial mySerial(2);  // RX = 16, TX = 17
DFRobotDFPlayerMini dfPlayer;

// === MQTT Setup ===
WiFiClient espClient;
PubSubClient client(espClient);
const char *mqtt_server = "broker.emqx.io";

// === Blynk Setup ===
char auth[] = "cjqSPyXSlzm32sMLG0JOfH7ANlSvqe8M"; // Ganti dengan token Blynk Anda

// === WiFi LED Status ===
#define LED_WIFI 14
bool wifiConnected = false;
unsigned long lastBlink = 0;
bool ledState = false;

// === Status dan Waktu Terakhir ===
String statusSender[6] = {"OFF", "OFF", "OFF", "OFF", "OFF", "OFF"};
unsigned long lastMessageTime[6] = {0, 0, 0, 0, 0, 0};
const unsigned long debounceDelay = 1000;

// === Setup Display ===
void setupDisplay() {
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED Gagal Inisialisasi");
    while (1);
  }

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(20, 0);
  display.println("WAITRESS");
  display.setCursor(40, 20);
  display.println("CALL");
  display.setCursor(34, 40);
  display.println("SYSTEM");
  display.display();
  delay(3000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(20, 20);
  display.println("RISKY DAVID K");
  display.setCursor(30, 35);
  display.println("2212101134");
  display.display();
  delay(2000);
}

// === Update OLED Display ===
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  for (int i = 0; i < 6; i++) {
    display.setCursor(0, i * 10);
    display.printf("M%d (%s): %s", (i / 2) + 1, (i % 2 == 0 ? "Call" : "Bill"), statusSender[i].c_str());
  }
  display.display();
}

// === Set Blynk LED Status ===
void setBlynkLED(int senderIndex, bool call, bool bill) {
  int basePin = senderIndex * 3;
  Blynk.virtualWrite(V1 + basePin, call ? 1 : 0);     // Call LED
  Blynk.virtualWrite(V2 + basePin, bill ? 1 : 0);     // Bill LED
  Blynk.virtualWrite(V3 + basePin, (!call && !bill) ? 1 : 0);  // Reset LED
}

// === Kirim Reset ke Sender via MQTT ===
void publishResetToSender(int senderIndex) {
  const char* senderIds[] = {"ESP32_Sender1", "ESP32_Sender2", "ESP32_Sender3"};
  if (senderIndex < 0 || senderIndex > 2) return;

  const char* targetSenderId = senderIds[senderIndex];
  Serial.printf("Mengirim RESET ke %s\n", targetSenderId);

  // Reset call
  {
    StaticJsonDocument<128> doc;
    doc["id"] = "ESP32_Receiver";
    doc["target"] = targetSenderId;
    doc["type"] = "call";
    doc["status"] = false;

    char buffer[128];
    size_t len = serializeJson(doc, buffer);
    String topic = String("waitress/") + targetSenderId + "/call";
    client.publish(topic.c_str(), buffer, len);
    Serial.printf("-> call reset: %s\n", buffer);
  }

  // Reset bill
  {
    StaticJsonDocument<128> doc;
    doc["id"] = "ESP32_Receiver";
    doc["target"] = targetSenderId;
    doc["type"] = "bill";
    doc["status"] = false;

    char buffer[128];
    size_t len = serializeJson(doc, buffer);
    String topic = String("waitress/") + targetSenderId + "/bill";
    client.publish(topic.c_str(), buffer, len);
    Serial.printf("-> bill reset: %s\n", buffer);
  }
}

// === Reset Status Sender dari Blynk ===
BLYNK_WRITE(V10) { resetSender(0); }
BLYNK_WRITE(V11) { resetSender(1); }
BLYNK_WRITE(V12) { resetSender(2); }

void resetSender(int senderIndex) {
  int callIdx = senderIndex * 2;
  int billIdx = senderIndex * 2 + 1;

  statusSender[callIdx] = "OFF";
  statusSender[billIdx] = "OFF";

  setBlynkLED(senderIndex, false, false);
  updateDisplay();

  Serial.printf("Sender %d di-reset oleh Blynk\n", senderIndex + 1);
  publishResetToSender(senderIndex);
}

// === MQTT Callback ===
void callback(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.print("[ERROR] JSON: ");
    Serial.println(error.c_str());
    return;
  }

  String senderId = doc["id"];
  String type = doc["type"];
  bool status = doc["status"];
  unsigned long currentMillis = millis();

  int senderIndex = -1;
  if (senderId == "ESP32_Sender1") senderIndex = 0;
  else if (senderId == "ESP32_Sender2") senderIndex = 1;
  else if (senderId == "ESP32_Sender3") senderIndex = 2;

  if (senderIndex == -1) return;

  int index = (type == "call") ? senderIndex * 2 : senderIndex * 2 + 1;

  if (currentMillis - lastMessageTime[index] > debounceDelay) {
    statusSender[index] = status ? "ON" : "OFF";

    if (status) {
      if (type == "call") statusSender[senderIndex * 2 + 1] = "OFF";
      if (type == "bill") statusSender[senderIndex * 2] = "OFF";
    }

    updateDisplay();

    bool callOn = statusSender[senderIndex * 2] == "ON";
    bool billOn = statusSender[senderIndex * 2 + 1] == "ON";
    setBlynkLED(senderIndex, callOn, billOn);

    if (status) {
      dfPlayer.play(index + 1);
    }

    lastMessageTime[index] = currentMillis;
  }
}

// === Reconnect MQTT ===
void reconnect() {
  while (!client.connected()) {
    Serial.print("[MQTT] Menghubungkan...");
    if (client.connect("ESP32_Receiver")) {
      Serial.println("Tersambung!");
      client.subscribe("waitress/+/call");
      client.subscribe("waitress/+/bill");
    } else {
      Serial.print("Gagal, rc=");
      Serial.print(client.state());
      Serial.println(" coba lagi 5 detik...");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_WIFI, OUTPUT);
  digitalWrite(LED_WIFI, HIGH);

  mySerial.begin(9600, SERIAL_8N1, 16, 17);
  if (!dfPlayer.begin(mySerial)) {
    Serial.println("[DFPlayer] Gagal inisialisasi!");
    while (1);
  }
  dfPlayer.volume(30);

  setupDisplay();

  WiFiManager wm;
  wm.setConfigPortalTimeout(60);
  if (!wm.autoConnect("WaitressReceiver")) {
    Serial.println("Gagal connect WiFi. Restart...");
    ESP.restart();
  }

  Blynk.begin(auth, WiFi.SSID().c_str(), WiFi.psk().c_str());
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  Blynk.run();

  wifiConnected = WiFi.status() == WL_CONNECTED;

  if (wifiConnected && millis() - lastBlink >= 500) {
    lastBlink = millis();
    ledState = !ledState;
    digitalWrite(LED_WIFI, ledState);
  } else {
    digitalWrite(LED_WIFI, HIGH);
  }

  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}
