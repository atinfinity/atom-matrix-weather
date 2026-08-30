/*
 * AtomMatrixWeather
 *
 * M5Stack ATOM Matrix の 5x5 LED に、今日の「午前 (6-12時)」と「午後 (12-18時)」の
 * 天気を色で表示するスケッチ。
 *
 *   - 天気データ : Open-Meteo (https://open-meteo.com/) 都道府県庁所在地の座標で取得
 *   - 表示       : 左2列 = 午前 / 右2列 = 午後 / 中央列 = 区切り(最下段はステータス)
 *   - 設定       : 初回起動時 or ボタン3秒長押しで AP モードに入り、
 *                  スマホ/PC から Wi-Fi と都道府県を設定 (Preferences に保存)
 *   - 更新       : UPDATE_INTERVAL_MS ごとに定期取得。ボタン短押しで即時取得
 *   - 再接続     : Wi-Fi 切断を loop() で監視し、指数バックオフで自動再接続
 *
 * 必要ライブラリ : M5Unified, ArduinoJson (v7), Adafruit NeoPixel
 * ボード         : M5Atom (M5Stack esp32 core 3.x)
 */

#include <M5Unified.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// ------------------------------------------------------------
// ユーザー設定 (必要に応じて変更)
// ------------------------------------------------------------
#define UPDATE_INTERVAL_MS (30UL * 60UL * 1000UL)  // 天気の取得間隔 (30分)
#define LED_BRIGHTNESS 20                          // 0-255 (ATOM の LED はかなり眩しい)
#define ROTATE_180 0                               // 1 にすると表示を180度回転
#define WIFI_CONNECT_TIMEOUT_MS 20000              // Wi-Fi 接続待ちの上限
#define WIFI_RETRY_INITIAL_MS 10000                // 再接続バックオフの初期間隔 (10秒)
#define WIFI_RETRY_MAX_MS (5UL * 60UL * 1000UL)    // 再接続バックオフの上限 (5分)
#define LONG_PRESS_MS 3000                         // 設定モードに入る長押し時間
#define DEFAULT_PREF_INDEX 12                      // 設定不正時のフォールバック (東京都)

// ファームウェアのバージョン。CI がタグ名を -DFW_VERSION で渡す。手元ビルドでは "dev"
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// ------------------------------------------------------------
// ハードウェア定数
// ------------------------------------------------------------
static const int LED_PIN = 27;
static const int LED_COUNT = 25;
static const int STATUS_LED_INDEX = 22;  // 中央列の最下段 (row 4, col 2)

Adafruit_NeoPixel pixels(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;

// ------------------------------------------------------------
// 都道府県テーブル (県庁所在地の座標)
// ------------------------------------------------------------
struct Prefecture {
  const char* name;
  float lat;
  float lon;
};

static const Prefecture PREFECTURES[] = {
    {"北海道", 43.06f, 141.35f},  {"青森県", 40.82f, 140.74f},  {"岩手県", 39.70f, 141.15f},
    {"宮城県", 38.27f, 140.87f},  {"秋田県", 39.72f, 140.10f},  {"山形県", 38.24f, 140.36f},
    {"福島県", 37.75f, 140.47f},  {"茨城県", 36.34f, 140.45f},  {"栃木県", 36.57f, 139.88f},
    {"群馬県", 36.39f, 139.06f},  {"埼玉県", 35.86f, 139.65f},  {"千葉県", 35.60f, 140.12f},
    {"東京都", 35.69f, 139.69f},  {"神奈川県", 35.45f, 139.64f}, {"新潟県", 37.90f, 139.02f},
    {"富山県", 36.70f, 137.21f},  {"石川県", 36.59f, 136.63f},  {"福井県", 36.07f, 136.22f},
    {"山梨県", 35.66f, 138.57f},  {"長野県", 36.65f, 138.18f},  {"岐阜県", 35.39f, 136.72f},
    {"静岡県", 34.98f, 138.38f},  {"愛知県", 35.18f, 136.91f},  {"三重県", 34.73f, 136.51f},
    {"滋賀県", 35.00f, 135.87f},  {"京都府", 35.02f, 135.76f},  {"大阪府", 34.69f, 135.52f},
    {"兵庫県", 34.69f, 135.18f},  {"奈良県", 34.69f, 135.83f},  {"和歌山県", 34.23f, 135.17f},
    {"鳥取県", 35.50f, 134.24f},  {"島根県", 35.47f, 133.05f},  {"岡山県", 34.66f, 133.93f},
    {"広島県", 34.40f, 132.46f},  {"山口県", 34.19f, 131.47f},  {"徳島県", 34.07f, 134.56f},
    {"香川県", 34.34f, 134.04f},  {"愛媛県", 33.84f, 132.77f},  {"高知県", 33.56f, 133.53f},
    {"福岡県", 33.61f, 130.42f},  {"佐賀県", 33.25f, 130.30f},  {"長崎県", 32.74f, 129.87f},
    {"熊本県", 32.79f, 130.74f},  {"大分県", 33.24f, 131.61f},  {"宮崎県", 31.91f, 131.42f},
    {"鹿児島県", 31.56f, 130.56f}, {"沖縄県", 26.21f, 127.68f},
};
static const int PREF_COUNT = sizeof(PREFECTURES) / sizeof(PREFECTURES[0]);

// ------------------------------------------------------------
// 天気分類と色
// ------------------------------------------------------------
enum WeatherKind : uint8_t {
  WX_UNKNOWN = 0,
  WX_CLEAR,   // 晴
  WX_CLOUDY,  // 曇
  WX_RAIN,    // 雨
  WX_SNOW,    // 雪
  WX_THUNDER  // 雷雨
};

static const char* weatherName(WeatherKind k) {
  switch (k) {
    case WX_CLEAR:   return "晴";
    case WX_CLOUDY:  return "曇";
    case WX_RAIN:    return "雨";
    case WX_SNOW:    return "雪";
    case WX_THUNDER: return "雷雨";
    default:         return "不明";
  }
}

static uint32_t weatherColor(WeatherKind k) {
  switch (k) {
    case WX_CLEAR:   return pixels.Color(255, 120, 0);    // オレンジ
    case WX_CLOUDY:  return pixels.Color(120, 120, 120);  // グレー白
    case WX_RAIN:    return pixels.Color(0, 60, 255);     // 青
    case WX_SNOW:    return pixels.Color(255, 255, 255);  // 白
    case WX_THUNDER: return pixels.Color(255, 220, 0);    // 黄
    default:         return 0;
  }
}

// WMO weather code -> 5分類。順序 (enum値) がそのまま「悪さ」の順位になる
static WeatherKind classifyWmo(int code) {
  if (code <= 1) return WX_CLEAR;
  if (code <= 3 || code == 45 || code == 48) return WX_CLOUDY;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return WX_RAIN;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WX_SNOW;
  if (code >= 95 && code <= 99) return WX_THUNDER;
  return WX_CLOUDY;  // 未知のコードは曇扱い
}

// ------------------------------------------------------------
// 状態
// ------------------------------------------------------------
enum StatusKind : uint8_t { ST_NONE, ST_CONNECTING, ST_FETCHING, ST_ERROR };
enum WifiState : uint8_t { WIFI_IDLE, WIFI_CONNECTING, WIFI_CONNECTED, WIFI_WAIT_RETRY };

struct Settings {
  String ssid;
  String pass;
  int prefIndex = DEFAULT_PREF_INDEX;
};

Settings settings;
WeatherKind morning = WX_UNKNOWN;
WeatherKind afternoon = WX_UNKNOWN;
StatusKind status = ST_NONE;
bool configMode = false;
unsigned long lastFetchMs = 0;
bool hasFetched = false;     // 一度でも取得を試みたか (表示の切り替えに使用)
bool lastFetchOk = false;    // 直近の取得が成功したか
bool fetchRequested = true;  // 起動直後に1回取得

WifiState wifiState = WIFI_IDLE;
unsigned long wifiStateSinceMs = 0;
unsigned long wifiRetryDelayMs = WIFI_RETRY_INITIAL_MS;

// ------------------------------------------------------------
// LED ユーティリティ
// ------------------------------------------------------------
static int ledIndex(int row, int col) {
  int idx = row * 5 + col;
#if ROTATE_180
  idx = LED_COUNT - 1 - idx;
#endif
  return idx;
}

static void renderWeather() {
  pixels.clear();
  uint32_t am = weatherColor(morning);
  uint32_t pm = weatherColor(afternoon);
  for (int row = 0; row < 5; row++) {
    pixels.setPixelColor(ledIndex(row, 0), am);
    pixels.setPixelColor(ledIndex(row, 1), am);
    pixels.setPixelColor(ledIndex(row, 3), pm);
    pixels.setPixelColor(ledIndex(row, 4), pm);
  }
  uint32_t st = 0;
  if (status == ST_CONNECTING) st = pixels.Color(0, 200, 0);
  if (status == ST_FETCHING) st = pixels.Color(255, 200, 0);
  if (status == ST_ERROR) st = pixels.Color(255, 0, 0);
  pixels.setPixelColor(ledIndex(4, 2), st);
  pixels.show();
}

// 外周を1ドットずつ回す簡易アニメーション
static void renderSpinner(uint32_t color) {
  static const uint8_t ring[16] = {0, 1, 2, 3, 4, 9, 14, 19, 24, 23, 22, 21, 20, 15, 10, 5};
  static uint8_t pos = 0;
  static unsigned long lastMs = 0;
  if (millis() - lastMs < 60) return;
  lastMs = millis();
  pixels.clear();
  for (int i = 0; i < 4; i++) {
    uint8_t idx = ring[(pos + 16 - i) % 16];
    uint8_t r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    uint8_t f = 4 - i;
    pixels.setPixelColor(idx, pixels.Color(r * f / 4, g * f / 4, b * f / 4));
  }
  pixels.show();
  pos = (pos + 1) % 16;
}

// ------------------------------------------------------------
// 設定の読み書き
// ------------------------------------------------------------
static void loadSettings() {
  prefs.begin("weather", true);
  settings.ssid = prefs.getString("ssid", "");
  settings.pass = prefs.getString("pass", "");
  settings.prefIndex = prefs.getInt("pref", DEFAULT_PREF_INDEX);
  prefs.end();
  if (settings.prefIndex < 0 || settings.prefIndex >= PREF_COUNT) {
    settings.prefIndex = DEFAULT_PREF_INDEX;
  }
}

static void saveSettings() {
  prefs.begin("weather", false);
  prefs.putString("ssid", settings.ssid);
  prefs.putString("pass", settings.pass);
  prefs.putInt("pref", settings.prefIndex);
  prefs.end();
}

// ------------------------------------------------------------
// 天気取得
// ------------------------------------------------------------
// 非ブロッキングの Wi-Fi ステートマシン。loop() から毎回呼ぶ。
static void wifiStartConnect() {
  Serial.printf("[WiFi] connecting to %s\n", settings.ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());
  wifiState = WIFI_CONNECTING;
  wifiStateSinceMs = millis();
}

static void wifiTick() {
  unsigned long now = millis();
  switch (wifiState) {
    case WIFI_IDLE:
      wifiStartConnect();
      break;

    case WIFI_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] connected: %s\n", WiFi.localIP().toString().c_str());
        wifiState = WIFI_CONNECTED;
        wifiRetryDelayMs = WIFI_RETRY_INITIAL_MS;
        // 切断中に取得タイミングを逃した / 直近が失敗なら即取得
        if (hasFetched && (!lastFetchOk || now - lastFetchMs >= UPDATE_INTERVAL_MS)) fetchRequested = true;
      } else if (now - wifiStateSinceMs > WIFI_CONNECT_TIMEOUT_MS) {
        Serial.printf("[WiFi] connect timeout, retry in %lu s\n", wifiRetryDelayMs / 1000UL);
        WiFi.disconnect(false);
        wifiState = WIFI_WAIT_RETRY;
        wifiStateSinceMs = now;
      }
      break;

    case WIFI_CONNECTED:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] disconnected, reconnecting");
        wifiStartConnect();
      }
      break;

    case WIFI_WAIT_RETRY:
      if (now - wifiStateSinceMs >= wifiRetryDelayMs) {
        wifiRetryDelayMs = min(wifiRetryDelayMs * 2, (unsigned long)WIFI_RETRY_MAX_MS);
        wifiStartConnect();
      }
      break;
  }
}

static bool wifiIsConnected() { return wifiState == WIFI_CONNECTED; }

static bool fetchWeather() {
  const Prefecture& p = PREFECTURES[settings.prefIndex];
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(p.lat, 2) +
               "&longitude=" + String(p.lon, 2) +
               "&hourly=weathercode&timezone=Asia%2FTokyo&forecast_days=1";
  Serial.printf("[Fetch] %s: %s\n", p.name, url.c_str());

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(client, url)) {
    Serial.println("[Fetch] http.begin failed");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[Fetch] HTTP error %d\n", code);
    http.end();
    return false;
  }

  // Open-Meteo は Transfer-Encoding: chunked で返す。getStream() の生ストリームには
  // chunk サイズが混ざり deserializeJson が InvalidInput になるため、getString() でデコードしてから解析する
  String body = http.getString();
  http.end();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[Fetch] JSON error: %s\n", err.c_str());
    return false;
  }

  JsonArray codes = doc["hourly"]["weathercode"].as<JsonArray>();
  if (codes.size() < 18) {
    Serial.printf("[Fetch] unexpected hourly length %u\n", (unsigned)codes.size());
    return false;
  }

  WeatherKind am = WX_UNKNOWN, pm = WX_UNKNOWN;
  for (int h = 6; h < 12; h++) {
    WeatherKind k = classifyWmo(codes[h].as<int>());
    if (k > am) am = k;
  }
  for (int h = 12; h < 18; h++) {
    WeatherKind k = classifyWmo(codes[h].as<int>());
    if (k > pm) pm = k;
  }
  morning = am;
  afternoon = pm;
  Serial.printf("[Fetch] 午前=%s 午後=%s\n", weatherName(am), weatherName(pm));
  return true;
}

static void doFetch() {
  status = ST_FETCHING;
  renderWeather();
  lastFetchOk = fetchWeather();
  status = lastFetchOk ? ST_NONE : ST_ERROR;
  lastFetchMs = millis();
  hasFetched = true;
  renderWeather();
}

// 通常モードの表示更新: 天気未取得で接続待ちなら緑回転、それ以外は天気+ステータスドット
static void renderNormal() {
  if (!wifiIsConnected() && morning == WX_UNKNOWN) {
    renderSpinner(pixels.Color(0, 255, 0));
    return;
  }
  StatusKind desired = !wifiIsConnected() ? ST_CONNECTING : (lastFetchOk ? ST_NONE : ST_ERROR);
  if (status != desired) {
    status = desired;
    renderWeather();
  }
}

// ------------------------------------------------------------
// 設定モード (AP + Captive Portal + Web UI)
// ------------------------------------------------------------
static const char HTML_HEAD[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="ja"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AtomWeather 設定</title>
<style>body{font-family:sans-serif;max-width:420px;margin:20px auto;padding:0 12px}
label{display:block;margin-top:14px;font-weight:bold}
input,select{width:100%;padding:8px;font-size:16px;box-sizing:border-box;margin-top:4px}
button{margin-top:20px;width:100%;padding:12px;font-size:16px}</style></head><body>
<h2>AtomWeather 設定</h2>)rawliteral";

static String scannedSsidOptions;

static void scanNetworks() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  int n = WiFi.scanNetworks();
  scannedSsidOptions = "";
  for (int i = 0; i < n && i < 30; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    scannedSsidOptions += "<option value=\"" + s + "\"";
    if (s == settings.ssid) scannedSsidOptions += " selected";
    scannedSsidOptions += ">" + s + " (" + String(WiFi.RSSI(i)) + "dBm)</option>";
  }
  WiFi.scanDelete();
  Serial.printf("[AP] scanned %d networks\n", n);
}

static void handleRoot() {
  String html = FPSTR(HTML_HEAD);
  html += "<form method=\"POST\" action=\"/save\">";
  html += "<label>Wi-Fi (スキャン結果)</label><select name=\"ssid_sel\"><option value=\"\">-- 選択しない --</option>";
  html += scannedSsidOptions;
  html += "</select>";
  html += "<label>Wi-Fi SSID (手入力・こちらが優先)</label><input name=\"ssid\" value=\"" + settings.ssid + "\">";
  // パスワードは HTML に含めない。空欄のまま保存すると (同じ SSID なら) 前回値を維持する
  html += "<label>Wi-Fi パスワード</label><input name=\"pass\" type=\"password\" placeholder=\"";
  html += settings.pass.length() ? "保存済み (変更する場合のみ入力)" : "パスワード";
  html += "\">";
  html += "<label>都道府県</label><select name=\"pref\">";
  for (int i = 0; i < PREF_COUNT; i++) {
    html += "<option value=\"" + String(i) + "\"";
    if (i == settings.prefIndex) html += " selected";
    html += ">" + String(PREFECTURES[i].name) + "</option>";
  }
  html += "</select>";
  html += "<button type=\"submit\">保存して再起動</button></form>";
  html += "<p style=\"color:#888;font-size:12px\">AtomMatrixWeather " FW_VERSION "</p></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

static void handleSave() {
  String ssid = server.arg("ssid");
  ssid.trim();
  if (ssid.length() == 0) ssid = server.arg("ssid_sel");
  if (ssid.length() == 0) {
    server.send(400, "text/html; charset=utf-8",
                String(FPSTR(HTML_HEAD)) + "<p>SSID が空です。<a href=\"/\">戻る</a></p></body></html>");
    return;
  }
  String pass = server.arg("pass");
  bool keptPassword = false;
  if (pass.length() == 0 && ssid == settings.ssid && settings.pass.length() > 0) {
    // 同じ SSID でパスワード空欄 → 前回値を維持。SSID が変わった場合はオープン網として空を保存
    pass = settings.pass;
    keptPassword = true;
  }
  settings.ssid = ssid;
  settings.pass = pass;
  int pref = server.arg("pref").toInt();
  settings.prefIndex = (pref >= 0 && pref < PREF_COUNT) ? pref : DEFAULT_PREF_INDEX;
  saveSettings();
  Serial.printf("[AP] saved ssid=%s pref=%s password=%s\n", settings.ssid.c_str(),
                PREFECTURES[settings.prefIndex].name, keptPassword ? "kept" : "updated");
  String html = String(FPSTR(HTML_HEAD)) + "<p>保存しました。再起動します。</p>";
  if (keptPassword) html += "<p>パスワードは前回の値を維持しました。</p>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
  delay(1000);
  ESP.restart();
}

static void handleNotFound() {
  // Captive Portal: 未知の URL は設定画面へリダイレクト
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

static void startConfigMode() {
  configMode = true;
  scanNetworks();

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char apName[32];
  snprintf(apName, sizeof(apName), "AtomWeather-%02X%02X", mac[4], mac[5]);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName);
  delay(100);
  dnsServer.start(53, "*", WiFi.softAPIP());
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.printf("[AP] config mode: SSID=%s  http://%s/\n", apName, WiFi.softAPIP().toString().c_str());
}

// ------------------------------------------------------------
// setup / loop
// ------------------------------------------------------------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(100);
  Serial.println("\n[Boot] AtomMatrixWeather " FW_VERSION);

  pixels.begin();
  pixels.setBrightness(LED_BRIGHTNESS);
  pixels.clear();
  pixels.show();

  loadSettings();
  Serial.printf("[Boot] ssid=%s pref=%s interval=%lu min\n",
                settings.ssid.length() ? settings.ssid.c_str() : "(none)",
                PREFECTURES[settings.prefIndex].name, UPDATE_INTERVAL_MS / 60000UL);

  // 起動時にボタンを押している、または Wi-Fi 未設定なら設定モード
  M5.update();
  bool btnHeld = M5.BtnA.isPressed();
  if (btnHeld || settings.ssid.length() == 0) {
    Serial.println(btnHeld ? "[Boot] button held -> config mode" : "[Boot] no wifi settings -> config mode");
    startConfigMode();
    return;
  }
}

void loop() {
  M5.update();

  if (configMode) {
    dnsServer.processNextRequest();
    server.handleClient();
    renderSpinner(pixels.Color(0, 80, 255));
    delay(5);
    return;
  }

  // 3秒長押しで設定モードへ
  if (M5.BtnA.pressedFor(LONG_PRESS_MS)) {
    Serial.println("[Btn] long press -> config mode");
    WiFi.disconnect(true);
    startConfigMode();
    return;
  }
  // 短押しで即時取得
  if (M5.BtnA.wasReleased() && !fetchRequested) {
    Serial.println("[Btn] short press -> fetch now");
    fetchRequested = true;
  }

  wifiTick();

  if (!fetchRequested && hasFetched && millis() - lastFetchMs >= UPDATE_INTERVAL_MS) {
    fetchRequested = true;
  }
  if (fetchRequested && wifiIsConnected()) {
    fetchRequested = false;
    doFetch();
  }

  renderNormal();
  delay(20);
}
