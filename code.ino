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
  int thresholdOn = 350;
  int thresholdOff = 320;
  int movingAverageSamples = 10;
  int confirmationSeconds = 5;
  int samplingIntervalMs = 1000;
};

Config config;

enum SensorState {
  STATE_OK,
  STATE_PENDING_BAD,
  STATE_BAD,
  STATE_PENDING_OK
};

SensorState sensorState = STATE_OK;

bool malOlor = false;
bool estadoInicializado = false;

bool lastButton = HIGH;
unsigned long buttonDownAt = 0;
bool longPressDone = false;

unsigned long lastRead = 0;
int lastValue = 0;

const int MAX_AVG_SAMPLES = 60;
int samples[MAX_AVG_SAMPLES];
int sampleCount = 0;
int sampleIndex = 0;
long sampleSum = 0;
int movingAverage = 0;
unsigned long pendingSince = 0;

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
  if (millis() - lastRead < (unsigned long)config.samplingIntervalMs) return;
  lastRead = millis();

  lastValue = analogRead(MQ_PIN);
  updateMovingAverage(lastValue);

  Serial.print("MQ135: ");
  Serial.print(lastValue);
  Serial.print(" | AVG: ");
  Serial.print(movingAverage);
  Serial.print(" | State: ");
  Serial.println(sensorStateName());

  if (!estadoInicializado) {
    malOlor = movingAverage > config.thresholdOn;
    sensorState = malOlor ? STATE_BAD : STATE_OK;
    estadoInicializado = true;
    return;
  }

  updateSensorState();
}

void updateMovingAverage(int value) {
  int requestedSamples = constrain(config.movingAverageSamples, 1, MAX_AVG_SAMPLES);

  if (sampleCount != requestedSamples && sampleCount > 0) {
    resetMovingAverage();
  }

  if (sampleCount < requestedSamples) {
    samples[sampleIndex] = value;
    sampleSum += value;
    sampleCount++;
    sampleIndex = (sampleIndex + 1) % requestedSamples;
  } else {
    sampleSum -= samples[sampleIndex];
    samples[sampleIndex] = value;
    sampleSum += value;
    sampleIndex = (sampleIndex + 1) % requestedSamples;
  }

  movingAverage = sampleSum / sampleCount;
}

void resetMovingAverage() {
  sampleCount = 0;
  sampleIndex = 0;
  sampleSum = 0;
  movingAverage = 0;
}

void updateSensorState() {
  unsigned long now = millis();
  unsigned long confirmationMs = (unsigned long)config.confirmationSeconds * 1000UL;

  switch (sensorState) {
    case STATE_OK:
      malOlor = false;
      if (movingAverage > config.thresholdOn) {
        sensorState = STATE_PENDING_BAD;
        pendingSince = now;
      }
      break;

    case STATE_PENDING_BAD:
      if (movingAverage <= config.thresholdOn) {
        sensorState = STATE_OK;
        pendingSince = 0;
      } else if (now - pendingSince >= confirmationMs) {
        sensorState = STATE_BAD;
        malOlor = true;
        pendingSince = 0;
        sendWebhook(renderMessage(config.msgBad, lastValue));
      }
      break;

    case STATE_BAD:
      malOlor = true;
      if (movingAverage < config.thresholdOff) {
        sensorState = STATE_PENDING_OK;
        pendingSince = now;
      }
      break;

    case STATE_PENDING_OK:
      if (movingAverage >= config.thresholdOff) {
        sensorState = STATE_BAD;
        pendingSince = 0;
      } else if (now - pendingSince >= confirmationMs) {
        sensorState = STATE_OK;
        malOlor = false;
        pendingSince = 0;
        sendWebhook(renderMessage(config.msgOk, lastValue));
      }
      break;
  }
}

String sensorStateName() {
  switch (sensorState) {
    case STATE_OK: return "OK";
    case STATE_PENDING_BAD: return "PENDING_BAD";
    case STATE_BAD: return "BAD";
    case STATE_PENDING_OK: return "PENDING_OK";
  }
  return "UNKNOWN";
}

unsigned long pendingSeconds() {
  if (pendingSince == 0) return 0;
  return (millis() - pendingSince) / 1000UL;
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
  msg.replace("[value]", String(value));
  msg.replace("[average]", String(movingAverage));
  msg.replace("[threshold_on]", String(config.thresholdOn));
  msg.replace("[threshold_off]", String(config.thresholdOff));
  msg.replace("[state]", sensorStateName());
  msg.replace("[ip]", WiFi.localIP().toString());
  msg.replace("[hostname]", "wc-sensor");
  msg.replace("[uptime]", String(millis() / 1000UL));
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
  html += "<p><b>Valor actual:</b> " + String(lastValue) + "</p>";
  html += "<p><b>Media movil:</b> " + String(movingAverage) + "</p>";
  html += "<p><b>Umbral ON:</b> " + String(config.thresholdOn) + "</p>";
  html += "<p><b>Umbral OFF:</b> " + String(config.thresholdOff) + "</p>";
  html += "<p><b>Estado:</b> " + sensorStateName() + "</p>";
  html += "<p><b>Tiempo candidato:</b> " + String(pendingSeconds()) + "s</p>";
  html += "<p><b>Muestras media:</b> " + String(config.movingAverageSamples) + "</p>";
  html += "<p><b>Confirmacion:</b> " + String(config.confirmationSeconds) + "s</p>";
  html += "<p><b>Intervalo lectura:</b> " + String(config.samplingIntervalMs) + "ms</p>";
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

  html += "Umbral ON - pasa a mal olor si la media supera este valor:<br>";
  html += "<input name='thresholdOn' type='number' value='" + String(config.thresholdOn) + "'>";

  html += "Umbral OFF - vuelve a OK si la media baja de este valor:<br>";
  html += "<input name='thresholdOff' type='number' value='" + String(config.thresholdOff) + "'>";

  html += "Muestras de media movil - 1 a 60:<br>";
  html += "<input name='movingAverageSamples' type='number' min='1' max='60' value='" + String(config.movingAverageSamples) + "'>";

  html += "Tiempo de confirmacion en segundos:<br>";
  html += "<input name='confirmationSeconds' type='number' min='0' value='" + String(config.confirmationSeconds) + "'>";

  html += "Intervalo de lectura en ms:<br>";
  html += "<input name='samplingIntervalMs' type='number' min='200' value='" + String(config.samplingIntervalMs) + "'>";

  html += "Mensaje mal olor:<br>";
  html += "<textarea name='msgBad'>" + String(config.msgBad) + "</textarea>";

  html += "Mensaje OK:<br>";
  html += "<textarea name='msgOk'>" + String(config.msgOk) + "</textarea>";

  html += "Mensaje test:<br>";
  html += "<textarea name='msgTest'>" + String(config.msgTest) + "</textarea>";

  html += "<p>Variables disponibles: <b>[valor]</b>, <b>[value]</b>, <b>[average]</b>, <b>[threshold_on]</b>, <b>[threshold_off]</b>, <b>[state]</b>, <b>[ip]</b>, <b>[hostname]</b>, <b>[uptime]</b>.</p>";
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
  config.thresholdOn = server.arg("thresholdOn").toInt();
  config.thresholdOff = server.arg("thresholdOff").toInt();
  config.movingAverageSamples = constrain(server.arg("movingAverageSamples").toInt(), 1, MAX_AVG_SAMPLES);
  config.confirmationSeconds = max(0, server.arg("confirmationSeconds").toInt());
  config.samplingIntervalMs = max(200, server.arg("samplingIntervalMs").toInt());

  if (config.thresholdOff >= config.thresholdOn) {
    config.thresholdOff = config.thresholdOn - 1;
  }

  resetMovingAverage();
  estadoInicializado = false;
  pendingSince = 0;

  saveConfig();

  server.sendHeader("Location", "/");
  server.send(303);
}

void handleStatus() {
  StaticJsonDocument<512> doc;

  doc["value"] = lastValue;
  doc["average"] = movingAverage;
  doc["threshold_on"] = config.thresholdOn;
  doc["threshold_off"] = config.thresholdOff;
  doc["moving_average_samples"] = config.movingAverageSamples;
  doc["confirmation_seconds"] = config.confirmationSeconds;
  doc["sampling_interval_ms"] = config.samplingIntervalMs;
  doc["bad_smell"] = malOlor;
  doc["state"] = sensorStateName();
  doc["pending_seconds"] = pendingSeconds();
  doc["endpoint"] = config.endpoint;
  doc["ip"] = WiFi.localIP().toString();
  doc["hostname"] = "wc-sensor";
  doc["uptime"] = millis() / 1000UL;

  String json;
  serializeJson(doc, json);

  server.send(200, "application/json", json);
}

void handleTest() {
  int value = analogRead(MQ_PIN);
  updateMovingAverage(value);
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

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) return;

  strlcpy(config.endpoint, doc["endpoint"] | config.endpoint, sizeof(config.endpoint));
  strlcpy(config.msgBad, doc["msgBad"] | config.msgBad, sizeof(config.msgBad));
  strlcpy(config.msgOk, doc["msgOk"] | config.msgOk, sizeof(config.msgOk));
  strlcpy(config.msgTest, doc["msgTest"] | config.msgTest, sizeof(config.msgTest));
  config.thresholdOn = doc["thresholdOn"] | doc["threshold_on"] | doc["threshold"] | config.thresholdOn;
  config.thresholdOff = doc["thresholdOff"] | doc["threshold_off"] | config.thresholdOff;
  config.movingAverageSamples = constrain((int)(doc["movingAverageSamples"] | doc["moving_average_samples"] | config.movingAverageSamples), 1, MAX_AVG_SAMPLES);
  config.confirmationSeconds = max(0, (int)(doc["confirmationSeconds"] | doc["confirmation_seconds"] | config.confirmationSeconds));
  config.samplingIntervalMs = max(200, (int)(doc["samplingIntervalMs"] | doc["sampling_interval_ms"] | config.samplingIntervalMs));

  if (config.thresholdOff >= config.thresholdOn) {
    config.thresholdOff = config.thresholdOn - 1;
  }
}

void saveConfig() {
  StaticJsonDocument<1024> doc;

  doc["endpoint"] = config.endpoint;
  doc["thresholdOn"] = config.thresholdOn;
  doc["thresholdOff"] = config.thresholdOff;
  doc["movingAverageSamples"] = config.movingAverageSamples;
  doc["confirmationSeconds"] = config.confirmationSeconds;
  doc["samplingIntervalMs"] = config.samplingIntervalMs;
  doc["msgBad"] = config.msgBad;
  doc["msgOk"] = config.msgOk;
  doc["msgTest"] = config.msgTest;

  File file = LittleFS.open("/config.json", "w");
  if (!file) return;

  serializeJson(doc, file);
  file.close();
}