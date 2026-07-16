#include <WiFi.h>
#include <WebSocketsServer.h>
#include <FastLED.h>

// ============================================================
//  MODIFIE CES DEUX LIGNES AVEC TON WIFI
// ============================================================
const char* WIFI_SSID     = "TON_WIFI";
const char* WIFI_PASSWORD = "TON_MOT_DE_PASSE";

// ============================================================
//  RUBAN LED — ajuste NUM_LEDS selon la longueur que tu as coupée
// ============================================================
#define LED_PIN     4
#define NUM_LEDS    176      // <- adapte à ta coupe réelle
#define BRIGHTNESS  180

// Piano 88 touches
#define MIDI_MIN    21       // La0
#define NUM_KEYS    88
#define NUM_WHITES  52       // touches blanches d'un piano 88 touches

CRGB leds[NUM_LEDS];
WebSocketsServer webSocket(81);

bool pedalActive = false;

// ---- Détecte si une note MIDI est une touche noire ----
bool isBlackKey(int midi) {
  int pc = midi % 12;
  return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

// ---- Calcule la position réelle sur le ruban et allume la note ----
void setNote(int midi, uint8_t r, uint8_t g, uint8_t b) {
  if (midi < MIDI_MIN || midi > MIDI_MIN + NUM_KEYS - 1) return;

  int w = 0; // nombre de touches blanches avant cette note
  for (int m = MIDI_MIN; m < midi; m++) if (!isBlackKey(m)) w++;

  float pos = isBlackKey(midi) ? (w - 0.5f) : (float)w;
  int idx = (int)(pos / (NUM_WHITES - 1) * (NUM_LEDS - 1));
  if (idx < 0) idx = 0;
  if (idx >= NUM_LEDS) idx = NUM_LEDS - 1;

  // Teinte violette pour les touches noires (sauf hit/wrong)
  bool isRightColor = (r == 0   && g == 220 && b == 255); // cyan = main droite
  bool isLeftColor  = (r == 255 && g == 200 && b ==   0); // doré = main gauche

  if (isBlackKey(midi) && (isRightColor || isLeftColor)) {
    if (isRightColor) { r = 150; g = 60;  b = 255; }  // violet clair (droite, noire)
    else               { r = 130; g = 20;  b = 200; }  // violet foncé (gauche, noire)
  }

  leds[idx] = CRGB(r, g, b);
  if (idx + 1 < NUM_LEDS) leds[idx + 1] = CRGB(r, g, b);
  FastLED.show();
}

void clearAll() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  pedalActive = false;
  FastLED.show();
}

// ---- Pédale : allume les deux extrémités du ruban en blanc ----
#define PEDAL_COLOR CRGB(255, 255, 255)

void setPedal(bool on) {
  pedalActive = on;
  CRGB c = on ? PEDAL_COLOR : CRGB::Black;
  leds[0] = c;
  leds[1] = c;
  leds[NUM_LEDS - 1] = c;
  leds[NUM_LEDS - 2] = c;
  FastLED.show();
}

// Format des messages reçus :
//   "on:60:R"  "on:48:L"  "on:60:hit"  "off:60"  "clear"
//   "pedal:on" "pedal:off"
void handleMessage(String m) {
  if (m == "clear")     { clearAll();      return; }
  if (m == "pedal:on")  { setPedal(true);  return; }
  if (m == "pedal:off") { setPedal(false); return; }

  int p1 = m.indexOf(':');
  if (p1 < 0) return;
  String cmd = m.substring(0, p1);

  if (cmd == "off") {
    int midi = m.substring(p1 + 1).toInt();
    setNote(midi, 0, 0, 0);
    return;
  }

  if (cmd == "on") {
    int p2 = m.indexOf(':', p1 + 1);
    int midi = m.substring(p1 + 1, p2 > 0 ? p2 : m.length()).toInt();
    String param = p2 > 0 ? m.substring(p2 + 1) : "R";

    if      (param == "hit")   setNote(midi,  50, 255, 120); // vert
    else if (param == "wrong") setNote(midi, 255,  50,  80); // rouge
    else if (param == "L")     setNote(midi, 255, 200,   0); // doré (gauche)
    else                       setNote(midi,   0, 220, 255); // cyan (droite)
  }
}

void onEvent(uint8_t client, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    // balayage arc-en-ciel de confirmation
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CHSV(i * 256 / NUM_LEDS, 255, 200);
      if (i > 0) leds[i - 1] = CRGB::Black;
      FastLED.show();
      delay(3);
    }
    clearAll();
  } else if (type == WStype_DISCONNECTED) {
    clearAll();
  } else if (type == WStype_TEXT) {
    handleMessage(String((char*)payload));
  }
}

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  clearAll();

  // 3 flashs blancs au démarrage
  for (int i = 0; i < 3; i++) {
    fill_solid(leds, NUM_LEDS, CRGB::White);
    FastLED.show(); delay(150);
    clearAll();     delay(150);
  }

  // Connexion WiFi (LEDs jaunes qui défilent pendant la recherche)
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int d = 0;
  while (WiFi.status() != WL_CONNECTED) {
    leds[d % NUM_LEDS] = CRGB(255, 180, 0);
    FastLED.show(); delay(100);
    leds[d % NUM_LEDS] = CRGB::Black;
    d++;
  }

  // Flash vert = connecté
  fill_solid(leds, NUM_LEDS, CRGB::Green);
  FastLED.show(); delay(500);
  clearAll();

  Serial.println();
  Serial.println("WiFi connecte !");
  Serial.print("IP de l'ESP32 : ");
  Serial.println(WiFi.localIP());
  Serial.println("Copie cette IP dans l'appli Piano Flow");

  webSocket.begin();
  webSocket.onEvent(onEvent);
}

void loop() {
  webSocket.loop();
}
