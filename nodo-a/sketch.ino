// TI3042 - Evaluacion 1 - Nodo A (comedero)
// Sensor: HC-SR04. Actuadores: servo, LED, buzzer.
// Recibe ordenes y publica telemetria por WiFi via MQTT.

#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <WiFiClientSecure.h>
#include <time.h>

// CAMBIAR el prefijo por uno propio. El broker es publico.
#define PREFIJO "inacap/ti3042/bs9c4d1e"
#define T_TEL PREFIJO "/tel"
#define T_CMD PREFIJO "/cmd"
#define T_ACK PREFIJO "/ack"

const char *SSID_WIFI = "Wokwi-GUEST";
const char *PASS_WIFI = "";
const char *BROKER = "a076afc0.ala.us-east-1.emqxsl.com";
const char *MQTT_USER = "nodo-a";
const char *MQTT_PASS = "coloque la contra aqui";

const int PIN_TRIG = 5, PIN_ECHO = 18, PIN_SERVO = 13;
const int PIN_LED = 25, PIN_BUZ = 27;

const int RACION_MS = 600;
const int MAX_RACIONES = 2;
const int NIVEL_MIN = 10;
const unsigned long INTERVALO = 25000;
const unsigned long ESPERA_MIN = 6000;

WiFiClientSecure net;
PubSubClient mqtt(net);
Servo compuerta;

unsigned long seqTx = 0, seqRx = 0;
unsigned long tTel = 0, tAuto = 0, tDisp = 0;
int nivel = 0, raciones = 0;

String campo(String t, int idx) {
  int ini = 0, n = 0;
  for (int i = 0; i <= (int)t.length(); i++) {
    if (i == (int)t.length() || t.charAt(i) == ',') {
      if (n == idx) return t.substring(ini, i);
      n++;
      ini = i + 1;
    }
  }
  return "";
}

int leerNivel() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long dur = pulseIn(PIN_ECHO, HIGH, 30000);
  if (dur == 0) return nivel;
  long cm = dur / 58;
  return constrain(map(cm, 4, 25, 100, 0), 0, 100);
}

// Politicas que ninguna orden remota puede saltarse
String validar() {
  if (raciones >= MAX_RACIONES) return "LIMITE_DIARIO";
  if (millis() - tDisp < ESPERA_MIN) return "LIMITE_TASA";
  if (nivel < NIVEL_MIN) return "TOLVA_VACIA";
  return "";
}

void dispensar(const char *origen) {
  compuerta.write(90);
  delay(RACION_MS);
  compuerta.write(0);
  digitalWrite(PIN_BUZ, HIGH);
  delay(80);
  digitalWrite(PIN_BUZ, LOW);
  raciones++;
  tDisp = millis();
  Serial.printf("[ACT] dispensado origen=%s raciones=%d\n", origen, raciones);
}

void publicar(const char *topic, String msg) {
  mqtt.publish(topic, msg.c_str());
  Serial.printf("[TX ] %s\n", msg.c_str());
}

// Memoria de las ultimas 32 ordenes recibidas.
String clavesOrden[32];
String respuestasOrden[32];
int posicionOrden = 0;

void responderOrden(String clave, String respuesta) {
  clavesOrden[posicionOrden] = clave;
  respuestasOrden[posicionOrden] = respuesta;
  posicionOrden = (posicionOrden + 1) % 32;

  publicar(T_ACK, respuesta);
}

void onMsg(char *topic, byte *payload, unsigned int len) {
  if (String(topic) != T_CMD || len > 160) return;

  String t;
  for (unsigned int i = 0; i < len; i++) {
    t += (char)payload[i];
  }

  String origen = campo(t, 0);
  String numero = campo(t, 1);
  String cmd = campo(t, 2);

  if (origen.length() == 0 || origen.length() > 60) return;
  if (numero.length() == 0 || numero.length() > 10) return;

  for (unsigned int i = 0; i < numero.length(); i++) {
    if (numero.charAt(i) < '0' || numero.charAt(i) > '9') {
      return;
    }
  }

  if (cmd != "DISP" && cmd != "RESET") return;

  String clave = origen + ":" + numero;

  // Si la orden se repite, devuelve su respuesta anterior.
  for (int i = 0; i < 32; i++) {
    if (clavesOrden[i] == clave) {
      publicar(T_ACK, respuestasOrden[i]);
      return;
    }
  }

  Serial.printf("[RX ] %s\n", t.c_str());

  String resultado;

  if (cmd == "RESET") {
    raciones = 0;
    resultado = "OK:RESET";
  } else {
    nivel = leerNivel();
    String err = validar();

    if (err != "") {
      resultado = "REJ:" + err;
    } else {
      dispensar("REMOTO");
      resultado = "OK:DISP";
    }
  }

  String respuesta =
      "A," + numero + "," + resultado + "," + origen;

  responderOrden(clave, respuesta);
}

void conectar() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("[NET] WiFi");
    WiFi.begin(SSID_WIFI, PASS_WIFI, 6);
    while (WiFi.status() != WL_CONNECTED) { delay(200); Serial.print("."); }
    Serial.printf("\n[NET] IP %s\n", WiFi.localIP().toString().c_str());
  }

  // La hora correcta permite comprobar la vigencia del certificado.
if (time(nullptr) < 1700000000) {
  Serial.println("[TLS] Sincronizando hora...");
  configTime(0, 0, "pool.ntp.org", "time.google.com");

  unsigned long inicioHora = millis();

  while (time(nullptr) < 1700000000) {
    delay(200);

    if (millis() - inicioHora > 20000) {
      Serial.println("[TLS] No se pudo sincronizar la hora");
      return;
    }
  }

  Serial.println("[TLS] Hora sincronizada");
}

  while (!mqtt.connected()) {
    Serial.print("[MQTT] conectando... ");
    if (mqtt.connect(
      ("nodoA-" + String(random(9999))).c_str(),
      MQTT_USER,
      MQTT_PASS)) {
      Serial.println("ok");
      mqtt.subscribe(T_CMD);
    } else {
      Serial.println("fallo");
      delay(2000);
    }
  }
  digitalWrite(PIN_LED, HIGH);
}

const char *CA_CERT = R"EOF(-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----)EOF";

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZ, OUTPUT);

  ESP32PWM::allocateTimer(0);
  compuerta.setPeriodHertz(50);
  compuerta.attach(PIN_SERVO, 500, 2400);
  compuerta.write(0);

  Serial.println("\n=== NODO A COMEDERO ===");
  net.setCACert(CA_CERT);
  mqtt.setServer(BROKER, 8883);
  mqtt.setCallback(onMsg);
  conectar();
}

void loop() {
  if (!mqtt.connected()) { digitalWrite(PIN_LED, LOW); conectar(); }
  mqtt.loop();

  nivel = leerNivel();

  // Tarea autonoma: dispensa sin intervencion remota
  if (millis() - tAuto >= INTERVALO) {
    tAuto = millis();
    String err = validar();
    if (err == "") dispensar("AUTONOMO");
    else Serial.printf("[AUTO] omitido: %s\n", err.c_str());
  }

  if (millis() - tTel >= 2000) {
    tTel = millis();
    seqTx++;
    publicar(T_TEL, "A," + String(seqTx) + "," + String(nivel) + "," +
                    String(raciones) + "," + String(MAX_RACIONES));
  }

  delay(50);
}
