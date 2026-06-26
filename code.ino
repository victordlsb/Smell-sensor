#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

const int MQ_PIN = A0;
const int FLASH_BUTTON = D3;

ESP8266WebServer server(80);

struct Config {
  char endpoint[180] = "https://ntfy.sh/quepestequepeste";
  char msgBad[140] = "Mal olor detectado ([valor])";
  char msgOk[140] = "Ya no huele mal ([valor])";
  char msgTest[140] = "Test sensor WC. Valor MQ135: [valor]";
  int threshold = 350;
};

Config config;

bool malOlor = false;
bool estadoInicializado = false;

bool lastButton = HIGH;
unsigned long buttonDownAt = 0;
bool longPressDone = false;

unsigned long lastRead = 0;
int lastValue = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(FLASH_BUTTON, INPUT_PULLUP);

  LittleFS.begin();
  loadConfig();

  WiFiManager wm;
  bool ok = wm.autoConnect("WC-Sensor-Setup");

  if (!ok) {
    delay(3000);
    ESP.restart();
  }

  Serial.println("WiFi conectado");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("wc-sensor")) {
    Serial.println("mDNS activo: http://wc-sensor.local");
  }

  setupWebServer();
  server.begin();

  sendWebhook("Sensor WC iniciado. IP: " + WiFi.localIP().toString());
}

void loop() {
  MDNS.update();
  server.handleClient();
  handleButton();
  handleSensor();
}

void handleSensor() {
  if (millis() - lastRead < 1000) return;
  lastRead = millis();

  lastValue = analogRead(MQ_PIN);

  Serial.print("MQ135: ");
  Serial.println(lastValue);

  bool ahoraMalOlor = lastValue > config.threshold;

  if (!estadoInicializado) {
    malOlor = ahoraMalOlor;
    estadoInicializado = true;
    return;
  }

  if (!malOlor && ahoraMalOlor) {
    malOlor = true;
    sendWebhook(renderMessage(config.msgBad, lastValue));
  }

  if (malOlor && !ahoraMalOlor) {
    malOlor = false;
    sendWebhook(renderMessage(config.msgOk, lastValue));
  }
}

void handleButton() {
  bool button = digitalRead(FLASH_BUTTON);

  if (lastButton == HIGH && button == LOW) {
    buttonDownAt = millis();
    longPressDone = false;
  }

  if (button == LOW && !longPressDone && millis() - buttonDownAt > 5000) {
    longPressDone = true;

    WiFiManager wm;
    wm.resetSettings();
    LittleFS.remove("/config.json");

    delay(1000);
    ESP.restart();
  }

  if (lastButton == LOW && button == HIGH) {
    unsigned long duration = millis() - buttonDownAt;

    if (duration < 5000 && !longPressDone) {
      sendWebhook(renderMessage(config.msgTest, analogRead(MQ_PIN)));
    }
  }

  lastButton = button;
}

String renderMessage(String msg, int value) {
  msg.replace("[valor]", String(value));
  return msg;
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/save", HTTP_POST, handleSaveConfig);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/test", HTTP_GET, handleTest);
  server.on("/reset", HTTP_GET, handleReset);
}

void handleRoot() {
  String html = "";
  html += "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='3'>";
  html += "<style>body{font-family:Arial;margin:24px;}input,textarea{width:100%;padding:8px;margin:6px 0;}button{padding:10px 14px;margin:6px 0;}</style>";
  html += "</head><body>";
  html += "<h2>Sensor WC</h2>";
  html += "<p><b>Valor:</b> " + String(lastValue) + "</p>";
  html += "<p><b>Umbral:</b> " + String(config.threshold) + "</p>";
  html += "<p><b>Estado:</b> " + String(malOlor ? "MAL OLOR" : "OK") + "</p>";
  html += "<p><a href='/config'><button>Configurar</button></a></p>";
  html += "<p><a href='/test'><button>Enviar test</button></a></p>";
  html += "<p><a href='/status'>Ver JSON</a></p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleConfigPage() {
  String html = "";
  html += "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:24px;}input,textarea{width:100%;padding:8px;margin:6px 0;}button{padding:10px 14px;margin-top:10px;}</style>";
  html += "</head><body>";
  html += "<h2>Configurar Sensor WC</h2>";
  html += "<form method='POST' action='/save'>";

  html += "Endpoint:<br>";
  html += "<input name='endpoint' value='" + String(config.endpoint) + "'>";

  html += "Umbral:<br>";
  html += "<input name='threshold' type='number' value='" + String(config.threshold) + "'>";

  html += "Mensaje mal olor:<br>";
  html += "<textarea name='msgBad'>" + String(config.msgBad) + "</textarea>";

  html += "Mensaje OK:<br>";
  html += "<textarea name='msgOk'>" + String(config.msgOk) + "</textarea>";

  html += "Mensaje test:<br>";
  html += "<textarea name='msgTest'>" + String(config.msgTest) + "</textarea>";

  html += "<p>Usa <b>[valor]</b> para insertar el valor del sensor.</p>";
  html += "<button type='submit'>Guardar</button>";
  html += "</form>";
  html += "<p><a href='/'>Volver</a></p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleSaveConfig() {
  strlcpy(config.endpoint, server.arg("endpoint").c_str(), sizeof(config.endpoint));
  strlcpy(config.msgBad, server.arg("msgBad").c_str(), sizeof(config.msgBad));
  strlcpy(config.msgOk, server.arg("msgOk").c_str(), sizeof(config.msgOk));
  strlcpy(config.msgTest, server.arg("msgTest").c_str(), sizeof(config.msgTest));
  config.threshold = server.arg("threshold").toInt();

  saveConfig();

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleStatus() {
  StaticJsonDocument<256> doc;

  doc["value"] = lastValue;
  doc["threshold"] = config.threshold;
  doc["bad_smell"] = malOlor;
  doc["endpoint"] = config.endpoint;
  doc["ip"] = WiFi.localIP().toString();

  String json;
  serializeJson(doc, json);

  server.send(200, "application/json", json);
}

void handleTest() {
  int value = analogRead(MQ_PIN);
  sendWebhook(renderMessage(config.msgTest, value));

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleReset() {
  WiFiManager wm;
  wm.resetSettings();
  LittleFS.remove("/config.json");

  server.send(200, "text/plain", "Reset hecho. Reiniciando...");
  delay(1000);
  ESP.restart();
}

void sendWebhook(String message) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;

  bool isHttps = String(config.endpoint).startsWith("https://");

  WiFiClient client;
  WiFiClientSecure secureClient;

  if (isHttps) {
    secureClient.setInsecure();
    http.begin(secureClient, config.endpoint);
  } else {
    http.begin(client, config.endpoint);
  }

  http.addHeader("Content-Type", "text/plain");
  http.addHeader("Title", "Sensor WC");

  Serial.println("Enviando: " + message);
  Serial.println("Endpoint: " + String(config.endpoint));

  int code = http.POST(message);

  Serial.print("HTTP code: ");
  Serial.println(code);
  Serial.println(http.getString());

  http.end();
}

void loadConfig() {
  if (!LittleFS.exists("/config.json")) return;

  File file = LittleFS.open("/config.json", "r");
  if (!file) return;

  StaticJsonDocument<700> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) return;

  strlcpy(config.endpoint, doc["endpoint"] | config.endpoint, sizeof(config.endpoint));
  strlcpy(config.msgBad, doc["msgBad"] | config.msgBad, sizeof(config.msgBad));
  strlcpy(config.msgOk, doc["msgOk"] | config.msgOk, sizeof(config.msgOk));
  strlcpy(config.msgTest, doc["msgTest"] | config.msgTest, sizeof(config.msgTest));
  config.threshold = doc["threshold"] | config.threshold;
}

void saveConfig() {
  StaticJsonDocument<700> doc;

  doc["endpoint"] = config.endpoint;
  doc["threshold"] = config.threshold;
  doc["msgBad"] = config.msgBad;
  doc["msgOk"] = config.msgOk;
  doc["msgTest"] = config.msgTest;

  File file = LittleFS.open("/config.json", "w");
  if (!file) return;

  serializeJson(doc, file);
  file.close();
}