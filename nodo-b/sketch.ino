
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiClientSecure.h>
#include <time.h>

#define PREFIJO "inacap/ti3042/bs9c4d1e"
#define T_TEL PREFIJO "/tel"
#define T_CMD PREFIJO "/cmd"
#define T_ACK PREFIJO "/ack"

const char *SSID_WIFI = "Wokwi-GUEST";
const char *PASS_WIFI = "";
const char *BROKER = "a076afc0.ala.us-east-1.emqxsl.com";
const char *MQTT_USER = "nodo-b";
const char *MQTT_PASS = "CONFIGURAR_EN_LOCAL";

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

const unsigned long RETRY_WIFI_MS = 10000UL;
const unsigned long RETRY_MQTT_MS = 5000UL;
unsigned long ultimoIntentoWiFi = 0;
unsigned long ultimoIntentoMQTT = 0;
bool wifiEstabaConectado = false;
bool horaSolicitada = false;

unsigned long duplicadas = 0;
unsigned long antiguas = 0;
unsigned long ultimoDato = 0;
bool hayTelemetria = false;


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
  if (!mqtt.connected() || esperandoAck) {
    ultimoAck = mqtt.connected() ? "ESPERA" : "SIN_RED";
    Serial.println("[ORDEN] No enviada: desconectado u orden pendiente");
    return;
  }

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

bool esNumero(const String &valor);

void onMsg(char *topic, byte *payload, unsigned int len) {
  if (len == 0 || len > 160) return;

  String t;
  t.reserve(len);
  for (unsigned int i = 0; i < len; i++) {
    t += (char)payload[i];
  }

  String topico = String(topic);

  if (topico == T_TEL) {
    if (campo(t, 0) != "A") return;
    int comas = 0;
    for (unsigned int i = 0; i < t.length(); i++) {
      if (t[i] == ',') comas++;
    }
    if (comas != 4) return;
    String seqTexto = campo(t, 1);
    String nivelTexto = campo(t, 2);
    String racionesTexto = campo(t, 3);
    String maxTexto = campo(t, 4);

    if (!esNumero(seqTexto) || !esNumero(nivelTexto) ||
        !esNumero(racionesTexto) || !esNumero(maxTexto)) return;

    unsigned long seq = strtoul(seqTexto.c_str(), nullptr, 10);
    int nivelNuevo = nivelTexto.toInt();
    int racionesNuevas = racionesTexto.toInt();
    int maxNuevo = maxTexto.toInt();
    if (seq == 0 || nivelNuevo < 0 || nivelNuevo > 100 ||
        maxNuevo <= 0 || racionesNuevas < 0 ||
        racionesNuevas > maxNuevo) return;

    if (hayTelemetria && seq == seqRx) {
      duplicadas++;
      Serial.printf("[TEL] Duplicada, secuencia=%lu\\n", seq);
      return;
    }
    if (hayTelemetria && seq < seqRx) {
      antiguas++;
      Serial.printf("[TEL] Historica, secuencia=%lu (ultima=%lu)\\n",
                    seq, seqRx);
      return;
    }
    if (hayTelemetria && seq > seqRx + 1) {
      perdidas += seq - seqRx - 1;
    }

    seqRx = seq;
    hayTelemetria = true;
    recibidas++;
    nivel = nivelNuevo;
    raciones = racionesNuevas;
    maxRaciones = maxNuevo;
    ultimoDato = millis();

  } else if (topico == T_ACK) {
    if (campo(t, 0) != "A" || campo(t, 3) != origenB) return;
    String seqTexto = campo(t, 1);
    if (!esNumero(seqTexto)) return;
    unsigned long seq = strtoul(seqTexto.c_str(), nullptr, 10);
    if (!esperandoAck || seq != seqPendiente) return;

    latencia = millis() - tEnvio;
    ultimoAck = campo(t, 2);
    esperandoAck = false;
    Serial.printf("[ACK] %s latencia=%lu ms\\n",
                  ultimoAck.c_str(), latencia);
  }
}

bool esNumero(const String &valor) {
  if (valor.length() == 0 || valor.length() > 10) return false;
  for (unsigned int i = 0; i < valor.length(); i++) {
    if (valor[i] < '0' || valor[i] > '9') return false;
  }
  return true;
}

// los intentos tls pueden bloquear brevemente el ciclo principal
void conectar() {
  unsigned long ahora = millis();

  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(PIN_LED, LOW);

    if (wifiEstabaConectado) {
      Serial.println("[NET] WiFi perdido");
      wifiEstabaConectado = false;
      horaSolicitada = false;
    }

    if (ultimoIntentoWiFi == 0 ||
        ahora - ultimoIntentoWiFi >= RETRY_WIFI_MS) {
      ultimoIntentoWiFi = ahora;
      Serial.printf("[NET] Intentando WiFi, estado=%d\\n", WiFi.status());
      WiFi.begin(SSID_WIFI, PASS_WIFI, 6);
    }
    return;
  }

  if (!wifiEstabaConectado) {
    wifiEstabaConectado = true;
    Serial.printf("[NET] IP %s\\n",
                  WiFi.localIP().toString().c_str());
  }

  if (!horaSolicitada) {
    horaSolicitada = true;
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    Serial.println("[TLS] Solicitando hora NTP...");
  }

  if (time(nullptr) < 1700000000) return;

  if (mqtt.connected()) {
    digitalWrite(PIN_LED, HIGH);
    return;
  }

  digitalWrite(PIN_LED, LOW);
  if (ultimoIntentoMQTT != 0 &&
      ahora - ultimoIntentoMQTT < RETRY_MQTT_MS) return;

  ultimoIntentoMQTT = ahora;
  String clientId = "nodoB-" + String(esp_random(), HEX);
  Serial.print("[MQTT] conectando... ");

  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    bool suscritoTel = mqtt.subscribe(T_TEL);
    bool suscritoAck = mqtt.subscribe(T_ACK);
    Serial.printf("ok (tel=%d ack=%d)\\n",
                  suscritoTel, suscritoAck);
    digitalWrite(PIN_LED, HIGH);
  } else {
    Serial.printf("fallo, codigo=%d\\n", mqtt.state());
  }
}

// certificado ca para verificar el broker mqtt
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
  mqtt.setSocketTimeout(3);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(SSID_WIFI, PASS_WIFI, 6);
  ultimoIntentoWiFi = millis();
  Serial.println("[SISTEMA] Nodo B iniciado");

}

void loop() {
  conectar();
  if (mqtt.connected()) {
    mqtt.loop();
  } else if (esperandoAck) {
    esperandoAck = false;
    ultimoAck = "SIN_RED";
    Serial.println("[ORDEN] Conectividad perdida; revisar antes de repetir");
  }


if (esperandoAck && millis() - tEnvio >= 8000) {
  esperandoAck = false;
  ultimoAck = "SIN_ACK";
  Serial.println("[ERR] Sin confirmacion del nodo A");
}

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
  if (!mqtt.connected()) {
    snprintf(l1, 17, "MQTT SIN CONEXION");
  } else if (!hayTelemetria || millis() - ultimoDato > 12000) {
    snprintf(l1, 17, "Esperando Nodo A");
  } else {
    snprintf(l1, 17, "Nivel %3d%% R%d/%d", nivel, raciones, maxRaciones);
  }
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