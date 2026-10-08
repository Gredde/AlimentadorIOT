// TI3042 - Evaluacion 1 - Nodo B (interior)
// Entradas: 2 pulsadores. Actuadores: LCD 16x2 I2C, LED.
// Recibe telemetria y envia ordenes por WiFi via MQTT.

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiClientSecure.h>
#include <time.h>

// El prefijo debe ser IDENTICO al del nodo A.
#define PREFIJO "inacap/ti3042/bs9c4d1e"
#define T_TEL PREFIJO "/tel"
#define T_CMD PREFIJO "/cmd"
#define T_ACK PREFIJO "/ack"

const char *SSID_WIFI = "Wokwi-GUEST";
const char *PASS_WIFI = "";
const char *BROKER = "a076afc0.ala.us-east-1.emqxsl.com";
const char *MQTT_USER = "nodo-b";
const char *MQTT_PASS = "COLOQUE LA CONTRA AQUI";

const int PIN_DISP = 32, PIN_RESET = 33, PIN_LED = 26;

WiFiClientSecure net;
PubSubClient mqtt(net);
LiquidCrystal_I2C lcd(0x27, 16, 2);

unsigned long seqTx = 0, seqRx = 0;
unsigned long recibidas = 0, perdidas = 0, latencia = 0;
unsigned long tEnvio = 0, tBtn = 0, tStat = 0;
int nivel = 0, raciones = 0, maxRaciones = 0;
String ultimoAck = "-";
String origenB;
bool esperandoAck = false;
unsigned long seqPendiente = 0;

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

void enviar(const char *cmd) {
  if (!mqtt.connected() || esperandoAck) return;

  seqTx++;
  String msg = origenB + "," + String(seqTx) + "," + cmd;

  if (mqtt.publish(T_CMD, msg.c_str())) {
    seqPendiente = seqTx;
    tEnvio = millis();
    esperandoAck = true;
    ultimoAck = "ESPERA";
    Serial.printf("[TX ] %s\n", msg.c_str());
  } else {
    ultimoAck = "ERROR_TX";
    Serial.println("[ERR] No se pudo enviar");
  }
}

void onMsg(char *topic, byte *payload, unsigned int len) {
  String t;
  for (unsigned int i = 0; i < len; i++) {
    t += (char)payload[i];
  }

  if (String(topic) == T_TEL) {
    if (campo(t, 0) != "A") return;

    unsigned long seq = campo(t, 1).toInt();

    // Si A reinicia su secuencia, comienza un nuevo conteo.
    if (seq < seqRx) {
      seqRx = 0;
      recibidas = 0;
      perdidas = 0;
    }

    if (seqRx != 0 && seq > seqRx + 1) {
      perdidas += seq - seqRx - 1;
    }

    seqRx = seq;
    recibidas++;
    nivel = campo(t, 2).toInt();
    raciones = campo(t, 3).toInt();
    maxRaciones = campo(t, 4).toInt();

  } else if (String(topic) == T_ACK) {
    unsigned long seq = campo(t, 1).toInt();

    // Solo acepta la confirmacion de su propia orden.
    if (campo(t, 0) != "A") return;
    if (campo(t, 3) != origenB) return;
    if (!esperandoAck || seq != seqPendiente) return;

    latencia = millis() - tEnvio;
    ultimoAck = campo(t, 2);
    esperandoAck = false;

    Serial.printf("[ACK] %s latencia=%lu ms\n",
                  ultimoAck.c_str(), latencia);
  }
}

void conectar() {
  if (WiFi.status() != WL_CONNECTED) {
    lcd.setCursor(0, 0);
    lcd.print("Conectando WiFi ");
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
      ("nodoB-" + String(random(9999))).c_str(),
      MQTT_USER,
      MQTT_PASS)) {
      Serial.println("ok");
      mqtt.subscribe(T_TEL);
      mqtt.subscribe(T_ACK);
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
  origenB = "B-" + String((uint32_t)ESP.getEfuseMac(), HEX)
          + "-" + String(esp_random(), HEX);
  pinMode(PIN_DISP, INPUT_PULLUP);
  pinMode(PIN_RESET, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);

  lcd.init();
  lcd.backlight();
  lcd.print("Alimentador IoT");

  Serial.println("\n=== NODO B INTERIOR ===");
  net.setCACert(CA_CERT);
  mqtt.setServer(BROKER, 8883);
  mqtt.setCallback(onMsg);
  conectar();
}

void loop() {
  if (!mqtt.connected()) { digitalWrite(PIN_LED, LOW); conectar(); }
  mqtt.loop();

if (esperandoAck && millis() - tEnvio >= 8000) {
  esperandoAck = false;
  ultimoAck = "SIN_ACK";
  Serial.println("[ERR] Sin confirmacion del nodo A");
}

// Exige soltar ambos botones antes de otra pulsacion.
static bool botonesArmados = true;

bool dispPulsado = digitalRead(PIN_DISP) == LOW;
bool resetPulsado = digitalRead(PIN_RESET) == LOW;

if (!dispPulsado && !resetPulsado) {
  botonesArmados = true;
}

if (botonesArmados &&
    millis() - tBtn > 300 &&
    (dispPulsado || resetPulsado)) {

  botonesArmados = false;
  tBtn = millis();

  if (dispPulsado) {
    enviar("DISP");
  } else {
    enviar("RESET");
  }
}

  char l1[17], l2[17];
  snprintf(l1, 17, "Nivel %3d%% R%d/%d", nivel, raciones, maxRaciones);
  snprintf(l2, 17, "P%lu %lums %s", perdidas, latencia, ultimoAck.c_str());
  lcd.setCursor(0, 0); lcd.print("                ");
  lcd.setCursor(0, 0); lcd.print(l1);
  lcd.setCursor(0, 1); lcd.print("                ");
  lcd.setCursor(0, 1); lcd.print(l2);

  if (millis() - tStat >= 5000) {
    tStat = millis();
    unsigned long total = recibidas + perdidas;
    Serial.printf("[STAT] recibidas=%lu perdidas=%lu entrega=%.1f%% latencia=%lu ms\n",
                  recibidas, perdidas, total ? 100.0 * recibidas / total : 0, latencia);
  }

  delay(100);
}
