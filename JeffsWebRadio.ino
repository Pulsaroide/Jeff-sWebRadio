// ============================================================
//  JEFF'S WEB RADIO — v1.1
//  Pour M5Stack Cardputer ADV (ESP32-S3)
//  Bibliothèques requises :
//    - M5Cardputer       (M5Stack, via Arduino Library Manager)
//    - ESP8266Audio      (earlephilhower, via GitHub)
//    - WiFiManager       (tzapu, via Arduino Library Manager)
//    - ArduinoJson       (benoit-blanchon, via Arduino Library Manager)
// ============================================================

#include <M5Cardputer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "AudioFileSourceHTTPStream.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

// ============================================================
//  CONFIGURATION STATIONS
// ============================================================
struct Station {
    const char* name;
    const char* url;
    const char* metaUrl;
};

Station STATIONS[] = {
    // ── France ──────────────────────────────────────────────
    {
        "Nostalgie",
        "https://streaming.nrjaudio.fm/oug7girb92oc",
        ""
    },
    {
        "FIP",
        "http://icecast.radiofrance.fr/fip-midfi.mp3",
        "https://api.radiofrance.fr/livemeta/pull/7"
    },
    // ── Radio Paradise ───────────────────────────────────────
    {
        "RP Main Mix",
        "http://stream.radioparadise.com/mp3-128",
        "https://api.radioparadise.com/api/now_playing?block_id=0&format=json"
    },
    {
        "RP Mellow Mix",
        "http://stream.radioparadise.com/mellow-128",
        "https://api.radioparadise.com/api/now_playing?block_id=1&format=json"
    },
    {
        "RP Rock Mix",
        "http://stream.radioparadise.com/rock-128",
        "https://api.radioparadise.com/api/now_playing?block_id=2&format=json"
    },
    // ── SomaFM ──────────────────────────────────────────────
    {
        "SomaFM Groove",
        "https://ice.somafm.com/groovesalad",
        ""
    },
    {
        "SomaFM Space",
        "https://ice.somafm.com/spacestation",
        ""
    },
    {
        "SomaFM 80s",
        "https://ice.somafm.com/u80s",
        ""
    },
    // ── Synthwave / Lofi ─────────────────────────────────────
    {
        "Nightride FM",
        "https://stream.nightride.fm/nightride.mp3",
        ""
    },
    {
        "Lofi Hip Hop",
        "https://stream.zeno.fm/f3wvbbqmdg8uv",
        ""
    },
    // ── Blues / Jazz ─────────────────────────────────────────
    {
        "BluesWave Athens",
        "http://blueswave.radio:8000/blueswave",
        ""
    },
    {
        "Blues Radio",
        "http://198.58.98.83/stream/1/",
        ""
    },
    {
        "101 Smooth Jazz",
        "http://strm112.1.fm/smoothjazz_mobile_mp3",
        ""
    },
    {
        "Smooth Jazz Deluxe",
        "http://agnes.torontocast.com:8142/stream",
        ""
    },
    // ── Lounge / Chill ───────────────────────────────────────
    {
        "Chillout Lounge",
        "http://strm112.1.fm/chilloutlounge_mobile_mp3",
        ""
    },
    {
        "Sensual Lounge",
        "http://agnes.torontocast.com:8146/stream",
        ""
    },
    // ── France ──────────────────────────────────────────────
    {
        "Sud Radio",
        "http://broadcast.infomaniak.ch/sudradio-high.mp3",
        ""
    },
};

const int NUM_STATIONS = sizeof(STATIONS) / sizeof(STATIONS[0]);

// ============================================================
//  AUDIO ENGINE
// ============================================================
AudioFileSourceHTTPStream* audioSource  = nullptr;
AudioFileSourceBuffer*     audioBuffer  = nullptr;
AudioGeneratorMP3*         audioMP3     = nullptr;
AudioOutputI2S*            audioOutput  = nullptr;

#define I2S_BCLK   41
#define I2S_LRCLK  43
#define I2S_DOUT   42

// ============================================================
//  ÉTAT APPLICATION
// ============================================================
int   currentStation = 0;
bool  isPlaying      = false;
int   volume         = 75;
bool  isMuted        = false;

String nowPlaying    = "";
String nowArtist     = "";
String statusMsg     = "Prêt";

// VU-mètre
int   vuLevel        = 0;
int   vuPeak         = 0;
unsigned long vuPeakTime = 0;

// Metadata refresh
unsigned long lastMetaFetch  = 0;
const long    META_INTERVAL  = 15000;

// Ticker metadata
int   tickerOffset   = 0;
unsigned long lastTickerMove = 0;

bool  wifiConfigDone = false;

// ============================================================
//  COULEURS
// ============================================================
#define COL_BG          M5.Lcd.color565(8,   8,  18)
#define COL_HEADER_BG   M5.Lcd.color565(0,   40,  80)
#define COL_HEADER_FG   M5.Lcd.color565(0,  180, 255)
#define COL_SELECT_BG   M5.Lcd.color565(20,  50,  90)
#define COL_SELECT_FG   M5.Lcd.color565(255, 200,  0)
#define COL_TEXT        M5.Lcd.color565(220, 220, 220)
#define COL_MUTED       M5.Lcd.color565(90,  90,  90)
#define COL_PLAY        M5.Lcd.color565(0,   255, 120)
#define COL_STOP        M5.Lcd.color565(255,  60,  60)
#define COL_BAR_BG      M5.Lcd.color565(15,  15,  35)
#define COL_VU_LOW      M5.Lcd.color565(0,   220,  80)
#define COL_VU_MID      M5.Lcd.color565(255, 180,  0)
#define COL_VU_HIGH     M5.Lcd.color565(255,  40,  40)
#define COL_META_BG     M5.Lcd.color565(5,   25,  45)
#define COL_META_FG     M5.Lcd.color565(180, 230, 255)
#define COL_PEAK        M5.Lcd.color565(255, 255, 255)

// ============================================================
//  DIMENSIONS ÉCRAN
// ============================================================
#define W   240
#define H   135

#define HEADER_H     18
#define META_H       22
#define LIST_Y       (HEADER_H + META_H)
#define LIST_ITEM_H  16
#define BAR_H        22
#define LIST_H       (H - LIST_Y - BAR_H)
#define VU_Y         (H - BAR_H)

// ============================================================
//  WIFI MANAGER — FIX : suppression de setAPName() inexistant
// ============================================================
void startWiFiPortal() {
    M5.Lcd.fillScreen(COL_BG);
    M5.Lcd.setTextColor(COL_HEADER_FG, COL_BG);
    M5.Lcd.setTextSize(1);
    M5.Lcd.drawString("JEFF'S WEB RADIO", 10, 10);
    M5.Lcd.setTextColor(COL_TEXT, COL_BG);
    M5.Lcd.drawString("Configuration WiFi...", 10, 30);
    M5.Lcd.setTextColor(COL_SELECT_FG, COL_BG);
    M5.Lcd.drawString("Connecte-toi au WiFi:", 10, 50);
    M5.Lcd.drawString("  JeffsRadio-Setup", 10, 65);
    M5.Lcd.setTextColor(COL_TEXT, COL_BG);
    M5.Lcd.drawString("Puis ouvre:", 10, 82);
    M5.Lcd.drawString("  192.168.4.1", 10, 97);
    M5.Lcd.setTextColor(COL_MUTED, COL_BG);
    M5.Lcd.drawString("(portail de config)", 10, 115);

    WiFiManager wm;
    // FIX v1.1 : setAPName() n'existe pas dans WiFiManager 2.0.17
    // Le nom du portail est passé directement dans autoConnect()
    wm.setAPStaticIPConfig(
        IPAddress(192,168,4,1),
        IPAddress(192,168,4,1),
        IPAddress(255,255,255,0)
    );
    wm.setConfigPortalTimeout(180);

    bool connected = wm.autoConnect("JeffsRadio-Setup");

    if (connected) {
        M5.Lcd.fillScreen(COL_BG);
        M5.Lcd.setTextColor(COL_PLAY, COL_BG);
        M5.Lcd.drawString("WiFi connecte !", 10, 50);
        M5.Lcd.setTextColor(COL_TEXT, COL_BG);
        M5.Lcd.drawString(WiFi.localIP().toString(), 10, 70);
        delay(1500);
        wifiConfigDone = true;
    } else {
        M5.Lcd.fillScreen(COL_BG);
        M5.Lcd.setTextColor(COL_STOP, COL_BG);
        M5.Lcd.drawString("WiFi non configure", 10, 50);
        M5.Lcd.drawString("Redemarre pour reessayer", 10, 70);
        delay(3000);
    }
}

// ============================================================
//  AUDIO — DÉMARRER STREAM
// ============================================================
void startStream() {
    stopStream();
    statusMsg = "Connexion...";
    drawUI();

    audioOutput = new AudioOutputI2S();
    audioOutput->SetPinout(I2S_BCLK, I2S_LRCLK, I2S_DOUT);
    audioOutput->SetGain((float)volume / 100.0f * 4.0f);

    audioSource = new AudioFileSourceHTTPStream(STATIONS[currentStation].url);
    audioBuffer = new AudioFileSourceBuffer(audioSource, 8192);
    audioMP3    = new AudioGeneratorMP3();

    if (audioMP3->begin(audioBuffer, audioOutput)) {
        isPlaying  = true;
        statusMsg  = "En lecture";
        nowPlaying = STATIONS[currentStation].name;
        tickerOffset = 0;
        fetchMetadata();
    } else {
        statusMsg = "Erreur stream!";
        stopStream();
    }
    drawUI();
}

// ============================================================
//  AUDIO — ARRÊTER STREAM
// ============================================================
void stopStream() {
    if (audioMP3)    { audioMP3->stop();  delete audioMP3;    audioMP3    = nullptr; }
    if (audioBuffer) {                    delete audioBuffer; audioBuffer = nullptr; }
    if (audioSource) {                    delete audioSource; audioSource = nullptr; }
    if (audioOutput) {                    delete audioOutput; audioOutput = nullptr; }
    isPlaying    = false;
    statusMsg    = "Arrete";
    nowPlaying   = "";
    nowArtist    = "";
    vuLevel      = 0;
    vuPeak       = 0;
    tickerOffset = 0;
}

// ============================================================
//  AUDIO — VOLUME
// ============================================================
void applyVolume() {
    if (audioOutput) {
        float gain = isMuted ? 0.0f : (float)volume / 100.0f * 4.0f;
        audioOutput->SetGain(gain);
    }
}

// ============================================================
//  METADATA — FIX ArduinoJson v7 (JsonDocument au lieu de DynamicJsonDocument)
// ============================================================
void fetchMetadata() {
    if (strlen(STATIONS[currentStation].metaUrl) == 0) return;
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    http.begin(STATIONS[currentStation].metaUrl);
    http.addHeader("User-Agent", "JeffsWebRadio/1.1");
    int code = http.GET();

    if (code == 200) {
        String payload = http.getString();
        // FIX v1.1 : DynamicJsonDocument déprécié en ArduinoJson v7
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (!err) {
            // Radio Paradise (stations 2, 3, 4)
            if (currentStation == 2 || currentStation == 3 || currentStation == 4) {
                nowPlaying = doc["title"] | String(STATIONS[currentStation].name);
                nowArtist  = doc["artist"] | String("");
            }
            // FIP (station 1)
            else if (currentStation == 1) {
                JsonObject now = doc["now"];
                nowPlaying = now["firstLine"]["title"] | String(STATIONS[currentStation].name);
                nowArtist  = now["secondLine"]["title"] | String("");
            }
            else {
                nowPlaying = doc["title"] | String(STATIONS[currentStation].name);
                nowArtist  = doc["artist"] | String("");
            }
            tickerOffset = 0;
        }
    }
    http.end();
    lastMetaFetch = millis();
}

// ============================================================
//  VU-MÈTRE SIMULÉ
// ============================================================
void updateVU() {
    if (!isPlaying) { vuLevel = 0; return; }
    int target = random(40, 95);
    vuLevel = (vuLevel * 3 + target) / 4;
    if (vuLevel > vuPeak) {
        vuPeak     = vuLevel;
        vuPeakTime = millis();
    } else if (millis() - vuPeakTime > 1200) {
        vuPeak = max(0, vuPeak - 3);
    }
}

// ============================================================
//  DESSIN UI
// ============================================================
void drawHeader() {
    M5.Lcd.fillRect(0, 0, W, HEADER_H, COL_HEADER_BG);
    M5.Lcd.setTextColor(COL_HEADER_FG, COL_HEADER_BG);
    M5.Lcd.setTextSize(1);
    M5.Lcd.drawString(">> JEFF'S WEB RADIO", 4, 5);
    if (WiFi.status() == WL_CONNECTED) {
        M5.Lcd.setTextColor(COL_PLAY, COL_HEADER_BG);
        M5.Lcd.drawString("WiFi", W - 32, 5);
    } else {
        M5.Lcd.setTextColor(COL_STOP, COL_HEADER_BG);
        M5.Lcd.drawString("NoWiFi", W - 44, 5);
    }
}

void drawMetaBand() {
    M5.Lcd.fillRect(0, HEADER_H, W, META_H, COL_META_BG);

    String full = "";
    if (nowArtist.length() > 0)       full = nowArtist + " - " + nowPlaying;
    else if (nowPlaying.length() > 0)  full = nowPlaying;
    else                               full = statusMsg;

    // Ticker scrolling si texte > 28 chars
    const int MAX_VISIBLE = 28;
    String display = full;

    if ((int)full.length() > MAX_VISIBLE) {
        // Boucle le texte avec un espace séparateur
        String looped = full + "   " + full;
        if (tickerOffset >= (int)(full.length() + 3)) tickerOffset = 0;
        display = looped.substring(tickerOffset, tickerOffset + MAX_VISIBLE);
    }

    M5.Lcd.setTextSize(1);
    if (isPlaying) {
        M5.Lcd.setTextColor(COL_PLAY, COL_META_BG);
        M5.Lcd.drawString("*", 4, HEADER_H + 7);
        M5.Lcd.setTextColor(COL_META_FG, COL_META_BG);
    } else {
        M5.Lcd.setTextColor(COL_MUTED, COL_META_BG);
    }
    M5.Lcd.drawString(display, 16, HEADER_H + 7);
}

void drawStationList() {
    for (int i = 0; i < NUM_STATIONS; i++) {
        int y = LIST_Y + i * LIST_ITEM_H;
        bool selected = (i == currentStation);

        if (selected) {
            M5.Lcd.fillRect(0, y, W, LIST_ITEM_H, COL_SELECT_BG);
            M5.Lcd.fillRect(0, y, 3, LIST_ITEM_H, COL_SELECT_FG);
            M5.Lcd.setTextColor(COL_SELECT_FG, COL_SELECT_BG);
        } else {
            M5.Lcd.fillRect(0, y, W, LIST_ITEM_H, COL_BG);
            M5.Lcd.setTextColor(COL_MUTED, COL_BG);
        }

        M5.Lcd.setTextSize(1);
        String label = String(i + 1) + ". " + STATIONS[i].name;
        M5.Lcd.drawString(label, 8, y + 4);

        if (selected && isPlaying) {
            M5.Lcd.fillRect(W - 50, y + 2, 46, 12, COL_PLAY);
            M5.Lcd.setTextColor(COL_BG, COL_PLAY);
            M5.Lcd.drawString("ON AIR", W - 46, y + 4);
        }
    }
}

void drawVUMeter() {
    M5.Lcd.fillRect(0, VU_Y, W, BAR_H, COL_BAR_BG);
    M5.Lcd.setTextColor(COL_TEXT, COL_BAR_BG);
    M5.Lcd.setTextSize(1);
    String volStr = isMuted ? "MUTE" : "V:" + String(volume) + "%";
    M5.Lcd.drawString(volStr, 4, VU_Y + 7);

    int vuX    = 50;
    int vuW    = 130;
    int vuH    = 8;
    int vuYpos = VU_Y + 7;
    int filled = (vuLevel * vuW) / 100;
    int peak_x = vuX + (vuPeak * vuW) / 100;

    M5.Lcd.fillRect(vuX, vuYpos, vuW, vuH, M5.Lcd.color565(20, 20, 40));

    for (int x = 0; x < filled; x++) {
        int pct = (x * 100) / vuW;
        uint16_t col;
        if (pct < 60)       col = COL_VU_LOW;
        else if (pct < 85)  col = COL_VU_MID;
        else                col = COL_VU_HIGH;
        M5.Lcd.drawFastVLine(vuX + x, vuYpos, vuH, col);
    }
    if (vuPeak > 0) M5.Lcd.drawFastVLine(peak_x, vuYpos, vuH, COL_PEAK);

    M5.Lcd.setTextColor(M5.Lcd.color565(70, 70, 70), COL_BAR_BG);
    M5.Lcd.drawString("ENT=play N/P=nav +/-=vol M=mute", W - 190, VU_Y + 7);
}

void drawUI() {
    drawHeader();
    drawMetaBand();
    drawStationList();
    drawVUMeter();
}

// ============================================================
//  GESTION CLAVIER — FIX : navigation haut/bas implémentée
// ============================================================
void handleKeyboard() {
    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

        // Entrée = Play/Stop
        if (status.enter) {
            if (isPlaying) stopStream();
            else           startStream();
            drawUI();
            return;
        }

        // Navigation haut/bas (touches fn)
        if (status.fn) {
            // Fn seul, combiné avec les touches de navigation
        }

        for (auto key : status.word) {
            switch (key) {
                // Station suivante
                case 'n': case 'N':
                    if (isPlaying) stopStream();
                    currentStation = (currentStation + 1) % NUM_STATIONS;
                    drawUI();
                    break;

                // Station précédente
                case 'p': case 'P':
                    if (isPlaying) stopStream();
                    currentStation = (currentStation - 1 + NUM_STATIONS) % NUM_STATIONS;
                    drawUI();
                    break;

                // Volume +
                case '+': case '=':
                    volume = min(100, volume + 5);
                    applyVolume();
                    drawVUMeter();
                    break;

                // Volume -
                case '-':
                    volume = max(0, volume - 5);
                    applyVolume();
                    drawVUMeter();
                    break;

                // Mute
                case 'm': case 'M':
                    isMuted = !isMuted;
                    applyVolume();
                    drawVUMeter();
                    break;

                // Sélection directe 1-4
                case '1': case '2': case '3': case '4':
                    if (isPlaying) stopStream();
                    currentStation = (key - '1');
                    if (currentStation < NUM_STATIONS) drawUI();
                    break;

                // Stop
                case 's': case 'S':
                    if (isPlaying) { stopStream(); drawUI(); }
                    break;

                // Refresh metadata
                case 'r': case 'R':
                    if (isPlaying) { fetchMetadata(); drawMetaBand(); }
                    break;
            }
        }
    }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);

    M5.Lcd.setRotation(1);
    M5.Lcd.fillScreen(COL_BG);
    M5.Lcd.setTextFont(1);

    M5.Lcd.setTextColor(COL_HEADER_FG, COL_BG);
    M5.Lcd.setTextSize(2);
    M5.Lcd.drawString("Jeff's", 60, 20);
    M5.Lcd.drawString("Web Radio", 40, 45);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(COL_MUTED, COL_BG);
    M5.Lcd.drawString("v1.3 - Cardputer ADV", 35, 80);
    M5.Lcd.setTextColor(COL_SELECT_FG, COL_BG);
    M5.Lcd.drawString("by Jeff  (powered by Claude)", 20, 100);
    delay(2000);

    startWiFiPortal();
    drawUI();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
    M5Cardputer.update();

    // Traitement audio
    if (isPlaying && audioMP3) {
        if (audioMP3->isRunning()) {
            if (!audioMP3->loop()) {
                statusMsg = "Reconnexion...";
                drawMetaBand();
                delay(2000);
                startStream();
            }
        }
    }

    // VU-mètre (80ms)
    static unsigned long lastVU = 0;
    if (millis() - lastVU > 80) {
        updateVU();
        drawVUMeter();
        lastVU = millis();
    }

    // Ticker scrolling (350ms)
    if (isPlaying && millis() - lastTickerMove > 350) {
        tickerOffset++;
        drawMetaBand();
        lastTickerMove = millis();
    }

    // Refresh metadata (15s)
    if (isPlaying && millis() - lastMetaFetch > META_INTERVAL) {
        fetchMetadata();
        drawMetaBand();
    }

    handleKeyboard();
    delay(20);
}
