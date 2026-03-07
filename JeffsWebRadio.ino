// ============================================================
//  JEFF'S WEB RADIO — v2.2
//  Pour M5Stack Cardputer ADV (ESP32-S3 / Stamp-S3A)
//
//  CALLBACKS : fonctions globales audio_info, audio_id3data,
//  audio_showstreamtitle, audio_eof_stream reconnues par la lib.
//
//  AUDIO : ESP32-audioI2S (schreibfaul1)
//  Pins Cardputer ADV : BCLK=41, LRC=43, DOUT=42
//  setPinout(BCLK, LRC, DOUT)
//
//  LIBRAIRIES CI :
//    arduino-cli lib install "M5Cardputer"
//    arduino-cli lib install "WiFiManager"
//    arduino-cli lib install "ArduinoJson"
//    git clone https://github.com/schreibfaul1/ESP32-audioI2S
//
//  PARTITION : huge_app (3MB No OTA)
//
//  CONTROLES :
//    ENTER  → Play / Stop
//    N / P  → Station suivante / précédente
//    + / =  → Volume +1  (0-21)
//    -      → Volume -1
//    M      → Mute / Unmute
//    R      → Refresh metadata
//    S      → Stop forcé
//    ?      → Aide
// ============================================================

#include <M5Cardputer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "Audio.h"

// ============================================================
//  PINS I2S — Cardputer ADV (Stamp-S3A)
// ============================================================
#define I2S_BCLK  41
#define I2S_LRC   43
#define I2S_DOUT  42

Audio audio;

// ============================================================
//  STATIONS
// ============================================================
struct Station {
    const char* name;
    const char* url;
    const char* metaUrl;
};

Station STATIONS[] = {
    { "FIP",             "http://icecast.radiofrance.fr/fip-midfi.mp3",               "https://api.radiofrance.fr/livemeta/pull/7" },
    { "RP Main Mix",     "http://stream.radioparadise.com/mp3-128",                   "https://api.radioparadise.com/api/now_playing?block_id=0&format=json" },
    { "RP Rock Mix",     "http://stream.radioparadise.com/rock-128",                  "https://api.radioparadise.com/api/now_playing?block_id=2&format=json" },
    { "SomaFM Groove",   "http://ice.somafm.com/groovesalad-128-mp3",                 "" },
    { "SomaFM Space",    "http://ice.somafm.com/spacestation-128-mp3",                "" },
    { "SomaFM 80s",      "http://ice.somafm.com/u80s-128-mp3",                        "" },
    { "Nightride FM",    "https://stream.nightride.fm/nightride.mp3",                 "" },
    { "Lofi Hip Hop",    "http://stream.zeno.fm/f3wvbbqmdg8uv",                       "" },
    { "BluesWave",       "http://blueswave.radio:8000/blueswave",                     "" },
    { "Blues Radio",     "http://198.58.98.83/stream/1/",                             "" },
    { "101 Smooth Jazz", "http://strm112.1.fm/smoothjazz_mobile_mp3",                 "" },
    { "Chillout Lounge", "http://strm112.1.fm/chilloutlounge_mobile_mp3",             "" },
    { "Sensual Lounge",  "http://agnes.torontocast.com:8146/stream",                  "" },
};
const int NUM_STATIONS = sizeof(STATIONS) / sizeof(STATIONS[0]);

// ============================================================
//  ÉTAT
// ============================================================
int   currentStation = 0;
int   listScrollTop  = 0;
bool  isPlaying      = false;
int   volume         = 12;
bool  isMuted        = false;
bool  showHelp       = false;

String nowPlaying = "";
String nowArtist  = "";
String statusMsg  = "Appuie ENTER pour jouer";

unsigned long lastMetaFetch  = 0;
const long    META_INTERVAL  = 15000;
int           tickerOffset   = 0;
unsigned long lastTickerMove = 0;

// ============================================================
//  COULEURS
// ============================================================
#define COL_BG       M5.Lcd.color565(8,   8,  18)
#define COL_HDR_BG   M5.Lcd.color565(0,   40,  80)
#define COL_HDR_FG   M5.Lcd.color565(0,  180, 255)
#define COL_SEL_BG   M5.Lcd.color565(20,  50,  90)
#define COL_SEL_FG   M5.Lcd.color565(255, 200,   0)
#define COL_TEXT     M5.Lcd.color565(220, 220, 220)
#define COL_DIM      M5.Lcd.color565(80,  80,  80)
#define COL_PLAY     M5.Lcd.color565(0,  255, 120)
#define COL_STOP     M5.Lcd.color565(255,  60,  60)
#define COL_META_BG  M5.Lcd.color565(5,   25,  45)
#define COL_META_FG  M5.Lcd.color565(180, 230, 255)
#define COL_SCROLL   M5.Lcd.color565(60,  60, 100)
#define COL_BAR_BG   M5.Lcd.color565(12,  12,  28)
#define COL_VOL_BG   M5.Lcd.color565(0,   30,  60)
#define COL_VOL_FG   M5.Lcd.color565(0,  210, 100)
#define COL_KEY_BG   M5.Lcd.color565(0,   50, 105)
#define COL_KEY_FG   M5.Lcd.color565(255, 200,   0)
#define COL_HINT_BG  M5.Lcd.color565(10,  10,  25)
#define COL_HELP_BG  M5.Lcd.color565(0,   15,  35)

// ============================================================
//  LAYOUT 240 × 135
// ============================================================
#define W             240
#define H             135
#define HDR_Y           0
#define HDR_H          16
#define META_Y         16
#define META_H         16
#define VOL_Y          32
#define VOL_H          12
#define LIST_Y         44
#define HINT_H         13
#define HINT_Y        (H - HINT_H)
#define LIST_H        (HINT_Y - LIST_Y)
#define LIST_ITEM_H    13
#define VISIBLE_ITEMS (LIST_H / LIST_ITEM_H)

void drawUI();
void drawMetaBand();
void startStream();
void fetchMetadata();

// ============================================================
//  TRAITEMENT ICY / ID3 — commun aux deux styles de callback
// ============================================================
void handleStreamTitle(const char* info) {
    String s(info);
    // ICY StreamTitle
    if (s.startsWith("StreamTitle:")) {
        String title = s.substring(12);
        title.trim();
        title.replace("'", "");
        int sep = title.indexOf(" - ");
        if (sep > 0) {
            nowArtist  = title.substring(0, sep);
            nowPlaying = title.substring(sep + 3);
        } else {
            nowArtist  = "";
            nowPlaying = title.length() > 0 ? title : String(STATIONS[currentStation].name);
        }
        tickerOffset = 0;
        drawMetaBand();
    }
}

// ============================================================
//  CALLBACKS ESP32-audioI2S v3.2.1 — fonctions globales
// ============================================================
void audio_info(const char* info) { }

void audio_showstreamtitle(const char* info) {
    if (!info || strlen(info) == 0) return;
    String title(info);
    int sep = title.indexOf(" - ");
    if (sep > 0) {
        nowArtist  = title.substring(0, sep);
        nowPlaying = title.substring(sep + 3);
    } else {
        nowArtist  = "";
        nowPlaying = title;
    }
    tickerOffset = 0;
    drawMetaBand();
}

void audio_showstation(const char* info) { }

void audio_id3data(const char* info) {
    handleStreamTitle(info);
}

void audio_eof_stream(const char* info) {
    if (isPlaying) {
        statusMsg = "Reconnexion...";
        drawMetaBand();
        delay(2000);
        startStream();
    }
}

// ============================================================
//  VOLUME
// ============================================================
void applyVolume() {
    audio.setVolume(isMuted ? 0 : volume);
}

// ============================================================
//  AUDIO
// ============================================================
void stopStream() {
    audio.stopSong();
    isPlaying    = false;
    nowPlaying   = "";
    nowArtist    = "";
    tickerOffset = 0;
    statusMsg    = "Arrete";
}

void startStream() {
    if (isPlaying) audio.stopSong();
    isPlaying = false;
    statusMsg = "Connexion...";
    drawUI();

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(isMuted ? 0 : volume);

    if (audio.connecttohost(STATIONS[currentStation].url)) {
        isPlaying    = true;
        statusMsg    = "En lecture";
        nowPlaying   = STATIONS[currentStation].name;
        nowArtist    = "";
        tickerOffset = 0;
        fetchMetadata();
    } else {
        statusMsg = "Erreur connexion!";
    }
    drawUI();
}

// ============================================================
//  METADATA JSON (FIP + Radio Paradise)
// ============================================================
void fetchMetadata() {
    if (strlen(STATIONS[currentStation].metaUrl) == 0) return;
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    http.begin(STATIONS[currentStation].metaUrl);
    http.addHeader("User-Agent", "JeffsWebRadio/2.2");
    http.setTimeout(4000);
    if (http.GET() == 200) {
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString())) {
            String sn = String(STATIONS[currentStation].name);
            if (currentStation == 0) {  // FIP
                JsonObject now = doc["now"];
                nowPlaying = now["firstLine"]["title"]  | sn;
                nowArtist  = now["secondLine"]["title"] | String("");
            } else {  // Radio Paradise
                nowPlaying = doc["title"]  | sn;
                nowArtist  = doc["artist"] | String("");
            }
            tickerOffset = 0;
        }
    }
    http.end();
    lastMetaFetch = millis();
}

// ============================================================
//  DESSIN UI
// ============================================================
void drawHeader() {
    M5.Lcd.fillRect(0, HDR_Y, W, HDR_H, COL_HDR_BG);
    M5.Lcd.setTextFont(1); M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COL_HDR_FG, COL_HDR_BG);
    M5.Lcd.drawString(">> JEFF'S WEB RADIO v2.2", 4, HDR_Y + 4);
    bool wok = (WiFi.status() == WL_CONNECTED);
    M5.Lcd.setTextColor(wok ? COL_PLAY : COL_STOP, COL_HDR_BG);
    M5.Lcd.drawString(wok ? "WiFi" : "NoWi", W - 30, HDR_Y + 4);
}

void drawMetaBand() {
    M5.Lcd.fillRect(0, META_Y, W, META_H, COL_META_BG);
    M5.Lcd.setTextFont(1); M5.Lcd.setTextSize(1);
    String full = isPlaying
        ? (nowArtist.length() > 0
            ? nowArtist + " - " + nowPlaying
            : (nowPlaying.length() > 0 ? nowPlaying : String(STATIONS[currentStation].name)))
        : statusMsg;
    const int MAX_VIS = 30;
    String display = full;
    if ((int)full.length() > MAX_VIS) {
        String loop = full + "   " + full;
        if (tickerOffset >= (int)(full.length() + 3)) tickerOffset = 0;
        display = loop.substring(tickerOffset, tickerOffset + MAX_VIS);
    }
    if (isPlaying) {
        M5.Lcd.setTextColor(COL_PLAY, COL_META_BG);
        M5.Lcd.drawString((millis() / 500) % 2 == 0 ? ">" : " ", 3, META_Y + 4);
        M5.Lcd.setTextColor(COL_META_FG, COL_META_BG);
    } else {
        M5.Lcd.setTextColor(COL_DIM, COL_META_BG);
    }
    M5.Lcd.drawString(display, 14, META_Y + 4);
}

void drawVolBar() {
    M5.Lcd.fillRect(0, VOL_Y, W, VOL_H, COL_VOL_BG);
    M5.Lcd.setTextFont(1); M5.Lcd.setTextSize(1);
    if (isMuted) {
        M5.Lcd.setTextColor(COL_STOP, COL_VOL_BG);
        M5.Lcd.drawString("MUTE", 4, VOL_Y + 2);
        M5.Lcd.setTextColor(COL_DIM, COL_VOL_BG);
        M5.Lcd.drawString("  [M] pour reactiver", 30, VOL_Y + 2);
    } else {
        M5.Lcd.setTextColor(COL_VOL_FG, COL_VOL_BG);
        M5.Lcd.drawString("Vol", 3, VOL_Y + 2);
        const int BX = 24, BW = 162, BH = 8, BY = VOL_Y + 2;
        int filled = (volume * BW) / 21;
        M5.Lcd.fillRect(BX, BY, BW, BH, COL_BAR_BG);
        if (filled > 0) M5.Lcd.fillRect(BX, BY, filled, BH, COL_VOL_FG);
        M5.Lcd.setTextColor(COL_TEXT, COL_VOL_BG);
        M5.Lcd.drawString(String(volume) + "/21", 191, VOL_Y + 2);
    }
}

void drawStationList() {
    if (currentStation < listScrollTop) listScrollTop = currentStation;
    if (currentStation >= listScrollTop + VISIBLE_ITEMS)
        listScrollTop = currentStation - VISIBLE_ITEMS + 1;
    M5.Lcd.fillRect(0, LIST_Y, W, LIST_H, COL_BG);
    for (int i = 0; i < VISIBLE_ITEMS; i++) {
        int idx = listScrollTop + i;
        if (idx >= NUM_STATIONS) break;
        int y = LIST_Y + i * LIST_ITEM_H;
        bool sel = (idx == currentStation);
        if (sel) {
            M5.Lcd.fillRect(0, y, W - 4, LIST_ITEM_H, COL_SEL_BG);
            M5.Lcd.fillRect(0, y, 3, LIST_ITEM_H, COL_SEL_FG);
            M5.Lcd.setTextColor(COL_SEL_FG, COL_SEL_BG);
        } else {
            M5.Lcd.setTextColor(COL_DIM, COL_BG);
        }
        M5.Lcd.setTextFont(1); M5.Lcd.setTextSize(1);
        M5.Lcd.drawString(String(idx + 1) + ". " + STATIONS[idx].name, 7, y + 2);
        if (sel && isPlaying) {
            M5.Lcd.fillRect(W - 50, y + 1, 46, LIST_ITEM_H - 2, COL_PLAY);
            M5.Lcd.setTextColor(COL_BG, COL_PLAY);
            M5.Lcd.drawString("ON AIR", W - 46, y + 2);
        }
    }
    if (NUM_STATIONS > VISIBLE_ITEMS) {
        int bH = max(4, LIST_H * VISIBLE_ITEMS / NUM_STATIONS);
        int bY = LIST_Y + (LIST_H - bH) * listScrollTop / max(1, NUM_STATIONS - VISIBLE_ITEMS);
        M5.Lcd.fillRect(W - 3, LIST_Y, 3, LIST_H, COL_BAR_BG);
        M5.Lcd.fillRect(W - 3, bY, 3, bH, COL_SCROLL);
    }
}

static void hKey(int& x, const char* k) {
    int w = strlen(k) * 6 + 4;
    M5.Lcd.fillRect(x, HINT_Y + 1, w, HINT_H - 2, COL_KEY_BG);
    M5.Lcd.setTextColor(COL_KEY_FG, COL_KEY_BG);
    M5.Lcd.drawString(k, x + 2, HINT_Y + 3);
    x += w + 2;
}
static void hLbl(int& x, const char* t) {
    M5.Lcd.setTextColor(COL_DIM, COL_HINT_BG);
    M5.Lcd.drawString(t, x, HINT_Y + 3);
    x += strlen(t) * 6;
}

void drawHintBar() {
    M5.Lcd.fillRect(0, HINT_Y, W, HINT_H, COL_HINT_BG);
    M5.Lcd.setTextFont(1); M5.Lcd.setTextSize(1);
    int x = 3;
    hKey(x, "ENT"); hLbl(x, isPlaying ? ":stop " : ":play ");
    hKey(x, "N");   hLbl(x, "/");
    hKey(x, "P");   hLbl(x, ":nav ");
    hKey(x, "+");   hLbl(x, "/");
    hKey(x, "-");   hLbl(x, ":vol ");
    hKey(x, "M");   hLbl(x, ":mute ");
    hKey(x, "?");   hLbl(x, ":aide");
}

void drawHelpScreen() {
    M5.Lcd.fillScreen(COL_HELP_BG);
    M5.Lcd.setTextFont(1); M5.Lcd.setTextSize(1);
    M5.Lcd.fillRect(0, 0, W, HDR_H, COL_HDR_BG);
    M5.Lcd.setTextColor(COL_HDR_FG, COL_HDR_BG);
    M5.Lcd.drawString("  AIDE — JEFF'S WEB RADIO v2.2", 4, 4);
    struct { const char* k; const char* d; } lines[] = {
        { "ENTER",  "Lancer / arreter la radio"   },
        { "N",      "Station suivante"            },
        { "P",      "Station precedente"          },
        { "+ / =",  "Volume +1  (0 a 21)"         },
        { "-",      "Volume -1"                   },
        { "M",      "Mute / Unmute"               },
        { "R",      "Recharger infos du morceau"  },
        { "S",      "Stop force"                  },
        { "?",      "Fermer cette aide"           },
    };
    int n = sizeof(lines) / sizeof(lines[0]);
    for (int i = 0; i < n; i++) {
        int y = HDR_H + i * 13;
        uint16_t bg = (i % 2 == 0) ? COL_HELP_BG : M5.Lcd.color565(5, 28, 52);
        M5.Lcd.fillRect(0, y, W, 13, bg);
        M5.Lcd.fillRect(2, y + 1, 52, 11, COL_KEY_BG);
        M5.Lcd.setTextColor(COL_KEY_FG, COL_KEY_BG);
        M5.Lcd.drawString(lines[i].k, 4, y + 2);
        M5.Lcd.setTextColor(COL_TEXT, bg);
        M5.Lcd.drawString(lines[i].d, 58, y + 2);
    }
    M5.Lcd.fillRect(0, H - 11, W, 11, COL_BAR_BG);
    M5.Lcd.setTextColor(COL_DIM, COL_BAR_BG);
    M5.Lcd.drawString("Volume 0-21  |  appuie ? pour fermer", 4, H - 9);
}

void drawUI() {
    if (showHelp) { drawHelpScreen(); return; }
    drawHeader();
    drawMetaBand();
    drawVolBar();
    drawStationList();
    drawHintBar();
}

// ============================================================
//  WIFI
// ============================================================
void startWiFiPortal() {
    M5.Lcd.fillScreen(COL_BG);
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextColor(COL_HDR_FG, COL_BG); M5.Lcd.setTextSize(2);
    M5.Lcd.drawString("Jeff's", 65, 10);
    M5.Lcd.drawString("Web Radio", 42, 34);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COL_SEL_FG, COL_BG);
    M5.Lcd.drawString("v2.2  Cardputer ADV", 42, 66);
    M5.Lcd.setTextColor(COL_DIM, COL_BG);
    M5.Lcd.drawString("WiFi AP: JeffsRadio-Setup", 24, 82);
    M5.Lcd.drawString("puis 192.168.4.1", 52, 96);
    WiFiManager wm;
    wm.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
    wm.setConfigPortalTimeout(180);
    bool ok = wm.autoConnect("JeffsRadio-Setup");
    M5.Lcd.fillScreen(COL_BG); M5.Lcd.setTextSize(1);
    if (ok) {
        M5.Lcd.setTextColor(COL_PLAY, COL_BG);
        M5.Lcd.drawString("WiFi connecte!", 35, 55);
        M5.Lcd.setTextColor(COL_TEXT, COL_BG);
        M5.Lcd.drawString(WiFi.localIP().toString(), 65, 72);
        delay(1200);
    } else {
        M5.Lcd.setTextColor(COL_STOP, COL_BG);
        M5.Lcd.drawString("WiFi non configure", 28, 55);
        delay(2500);
    }
}

// ============================================================
//  CLAVIER
// ============================================================
void handleKeyboard() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
    if (st.enter) {
        showHelp = false;
        if (isPlaying) stopStream(); else startStream();
        drawUI();
        return;
    }
    for (auto k : st.word) {
        switch (k) {
            case 'n': case 'N':
                if (isPlaying) stopStream();
                currentStation = (currentStation + 1) % NUM_STATIONS;
                drawStationList(); drawHintBar(); break;
            case 'p': case 'P':
                if (isPlaying) stopStream();
                currentStation = (currentStation - 1 + NUM_STATIONS) % NUM_STATIONS;
                drawStationList(); drawHintBar(); break;
            case '+': case '=':
                isMuted = false; volume = min(21, volume + 1);
                applyVolume(); drawVolBar(); break;
            case '-':
                isMuted = false; volume = max(0, volume - 1);
                applyVolume(); drawVolBar(); break;
            case 'm': case 'M':
                isMuted = !isMuted; applyVolume(); drawVolBar(); break;
            case 's': case 'S':
                if (isPlaying) { stopStream(); drawUI(); } break;
            case 'r': case 'R':
                if (isPlaying) { fetchMetadata(); drawMetaBand(); } break;
            case '?':
                showHelp = !showHelp; drawUI(); break;
        }
    }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    M5.Lcd.setRotation(1);
    M5.Lcd.fillScreen(COL_BG);
    M5.Lcd.setTextFont(1);

    // Splash
    M5.Lcd.setTextColor(COL_HDR_FG, COL_BG); M5.Lcd.setTextSize(2);
    M5.Lcd.drawString("Jeff's", 65, 14);
    M5.Lcd.drawString("Web Radio", 42, 40);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COL_DIM, COL_BG);
    M5.Lcd.drawString("v2.2  Cardputer ADV", 42, 80);
    M5.Lcd.setTextColor(COL_SEL_FG, COL_BG);
    M5.Lcd.drawString("by Jeff  (powered by Claude)", 20, 96);
    delay(1800);

    startWiFiPortal();

    // Init audio pins
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(volume);

    drawUI();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
    audio.loop();
    handleKeyboard();

    // Ticker défilant
    if (isPlaying && millis() - lastTickerMove > 320) {
        tickerOffset++;
        drawMetaBand();
        lastTickerMove = millis();
    }

    // Refresh metadata périodique
    if (isPlaying && millis() - lastMetaFetch > META_INTERVAL) {
        fetchMetadata();
        drawMetaBand();
    }

    vTaskDelay(1);
}
