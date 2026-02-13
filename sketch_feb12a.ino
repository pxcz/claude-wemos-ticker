#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <time.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// ---- OLED pins (SPI) ----
static const uint8_t OLED_DC  = D2;  // GPIO4
static const uint8_t OLED_RST = D1;  // GPIO5
static const uint8_t OLED_CS  = D0;  // GPIO16 (change to D8 if needed)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_DC, OLED_RST, OLED_CS);

// ---- WiFi ----
const char* WIFI_SSID = "SSID";
const char* WIFI_PASS = "PASS";

// ---- mDNS host ----
const char* MDNS_NAME = "claude-ticker-001"; // => http://claude-ticker-px.local/

ESP8266WebServer server(80);

// ---- NTP ----
const char* NTP_SERVER = "pool.ntp.org";

// State
bool hasFirstJson = false;
bool ntpSynced = false;

// Last received values
float fivePct = 0.0;
float sevenPct = 0.0;
time_t fiveResetsAt = 0;
time_t sevenResetsAt = 0;

String getCurrentTimeStr()
{
  time_t now = time(nullptr);
  if (now < 100000) return String();
  struct tm* t = localtime(&now);
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
  return String(buf);
}

void syncNtp()
{
  configTime(0, 0, NTP_SERVER);
  Serial.print("NTP sync...");

  int attempts = 0;
  while (time(nullptr) < 100000 && attempts < 20) {
    delay(500);
    attempts++;
  }

  if (time(nullptr) >= 100000) {
    ntpSynced = true;
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    Serial.printf(" OK: %02d:%02d:%02d\n", t->tm_hour, t->tm_min, t->tm_sec);
  } else {
    Serial.println(" FAILED");
  }
}

// Parse ISO 8601 UTC string "2025-02-13T12:34:56Z" to time_t
time_t parseIsoUtc(const char* iso)
{
  if (!iso || strlen(iso) < 16) return 0;
  struct tm tm = {};
  // YYYY-MM-DDTHH:MM:SS
  tm.tm_year = atoi(iso) - 1900;
  tm.tm_mon  = atoi(iso + 5) - 1;
  tm.tm_mday = atoi(iso + 8);
  tm.tm_hour = atoi(iso + 11);
  tm.tm_min  = atoi(iso + 14);
  if (strlen(iso) >= 19) tm.tm_sec = atoi(iso + 17);
  return mktime(&tm);
}

// Format remaining time from now until target
String formatRemaining(time_t target)
{
  if (target == 0 || !ntpSynced) return F("--");
  time_t now = time(nullptr);
  long diff = (long)(target - now);
  if (diff <= 0) return F("0m");

  long days = diff / 86400;
  long hours = (diff % 86400) / 3600;
  long mins = (diff % 3600) / 60;

  char buf[16];
  if (days > 0) {
    snprintf(buf, sizeof(buf), "%ldd %ldh", days, hours);
  } else {
    snprintf(buf, sizeof(buf), "%ldh %ldm", hours, mins);
  }
  return String(buf);
}

float clampPct(float v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

void drawProgressBarFullWidth(int x, int y, int w, int h, float pct)
{
  pct = clampPct(pct);
  display.drawRect(x, y, w, h, SSD1306_WHITE);

  int fillW = (int)((w - 2) * (pct / 100.0f));
  if (fillW < 0) fillW = 0;
  if (fillW > (w - 2)) fillW = w - 2;

  display.fillRect(x + 1, y + 1, fillW, h - 2, SSD1306_WHITE);
}

void drawLineRemainingAndPct(int y, time_t resetsAt, float pct)
{
  pct = clampPct(pct);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, y);
  display.print(formatRemaining(resetsAt));

  char pctBuf[6];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", (int)lroundf(pct));

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(pctBuf, 0, y, &x1, &y1, &w, &h);

  int rightX = SCREEN_WIDTH - (int)w;
  if (rightX < 0) rightX = 0;

  display.setCursor(rightX, y);
  display.print(pctBuf);
}

void renderGraphs()
{
  display.clearDisplay();

  drawProgressBarFullWidth(0, 0, 128, 16, fivePct);
  drawLineRemainingAndPct(18, fiveResetsAt, fivePct);

  drawProgressBarFullWidth(0, 32, 128, 16, sevenPct);
  drawLineRemainingAndPct(52, sevenResetsAt, sevenPct);

  display.display();
}

void renderConnecting(uint32_t dots)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print(F("Connecting WiFi"));
  for (uint32_t i = 0; i < (dots % 4); i++) display.print('.');

  display.setCursor(0, 18);
  display.print(F("SSID: "));
  display.print(WIFI_SSID);

  display.setCursor(0, 30);
  display.print(F("Status: "));
  display.print((int)WiFi.status());

  display.display();
}

void renderIpAndMdnsScreen(const IPAddress& ip, bool mdnsOk)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println(F("WiFi connected"));

  display.setCursor(0, 18);
  display.print(F("IP: "));
  display.println(ip);

  display.setCursor(0, 32);
  if (mdnsOk) {
    display.print(F("mDNS: "));
    display.print(MDNS_NAME);
    display.println(F(".local"));
  } else {
    display.println(F("mDNS failed"));
  }

  display.setCursor(0, 50);
  display.print(F("POST /update"));

  if (ntpSynced) {
    String ts = getCurrentTimeStr();
    int16_t x1, y1;
    uint16_t tw, th;
    display.getTextBounds(ts.c_str(), 0, 0, &x1, &y1, &tw, &th);
    display.setCursor(SCREEN_WIDTH - tw, 50);
    display.print(ts);
  }

  display.display();
}

void handleHealth()
{
  server.send(200, "text/plain", "OK\n");
}

void handleUpdate()
{
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing body\n");
    return;
  }

  String body = server.arg("plain");
  if (body.length() > 2000) {
    server.send(413, "text/plain", "Payload too large\n");
    return;
  }

  StaticJsonDocument<768> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server.send(400, "text/plain", String("Bad JSON: ") + err.c_str() + "\n");
    return;
  }

  fivePct  = doc["five_hour"]["utilization"] | 0.0;
  sevenPct = doc["seven_day"]["utilization"] | 0.0;

  const char* fts = doc["five_hour"]["resets_at"] | (const char*)nullptr;
  const char* sts = doc["seven_day"]["resets_at"] | (const char*)nullptr;

  fiveResetsAt  = fts ? parseIsoUtc(fts) : 0;
  sevenResetsAt = sts ? parseIsoUtc(sts) : 0;

  hasFirstJson = true;
  renderGraphs();

  server.send(200, "application/json", "{\"status\":\"ok\"}\n");
}

void setup()
{
  delay(200);
  Serial.begin(115200);

  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    while (true) delay(1000);
  }

  renderConnecting(0);

  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    renderConnecting(dots++);
    delay(350);
    yield();
  }

  IPAddress ip = WiFi.localIP();
  Serial.print("WiFi connected, IP: ");
  Serial.println(ip);

  // NTP time sync
  syncNtp();

  // mDNS
  bool mdnsOk = MDNS.begin(MDNS_NAME);
  if (mdnsOk) {
    // Advertise HTTP service
    MDNS.addService("http", "tcp", 80);
    Serial.print("mDNS started: http://");
    Serial.print(MDNS_NAME);
    Serial.println(".local/");
  } else {
    Serial.println("mDNS start FAILED");
  }

  // Show IP + mDNS until first JSON arrives
  renderIpAndMdnsScreen(ip, mdnsOk);

  server.on("/health", HTTP_GET, handleHealth);
  server.on("/update", HTTP_POST, handleUpdate);
  server.begin();
}

void loop()
{
  // WiFi reconnect check
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint32_t dots = 0;
    while (WiFi.status() != WL_CONNECTED) {
      renderConnecting(dots++);
      delay(350);
      yield();
      // Safety: po 30s restart (aby se nezacyklil)
      if (dots > 85) {
        Serial.println("WiFi reconnect timeout, restarting...");
        ESP.restart();
      }
    }

    Serial.print("WiFi reconnected, IP: ");
    Serial.println(WiFi.localIP());
    MDNS.begin(MDNS_NAME);
    MDNS.addService("http", "tcp", 80);

    if (hasFirstJson) {
      renderGraphs();
    } else {
      renderIpAndMdnsScreen(WiFi.localIP(), true);
    }
  }

  server.handleClient();
  MDNS.update();

  // Periodic display refresh
  static uint32_t last = 0;
  if (millis() - last > 30000) {  // every 30s
    last = millis();
    if (hasFirstJson) {
      renderGraphs();  // update remaining time
    } else {
      renderIpAndMdnsScreen(WiFi.localIP(), true);
    }
  }
}