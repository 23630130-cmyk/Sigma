/*
  OBSERVATORIO DE OCOTLAN

  Laboratorio 1: Calidad del Aire
  Nodo: aire-01

  Componentes:
  - ESP32
  - ENS160
  - AHT21 / AHT2x

  Envia datos mediante MQTT cada 30 segundos.
*/

#define LAB 1  // Laboratorio 1: Aire

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <time.h>
#include "secrets.h"
#include "ScioSense_ENS160.h"

// =====================================
// CONFIGURACION DEL OBSERVATORIO
// =====================================

#define NODO "aire-01"
#define FW "0.2"

const char* BROKER = "192.168.10.188";
const uint16_t PUERTO = 1883;

const uint32_t INTERVALO_MS = 30000;

// =====================================
// CONEXIONES I2C
// =====================================

#define SDA_PIN 21
#define SCL_PIN 22

// =====================================
// OBJETOS
// =====================================

WiFiClient red;
PubSubClient mqtt(red);

ScioSense_ENS160* ens160 = nullptr;

// =====================================
// VARIABLES
// =====================================

char topicLectura[48];
char topicEstado[48];

uint32_t ultimo = 0;

// =====================================
// LECTURA DEL AHT21
// =====================================

bool leerAHT(float &t, float &h) {

  Wire.beginTransmission(0x38);

  Wire.write(0xAC);
  Wire.write(0x33);
  Wire.write(0x00);

  if (Wire.endTransmission() != 0) {
    return false;
  }

  delay(80);

  if (Wire.requestFrom((uint8_t)0x38,
                       (uint8_t)7) != 7) {
    return false;
  }

  uint8_t d[7];

  for (int i = 0; i < 7; i++) {
    d[i] = Wire.read();
  }

  if (d[0] & 0x80) {
    return false;
  }

  uint32_t rh =
    ((uint32_t)d[1] << 12) |
    ((uint32_t)d[2] << 4) |
    (d[3] >> 4);

  uint32_t rt =
    ((uint32_t)(d[3] & 0x0F) << 16) |
    ((uint32_t)d[4] << 8) |
    d[5];

  h = rh * 100.0f / 1048576.0f;

  t = rt * 200.0f / 1048576.0f - 50.0f;

  return true;
}

// =====================================
// INICIALIZAR SENSORES
// =====================================

void iniciarSensores() {

  Wire.begin(SDA_PIN, SCL_PIN);

  Serial.println("Iniciando sensores...");

  uint8_t dir = ENS160_I2CADDR_1;

  Wire.beginTransmission(dir);

  if (Wire.endTransmission() != 0) {
    dir = ENS160_I2CADDR_0;
  }

  ens160 = new ScioSense_ENS160(dir);

  ens160->begin();

  if (ens160->available()) {

    ens160->setMode(ENS160_OPMODE_STD);

    Serial.println("ENS160 detectado correctamente");

  } else {

    Serial.println("ERROR: ENS160 no responde");
    Serial.println("Revisa conexiones I2C");
  }
}

// =====================================
// LEER CALIDAD DEL AIRE
// =====================================

bool leerSensores(char* v, size_t n) {

  float temperatura;
  float humedad;

  if (!leerAHT(temperatura, humedad)) {

    Serial.println("ERROR: AHT21 no responde");
    return false;
  }

  if (!ens160 || !ens160->available()) {

    Serial.println("ERROR: ENS160 no disponible");
    return false;
  }

  // Compensacion de temperatura y humedad
  ens160->set_envdata(temperatura, humedad);

  ens160->measure(true);

  uint16_t eco2 = ens160->geteCO2();
  uint16_t tvoc = ens160->getTVOC();
  uint8_t aqi = ens160->getAQI();

  // Mostrar mediciones en Monitor Serie
  Serial.println();
  Serial.println("====== CALIDAD DEL AIRE ======");

  Serial.print("Temperatura: ");
  Serial.print(temperatura);
  Serial.println(" C");

  Serial.print("Humedad: ");
  Serial.print(humedad);
  Serial.println(" %");

  Serial.print("eCO2 estimado: ");
  Serial.print(eco2);
  Serial.println(" ppm");

  Serial.print("TVOC: ");
  Serial.print(tvoc);
  Serial.println(" ppb");

  Serial.print("AQI: ");
  Serial.println(aqi);

  Serial.println("==============================");

  // Preparar datos JSON
  snprintf(
    v,
    n,
    "\"eco2_ppm\":%u,"
    "\"tvoc_ppb\":%u,"
    "\"aqi\":%u,"
    "\"temp_c\":%.1f,"
    "\"rh_pct\":%.1f",
    eco2,
    tvoc,
    aqi,
    temperatura,
    humedad
  );

  return true;
}

// =====================================
// OBTENER HORA UTC
// =====================================

bool horaUTC(char* out, size_t n) {

  time_t t = time(nullptr);

  if (t < 1700000000) {
    return false;
  }

  struct tm tm;

  gmtime_r(&t, &tm);

  strftime(
    out,
    n,
    "%Y-%m-%dT%H:%M:%SZ",
    &tm
  );

  return true;
}

// =====================================
// CONECTAR WIFI
// =====================================

void conectarWiFi() {

  Serial.print("Wi-Fi: conectando a ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  for (int i = 0;
       i < 30 && WiFi.status() != WL_CONNECTED;
       i++) {

    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("Wi-Fi conectado");

    Serial.print("Direccion IP: ");
    Serial.println(WiFi.localIP());

  } else {

    Serial.println("Wi-Fi: no conecto");
    Serial.println("Revisa nombre y contrasena");
  }
}

// =====================================
// CONECTAR MQTT
// =====================================

void conectarMQTT() {

  if (mqtt.connect(NODO, MQTT_USER, MQTT_PASS)) {

    Serial.println("MQTT: conectado");

  } else {

    Serial.print("MQTT: fallo, rc=");
    Serial.println(mqtt.state());
  }
}

// =====================================
// SETUP
// =====================================

void setup() {

  Serial.begin(115200);

  delay(1500);

  Serial.println();
  Serial.println("============================");
  Serial.println(" OBSERVATORIO DE OCOTLAN");
  Serial.println(" LABORATORIO 1 - AIRE");
  Serial.println(" NODO: aire-01");
  Serial.println("============================");

  snprintf(
    topicLectura,
    sizeof(topicLectura),
    "ito/obs/%s/lectura",
    NODO
  );

  snprintf(
    topicEstado,
    sizeof(topicEstado),
    "ito/obs/%s/estado",
    NODO
  );

  // Inicializar sensores
  iniciarSensores();

  // Configurar MQTT
  mqtt.setServer(BROKER, PUERTO);
  mqtt.setBufferSize(512);

  // Conectar WiFi
  conectarWiFi();

  // Sincronizar hora
  configTime(
    0,
    0,
    "pool.ntp.org",
    "time.google.com"
  );
}

// =====================================
// LOOP PRINCIPAL
// =====================================

void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }

  if (WiFi.status() == WL_CONNECTED &&
      !mqtt.connected()) {

    conectarMQTT();
  }

  mqtt.loop();

  if (millis() - ultimo < INTERVALO_MS) {
    return;
  }

  ultimo = millis();

  char ts[24];
  char v[160];
  char msg[320];

  if (!horaUTC(ts, sizeof(ts))) {

    Serial.println("Hora: esperando NTP");
    return;
  }

  if (!mqtt.connected()) {
    return;
  }

  // Enviar mediciones
  if (leerSensores(v, sizeof(v))) {

    snprintf(
      msg,
      sizeof(msg),
      "{\"ts\":\"%s\","
      "\"nodo\":\"%s\","
      "\"fw\":\"%s\","
      "\"v\":{%s}}",
      ts,
      NODO,
      FW,
      v
    );

    mqtt.publish(topicLectura, msg);

    Serial.println("Lectura enviada al observatorio:");
    Serial.println(msg);
  }

  // Enviar estado del ESP32
  snprintf(
    msg,
    sizeof(msg),
    "{\"ts\":\"%s\","
    "\"nodo\":\"%s\","
    "\"rssi\":%d,"
    "\"uptime_s\":%lu,"
    "\"heap_b\":%lu}",
    ts,
    NODO,
    WiFi.RSSI(),
    millis() / 1000UL,
    (unsigned long)ESP.getFreeHeap()
  );

  mqtt.publish(topicEstado, msg);

  Serial.println("Estado enviado:");
  Serial.println(msg);
}