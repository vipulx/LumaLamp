/*
 * LumaLamp Smart Light Firmware for ESP32-C6
 * Drives a 16-bit WRGB WS2812/SK6812 LED Ring
 *
 * Dependencies (Arduino IDE Library Manager):
 * - Adafruit NeoPixel by Adafruit
 * - WebSockets by Markus Sattler
 * - ArduinoJson by Benoit Blanchon
 */

#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WebSocketsClient.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// --- Configuration ---
#define LED_PIN 8        // Onboard RGB LED on ESP32-C6 DevKit
#define NUM_LEDS 1       // Single onboard LED
#define LED_TYPE NEO_GRB // Onboard LED is WS2812 RGB (no white channel)
#define WS_PORT 81

// --- Globals ---
Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, LED_TYPE + NEO_KHZ800);
WebServer server(80);
DNSServer dnsServer;
WebSocketsServer webSocket = WebSocketsServer(WS_PORT);
Preferences preferences;

// --- Cloud Relay WebSocket Client ---
WiFiClientSecure secureClient;
WebSocketsClient webSocketClient;
bool cloudWsConnected = false;

// --- Lamp State ---
bool power = true;
uint8_t brightness = 80;
struct {
  uint8_t r = 168;
  uint8_t g = 85;
  uint8_t b = 247;
  char hex[8] = "#a855f7";
  uint16_t temp = 4000;
} lampColor;
String activeEffect = "rainbow";

// --- System Telemetry ---
float deviceTemp = 36.5;
unsigned long lastStateBroadcast = 0;
unsigned long lastEffectUpdate = 0;

// --- WiFi AP Setup Mode ---
bool isApMode = false;
String wifiSsid = "";
String wifiPassword = "";
String lampName = "";
String roomName = "Living Room";
String deviceSuffix = "";

// --- Function Prototypes ---
void loadConfiguration();
void saveConfiguration();
void initWiFi();
void setupWebServer();
void broadcastState();
void updateLEDs();
void handleEffectNone();
void handleEffectRainbow();
void handleEffectAurora();
void handleEffectSunset();
void handleEffectOcean();
void handleEffectFire();
void handleEffectPulse();
void handleEffectBreathing();
void handleEffectRelax();
void handleEffectGaming();
void handleEffectMusic();

String getMacSuffix();
void initCloudWebSocket();
void sendStateToCloud();
void handleCloudMessage(char *payload);
void webSocketClientEvent(WStype_t type, uint8_t *payload, size_t length);
void handleLocalWebSocketMessage(uint8_t num, uint8_t *payload, size_t length);

// --- Embedded CSS Stylesheet ---
const char PAGE_CSS[] PROGMEM = R"rawcss(
:root {
  --bg-color: #0b0818;
  --panel-bg: rgba(20, 16, 42, 0.45);
  --border-color: rgba(255, 255, 255, 0.08);
  --text-color: #f3f4f6;
  --text-muted: #9ca3af;
  --accent-purple: #a855f7;
  --accent-blue: #3b82f6;
  --accent-glow: rgba(168, 85, 247, 0.35);
  --success-color: #10b981;
  --danger-color: #ef4444;
  --font-family: 'Inter', system-ui, -apple-system, sans-serif;
  --heading-font: 'Outfit', sans-serif;
}
* {
  box-sizing: border-box;
  margin: 0;
  padding: 0;
}
body {
  background: var(--bg-color);
  background-image: 
    radial-gradient(circle at 10% 20%, rgba(168, 85, 247, 0.12) 0%, transparent 45%),
    radial-gradient(circle at 90% 80%, rgba(59, 130, 246, 0.12) 0%, transparent 45%);
  color: var(--text-color);
  font-family: var(--font-family);
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 0;
}
header {
  width: 100%;
  max-width: 1000px;
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 20px 24px;
  background: rgba(13, 10, 31, 0.6);
  backdrop-filter: blur(12px);
  -webkit-backdrop-filter: blur(12px);
  border-bottom: 1px solid var(--border-color);
  position: sticky;
  top: 0;
  z-index: 100;
  border-radius: 0 0 16px 16px;
}
.logo-container {
  display: flex;
  align-items: center;
  gap: 10px;
}
.logo-text {
  font-family: var(--heading-font);
  font-size: 1.5rem;
  font-weight: 700;
  background: linear-gradient(to right, #a855f7, #3b82f6);
  -webkit-background-clip: text;
  color: transparent;
  letter-spacing: 0.5px;
}
.nav-menu {
  display: flex;
  gap: 8px;
}
.nav-link {
  color: var(--text-muted);
  text-decoration: none;
  font-weight: 500;
  padding: 8px 16px;
  border-radius: 8px;
  transition: all 0.2s ease;
  font-size: 0.95rem;
}
.nav-link:hover {
  color: var(--text-color);
  background: rgba(255, 255, 255, 0.05);
}
.nav-link.active {
  color: var(--text-color);
  background: rgba(168, 85, 247, 0.15);
  border: 1px solid rgba(168, 85, 247, 0.3);
}
.status-badge {
  display: flex;
  align-items: center;
  gap: 8px;
  background: rgba(255, 255, 255, 0.04);
  padding: 6px 12px;
  border-radius: 20px;
  border: 1px solid var(--border-color);
  font-size: 0.85rem;
}
.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
}
.status-dot.connected {
  background: var(--success-color);
  box-shadow: 0 0 8px var(--success-color);
}
.status-dot.disconnected {
  background: var(--danger-color);
  box-shadow: 0 0 8px var(--danger-color);
}
main {
  width: 100%;
  max-width: 1000px;
  padding: 24px;
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 24px;
}
.card {
  background: var(--panel-bg);
  backdrop-filter: blur(16px);
  -webkit-backdrop-filter: blur(16px);
  border: 1px solid var(--border-color);
  border-radius: 20px;
  padding: 24px;
  box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.3);
  transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
}
.card:hover {
  border-color: rgba(255, 255, 255, 0.12);
  box-shadow: 0 12px 40px 0 rgba(168, 85, 247, 0.08);
}
h2 {
  font-family: var(--heading-font);
  font-weight: 600;
  margin-bottom: 20px;
  font-size: 1.3rem;
  letter-spacing: -0.3px;
  display: flex;
  align-items: center;
  gap: 8px;
}
.grid-2 {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 24px;
}
@media (max-width: 768px) {
  .grid-2 {
    grid-template-columns: 1fr;
  }
  header {
    flex-direction: column;
    gap: 16px;
    border-radius: 0;
  }
}
.control-group {
  margin-bottom: 20px;
}
.control-label {
  display: flex;
  justify-content: space-between;
  margin-bottom: 8px;
  font-size: 0.9rem;
  color: var(--text-muted);
}
.slider-container {
  display: flex;
  align-items: center;
  gap: 12px;
}
input[type="range"] {
  -webkit-appearance: none;
  width: 100%;
  height: 6px;
  background: rgba(255, 255, 255, 0.1);
  border-radius: 3px;
  outline: none;
}
input[type="range"]::-webkit-slider-thumb {
  -webkit-appearance: none;
  width: 20px;
  height: 20px;
  border-radius: 50%;
  background: var(--text-color);
  box-shadow: 0 0 10px rgba(168, 85, 247, 0.8);
  cursor: pointer;
  transition: transform 0.1s ease;
}
input[type="range"]::-webkit-slider-thumb:hover {
  transform: scale(1.2);
}
.color-picker-wrapper {
  display: flex;
  align-items: center;
  gap: 20px;
}
input[type="color"] {
  -webkit-appearance: none;
  border: none;
  width: 60px;
  height: 60px;
  border-radius: 50%;
  cursor: pointer;
  background: none;
  outline: none;
}
input[type="color"]::-webkit-color-swatch-wrapper {
  padding: 0;
}
input[type="color"]::-webkit-color-swatch {
  border: 2px solid var(--border-color);
  border-radius: 50%;
  box-shadow: 0 0 15px rgba(255, 255, 255, 0.2);
}
.presets-grid {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
  margin-top: 12px;
}
.preset-btn {
  width: 32px;
  height: 32px;
  border-radius: 50%;
  border: 1px solid rgba(255, 255, 255, 0.2);
  cursor: pointer;
  transition: transform 0.2s ease, box-shadow 0.2s ease;
}
.preset-btn:hover {
  transform: scale(1.15);
  box-shadow: 0 0 10px rgba(255, 255, 255, 0.4);
}
.power-btn-container {
  display: flex;
  justify-content: center;
  margin: 15px 0;
}
.power-btn {
  width: 80px;
  height: 80px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.03);
  border: 2px solid var(--border-color);
  color: var(--text-muted);
  font-size: 1.2rem;
  font-weight: bold;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
  box-shadow: 0 4px 15px rgba(0, 0, 0, 0.2);
}
.power-btn.on {
  color: #fff;
  background: rgba(168, 85, 247, 0.2);
  border-color: var(--accent-purple);
  box-shadow: 0 0 30px var(--accent-glow);
}
.power-btn:hover {
  transform: scale(1.05);
}
.effects-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(120px, 1fr));
  gap: 12px;
}
.effect-card {
  background: rgba(255, 255, 255, 0.02);
  border: 1px solid var(--border-color);
  border-radius: 12px;
  padding: 16px;
  text-align: center;
  cursor: pointer;
  transition: all 0.2s ease;
  user-select: none;
}
.effect-card:hover {
  transform: translateY(-2px);
  background: rgba(255, 255, 255, 0.05);
  border-color: rgba(255, 255, 255, 0.15);
}
.effect-card.active {
  border-color: var(--accent-purple);
  background: rgba(168, 85, 247, 0.15);
  box-shadow: 0 0 15px var(--accent-glow);
}
.effect-card-icon {
  font-size: 1.5rem;
  margin-bottom: 8px;
}
.effect-card-name {
  font-size: 0.85rem;
  font-weight: 500;
}
.form-group {
  margin-bottom: 20px;
}
.form-group label {
  display: block;
  font-size: 0.9rem;
  color: var(--text-muted);
  margin-bottom: 8px;
}
input[type="text"], input[type="password"], select {
  width: 100%;
  padding: 12px;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid var(--border-color);
  border-radius: 10px;
  color: var(--text-color);
  font-family: var(--font-family);
  font-size: 0.95rem;
  transition: all 0.2s ease;
  outline: none;
}
input[type="text"]:focus, input[type="password"]:focus, select:focus {
  border-color: var(--accent-purple);
  box-shadow: 0 0 10px var(--accent-glow);
  background: rgba(255, 255, 255, 0.06);
}
select option {
  background: #141125;
  color: var(--text-color);
}
.btn {
  width: 100%;
  padding: 14px;
  background: linear-gradient(135deg, var(--accent-purple), var(--accent-blue));
  border: none;
  border-radius: 10px;
  color: #fff;
  font-weight: 600;
  font-size: 1rem;
  cursor: pointer;
  transition: all 0.2s ease;
  text-align: center;
}
.btn:hover {
  transform: translateY(-1px);
  box-shadow: 0 5px 15px var(--accent-glow);
}
.btn-danger {
  background: var(--danger-color);
}
.btn-danger:hover {
  box-shadow: 0 5px 15px rgba(239, 68, 68, 0.4);
}
.diag-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 16px;
  margin-bottom: 20px;
}
.diag-item {
  background: rgba(255, 255, 255, 0.02);
  border: 1px solid var(--border-color);
  border-radius: 12px;
  padding: 16px;
}
.diag-val {
  font-family: var(--heading-font);
  font-size: 1.4rem;
  font-weight: 600;
  margin-top: 4px;
}
.diag-label {
  font-size: 0.8rem;
  color: var(--text-muted);
}
pre {
  background: rgba(0, 0, 0, 0.3);
  padding: 16px;
  border-radius: 10px;
  border: 1px solid var(--border-color);
  color: #10b981;
  font-family: monospace;
  font-size: 0.9rem;
  overflow-x: auto;
}
)rawcss";

// --- Embedded Dashboard (Index HTML) ---
const char PAGE_INDEX[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <title>LumaLight Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&family=Outfit:wght@500;600;700&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <header>
    <div class="logo-container">
      <span class="logo-text" id="logo-title">LumaLight</span>
    </div>
    <nav class="nav-menu">
      <a href="/" class="nav-link active">Control</a>
      <a href="/setup" class="nav-link">Wi-Fi Setup</a>
      <a href="/api" class="nav-link">API Explorer</a>
    </nav>
    <div class="status-badge">
      <span class="status-dot disconnected" id="ws-status"></span>
      <span id="ws-status-text">Connecting</span>
    </div>
  </header>
  
  <main>
    <div class="grid-2">
      <div class="card">
        <h2>
          <svg style="width:20px;height:20px;vertical-align:middle;margin-right:6px;" viewBox="0 0 24 24"><path fill="currentColor" d="M12,2A10,10 0 0,0 2,12A10,10 0 0,0 12,22A10,10 0 0,0 22,12A10,10 0 0,0 12,2M12,4A8,8 0 0,1 20,12A8,8 0 0,1 12,20A8,8 0 0,1 4,12A8,8 0 0,1 12,4Z"/></svg>
          Device Controller
        </h2>
        
        <div class="power-btn-container">
          <button class="power-btn" id="power-btn" onclick="togglePower()">OFF</button>
        </div>
        
        <div class="control-group">
          <div class="control-label">
            <span>Brightness</span>
            <span id="brightness-val">80%</span>
          </div>
          <div class="slider-container">
            <input type="range" id="brightness-slider" min="0" max="255" value="80" oninput="sendBrightness(this.value)">
          </div>
        </div>
        
        <div class="control-group">
          <div class="control-label">
            <span>Color Tone</span>
          </div>
          <div class="color-picker-wrapper">
            <input type="color" id="color-picker" value="#a855f7" onchange="sendColorHex(this.value)">
            <div style="flex-grow: 1;">
              <div class="control-label" style="margin-bottom:4px;">
                <span>Color Temp</span>
                <span id="temp-val">4000K</span>
              </div>
              <input type="range" id="temp-slider" min="2000" max="6500" step="100" value="4000" oninput="sendTemp(this.value)">
            </div>
          </div>
        </div>
        
        <div class="control-group">
          <div class="control-label">
            <span>Quick Presets</span>
          </div>
          <div class="presets-grid">
            <button class="preset-btn" style="background: #a855f7;" onclick="setPresetColor(168, 85, 247, '#a855f7')"></button>
            <button class="preset-btn" style="background: #3b82f6;" onclick="setPresetColor(59, 130, 246, '#3b82f6')"></button>
            <button class="preset-btn" style="background: #10b981;" onclick="setPresetColor(16, 185, 129, '#10b981')"></button>
            <button class="preset-btn" style="background: #ef4444;" onclick="setPresetColor(239, 68, 68, '#ef4444')"></button>
            <button class="preset-btn" style="background: #f5af19;" onclick="setPresetColor(245, 175, 25, '#f5af19')"></button>
            <button class="preset-btn" style="background: #ff758c;" onclick="setPresetColor(255, 117, 140, '#ff758c')"></button>
            <button class="preset-btn" style="background: #00f260;" onclick="setPresetColor(0, 242, 96, '#00f260')"></button>
            <button class="preset-btn" style="background: #ffffff;" onclick="setPresetColor(255, 255, 255, '#ffffff')"></button>
          </div>
        </div>
      </div>
      
      <div class="card">
        <h2>
          <svg style="width:20px;height:20px;vertical-align:middle;margin-right:6px;" viewBox="0 0 24 24"><path fill="currentColor" d="M12,2c1.1,0 2,0.9 2,2v2c0,1.1 -0.9,2 -2,2s-2,-0.9 -2,-2V4c0,-1.1 0.9,-2 2,-2m0,14c-2.2,0 -4,-1.8 -4,-4s1.8,-4 4,-4 4,1.8 4,4 -1.8,4 -4,4m0,2c1.1,0 2,0.9 2,2v2c0,1.1 -0.9,2 -2,2s-2,-0.9 -2,-2v-2c0,-1.1 0.9,-2 2,-2M20,10c1.1,0 2,0.9 2,2v2c0,1.1 -0.9,2 -2,2s-2,-0.9 -2,-2v-2c0,-1.1 0.9,-2 2,-2M4,10c1.1,0 2,0.9 2,2v2c0,1.1 -0.9,2 -2,2s-2,-0.9 -2,-2v-2c0,-1.1 0.9,-2 2,-2m14.2,-5.8c0.8,-0.8 2,-0.8 2.8,0s0.8,2 0,2.8l-1.4,1.4c-0.8,0.8 -2,0.8 -2.8,0s-0.8,-2 0,-2.8l1.4,-1.4M5.6,15.6c0.8,-0.8 2,-0.8 2.8,0s0.8,2 0,2.8l-1.4,1.4c-0.8,0.8 -2,0.8 -2.8,0s-0.8,-2 0,-2.8l1.4,-1.4m12.8,0l1.4,1.4c0.8,0.8 0.8,2 0,2.8s-2,0.8 -2.8,0l-1.4,-1.4c-0.8,-0.8 -0.8,-2 0,-2.8s2,-0.8 2.8,0M5.6,8.4l1.4,-1.4c0.8,-0.8 2,-0.8 2.8,0s0.8,2 0,2.8l-1.4,1.4c-0.8,0.8 -2,0.8 -2.8,0s-0.8,-2 0,-2.8"/></svg>
          Lighting Vibe Effects
        </h2>
        
        <div class="effects-grid">
          <div class="effect-card" id="eff-none" onclick="sendEffect('none')">
            <div class="effect-card-icon">🚫</div>
            <div class="effect-card-name">Solid Color</div>
          </div>
          <div class="effect-card" id="eff-rainbow" onclick="sendEffect('rainbow')">
            <div class="effect-card-icon">🌈</div>
            <div class="effect-card-name">Rainbow</div>
          </div>
          <div class="effect-card" id="eff-aurora" onclick="sendEffect('aurora')">
            <div class="effect-card-icon">🌌</div>
            <div class="effect-card-name">Aurora</div>
          </div>
          <div class="effect-card" id="eff-sunset" onclick="sendEffect('sunset')">
            <div class="effect-card-icon">🌇</div>
            <div class="effect-card-name">Sunset</div>
          </div>
          <div class="effect-card" id="eff-ocean" onclick="sendEffect('ocean')">
            <div class="effect-card-icon">🌊</div>
            <div class="effect-card-name">Ocean</div>
          </div>
          <div class="effect-card" id="eff-fire" onclick="sendEffect('fire')">
            <div class="effect-card-icon">🔥</div>
            <div class="effect-card-name">Fire</div>
          </div>
          <div class="effect-card" id="eff-pulse" onclick="sendEffect('pulse')">
            <div class="effect-card-icon">💓</div>
            <div class="effect-card-name">Pulse</div>
          </div>
          <div class="effect-card" id="eff-breathing" onclick="sendEffect('breathing')">
            <div class="effect-card-icon">💨</div>
            <div class="effect-card-name">Breathing</div>
          </div>
          <div class="effect-card" id="eff-relax" onclick="sendEffect('relax')">
            <div class="effect-card-icon">🧘</div>
            <div class="effect-card-name">Relax</div>
          </div>
          <div class="effect-card" id="eff-gaming" onclick="sendEffect('gaming')">
            <div class="effect-card-icon">🎮</div>
            <div class="effect-card-name">Gaming</div>
          </div>
          <div class="effect-card" id="eff-music" onclick="sendEffect('music')">
            <div class="effect-card-icon">🎵</div>
            <div class="effect-card-name">Music Beat</div>
          </div>
        </div>
      </div>
    </div>
  </main>

  <script>
    let ws;
    let wsConnecting = false;
    
    function connectWS() {
      if (wsConnecting) return;
      wsConnecting = true;
      const host = window.location.hostname || '192.168.4.1';
      ws = new WebSocket('ws://' + host + ':81/');
      
      ws.onopen = () => {
        wsConnecting = false;
        document.getElementById('ws-status').className = 'status-dot connected';
        document.getElementById('ws-status-text').innerText = 'Connected';
      };
      
      ws.onclose = () => {
        wsConnecting = false;
        document.getElementById('ws-status').className = 'status-dot disconnected';
        document.getElementById('ws-status-text').innerText = 'Offline (Reconnecting)';
        setTimeout(connectWS, 1500);
      };
      
      ws.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          if (data.event === 'state') {
            updateUI(data);
          }
        } catch(e) {
          console.error(e);
        }
      };
    }
    
    function updateUI(data) {
      const powerBtn = document.getElementById('power-btn');
      if (data.power) {
        powerBtn.className = 'power-btn on';
        powerBtn.innerText = 'ON';
      } else {
        powerBtn.className = 'power-btn';
        powerBtn.innerText = 'OFF';
      }
      
      document.getElementById('brightness-slider').value = data.brightness;
      document.getElementById('brightness-val').innerText = Math.round((data.brightness / 255) * 100) + '%';
      
      if (data.color) {
        document.getElementById('color-picker').value = data.color.hex || '#a855f7';
        document.getElementById('temp-slider').value = data.color.temp || 4000;
        document.getElementById('temp-val').innerText = (data.color.temp || 4000) + 'K';
      }
      
      document.querySelectorAll('.effect-card').forEach(card => card.classList.remove('active'));
      const activeCard = document.getElementById('eff-' + data.effect);
      if (activeCard) {
        activeCard.classList.add('active');
      }
    }
    
    function sendSocket(payload) {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(payload));
      } else {
        let path = '/api/color';
        if (payload.power !== undefined) path = '/api/power';
        else if (payload.effect !== undefined) path = '/api/effect';
        else if (payload.brightness !== undefined) path = '/api/brightness'; // Fallback mapping in server API
        
        fetch(path, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        }).then(res => res.json()).catch(err => console.error(err));
      }
    }
    
    function togglePower() {
      const isCurrentlyOn = document.getElementById('power-btn').classList.contains('on');
      sendSocket({ power: !isCurrentlyOn });
    }
    
    function sendBrightness(val) {
      document.getElementById('brightness-val').innerText = Math.round((val / 255) * 100) + '%';
      sendSocket({ brightness: parseInt(val) });
    }
    
    function sendColorHex(hex) {
      const r = parseInt(hex.slice(1, 3), 16);
      const g = parseInt(hex.slice(3, 5), 16);
      const b = parseInt(hex.slice(5, 7), 16);
      const temp = parseInt(document.getElementById('temp-slider').value);
      sendSocket({ r, g, b, hex, temp });
    }
    
    function sendTemp(val) {
      document.getElementById('temp-val').innerText = val + 'K';
      const hex = document.getElementById('color-picker').value;
      const r = parseInt(hex.slice(1, 3), 16);
      const g = parseInt(hex.slice(3, 5), 16);
      const b = parseInt(hex.slice(5, 7), 16);
      sendSocket({ r, g, b, hex, temp: parseInt(val) });
    }
    
    function setPresetColor(r, g, b, hex) {
      document.getElementById('color-picker').value = hex;
      const temp = parseInt(document.getElementById('temp-slider').value);
      sendSocket({ r, g, b, hex, temp });
    }
    
    function sendEffect(effectName) {
      sendSocket({ effect: effectName });
    }
    
    window.onload = () => {
      connectWS();
      fetch('/api/status')
        .then(res => res.json())
        .then(data => {
          updateUI(data);
          if (data.system && data.system.deviceId) {
             document.getElementById('logo-title').innerText = 'LumaLight (' + data.system.deviceId.replace('LUMA-C6-', '') + ')';
          }
        }).catch(err => console.error(err));
    };
  </script>
</body>
</html>
)rawhtml";

// --- Embedded Wi-Fi Registration Page (Setup HTML) ---
const char PAGE_SETUP[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <title>LumaLight Wi-Fi Setup</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&family=Outfit:wght@500;600;700&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <header>
    <div class="logo-container">
      <span class="logo-text" id="logo-title">LumaLight</span>
    </div>
    <nav class="nav-menu">
      <a href="/" class="nav-link" id="nav-ctrl">Control</a>
      <a href="/setup" class="nav-link active">Wi-Fi Setup</a>
      <a href="/api" class="nav-link">API Explorer</a>
    </nav>
    <div class="status-badge">
      <span class="status-dot disconnected" id="ws-status"></span>
      <span id="ws-status-text">Setup Mode</span>
    </div>
  </header>
  
  <main style="max-width: 500px;">
    <div class="card">
      <h2>Wi-Fi Registration</h2>
      <p style="color: var(--text-muted); margin-bottom: 20px; font-size: 0.9rem;">
        Configure your device's connection settings to attach LumaLight to your home router.
      </p>
      
      <form action="/api/settings" method="POST">
        <div class="form-group">
          <label>Target SSID (Network Name)</label>
          <input type="text" name="ssid" id="ssid" placeholder="Enter WiFi SSID" required>
        </div>
        
        <div class="form-group">
          <label>Password</label>
          <input type="password" name="password" id="password" placeholder="Enter WiFi Password">
        </div>
        
        <div class="form-group">
          <label>Device Friendly Name</label>
          <input type="text" name="name" id="device-name" value="LumaLight">
        </div>
        
        <div class="form-group">
          <label>Location Room</label>
          <select name="room" id="device-room">
            <option value="Living Room">Living Room</option>
            <option value="Bedroom">Bedroom</option>
            <option value="Office">Office</option>
            <option value="Kitchen">Kitchen</option>
            <option value="Studio">Studio</option>
          </select>
        </div>
        
        <button type="submit" class="btn" style="margin-top: 10px;">Save & Apply Settings</button>
      </form>
    </div>
  </main>
  
  <script>
    window.onload = () => {
      fetch('/api/status')
        .then(res => res.json())
        .then(data => {
          if (data.system) {
            document.getElementById('logo-title').innerText = 'LumaLight (' + data.system.deviceId.replace('LUMA-C6-', '') + ')';
            document.getElementById('device-name').value = data.system.name || 'LumaLight';
            document.getElementById('device-room').value = data.system.room || 'Living Room';
            if (data.system.ip === '192.168.4.1') {
              document.getElementById('nav-ctrl').style.display = 'none';
              document.getElementById('ws-status-text').innerText = 'AP Setup Mode';
            } else {
              document.getElementById('ws-status').className = 'status-dot connected';
              document.getElementById('ws-status-text').innerText = 'Connected';
            }
          }
        }).catch(err => console.error(err));
    }
  </script>
</body>
</html>
)rawhtml";

// --- Embedded Developer Explorer (API HTML) ---
const char PAGE_API[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <title>LumaLight API Explorer</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&family=Outfit:wght@500;600;700&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <header>
    <div class="logo-container">
      <span class="logo-text" id="logo-title">LumaLight</span>
    </div>
    <nav class="nav-menu">
      <a href="/" class="nav-link" id="nav-ctrl">Control</a>
      <a href="/setup" class="nav-link">Wi-Fi Setup</a>
      <a href="/api" class="nav-link active">API Explorer</a>
    </nav>
    <div class="status-badge">
      <span class="status-dot disconnected" id="ws-status"></span>
      <span id="ws-status-text">Connected</span>
    </div>
  </header>
  
  <main>
    <div class="grid-2">
      <div class="card">
        <h2>Live System Telemetry</h2>
        <div class="diag-grid">
          <div class="diag-item">
            <div class="diag-label">Core Temp</div>
            <div class="diag-val" id="telemetry-temp">-- °C</div>
          </div>
          <div class="diag-item">
            <div class="diag-label">WiFi Signal</div>
            <div class="diag-val" id="telemetry-rssi">-- dBm</div>
          </div>
          <div class="diag-item">
            <div class="diag-label">Uptime</div>
            <div class="diag-val" id="telemetry-uptime">--s</div>
          </div>
          <div class="diag-item">
            <div class="diag-label">Free RAM</div>
            <div class="diag-val" id="telemetry-heap">-- KB</div>
          </div>
        </div>
        
        <h2>Administrative Tools</h2>
        <div style="display: flex; gap: 12px; flex-direction: column;">
          <button class="btn" onclick="triggerAction('/api/reboot', 'Device will restart, connections will temporarily drop.')">Restart LumaLight</button>
          <button class="btn btn-danger" onclick="triggerAction('/api/factory_reset', 'This will wipe all Wi-Fi settings. The device will restart in AP Setup mode.')">Factory Reset Settings</button>
        </div>
      </div>
      
      <div class="card">
        <h2>API Status Response</h2>
        <p style="color: var(--text-muted); margin-bottom: 12px; font-size: 0.85rem;">
          Query <code>GET /api/status</code> to fetch current lamp state.
        </p>
        <pre id="json-viewer">Loading state...</pre>
        
        <h2 style="margin-top: 24px;">JSON Post Tester</h2>
        <div class="form-group">
          <label>Endpoint</label>
          <select id="api-endpoint">
            <option value="/api/power">POST /api/power</option>
            <option value="/api/color">POST /api/color</option>
            <option value="/api/effect">POST /api/effect</option>
          </select>
        </div>
        <div class="form-group">
          <label>Payload (JSON String)</label>
          <input type="text" id="api-payload" value='{"power": true}'>
        </div>
        <button class="btn" onclick="sendCustomPayload()">Send Request</button>
      </div>
    </div>
  </main>
  
  <script>
    function updateTelemetry() {
      fetch('/api/status')
        .then(res => res.json())
        .then(data => {
          if (data.system) {
            document.getElementById('logo-title').innerText = 'LumaLight (' + data.system.deviceId.replace('LUMA-C6-', '') + ')';
            document.getElementById('telemetry-temp').innerText = parseFloat(data.system.temp).toFixed(1) + ' °C';
            document.getElementById('telemetry-rssi').innerText = data.system.rssi + ' dBm';
            document.getElementById('telemetry-heap').innerText = Math.round(data.system.freeHeap / 1024) + ' KB';
            
            let uptime = data.system.uptime;
            let days = Math.floor(uptime / 86400);
            let hours = Math.floor((uptime % 86400) / 3600);
            let mins = Math.floor((uptime % 3600) / 60);
            let secs = uptime % 60;
            let uptimeStr = '';
            if (days > 0) uptimeStr += days + 'd ';
            if (hours > 0) uptimeStr += hours + 'h ';
            if (mins > 0) uptimeStr += mins + 'm ';
            uptimeStr += secs + 's';
            document.getElementById('telemetry-uptime').innerText = uptimeStr;
            
            document.getElementById('json-viewer').innerText = JSON.stringify(data, null, 2);
            
            if (data.system.ip === '192.168.4.1') {
              document.getElementById('nav-ctrl').style.display = 'none';
              document.getElementById('ws-status').className = 'status-dot disconnected';
              document.getElementById('ws-status-text').innerText = 'AP Setup Mode';
            } else {
              document.getElementById('ws-status').className = 'status-dot connected';
              document.getElementById('ws-status-text').innerText = 'Connected';
            }
          }
        }).catch(err => {
          console.error(err);
          document.getElementById('ws-status').className = 'status-dot disconnected';
          document.getElementById('ws-status-text').innerText = 'Offline';
        });
    }
    
    function triggerAction(path, warning) {
      if (confirm(warning + ' Are you sure?')) {
        fetch(path, { method: 'POST' })
          .then(res => res.json())
          .then(data => {
            alert(data.message || data.error || 'Action submitted successfully!');
            setTimeout(updateTelemetry, 3000);
          })
          .catch(err => alert('Error sending command: ' + err));
      }
    }
    
    function sendCustomPayload() {
      const path = document.getElementById('api-endpoint').value;
      const payloadStr = document.getElementById('api-payload').value;
      
      try {
        const payload = JSON.parse(payloadStr);
        fetch(path, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        })
        .then(res => res.json())
        .then(data => {
          alert('Response: ' + JSON.stringify(data));
          updateTelemetry();
        })
        .catch(err => alert('Request failed: ' + err));
      } catch(e) {
        alert('Invalid JSON in payload field: ' + e);
      }
    }
    
    document.getElementById('api-endpoint').onchange = (e) => {
      const payloadInput = document.getElementById('api-payload');
      if (e.target.value === '/api/power') {
        payloadInput.value = '{"power": true}';
      } else if (e.target.value === '/api/color') {
        payloadInput.value = '{"r": 168, "g": 85, "b": 247, "hex": "#a855f7", "temp": 4000}';
      } else if (e.target.value === '/api/effect') {
        payloadInput.value = '{"effect": "rainbow"}';
      }
    };
    
    window.onload = () => {
      updateTelemetry();
      setInterval(updateTelemetry, 5000);
    }
  </script>
</body>
</html>
)rawhtml";

// --- Setup ---
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[SYSTEM] LumaLamp Initializing...");

  // Initialize LEDs
  pixels.begin();
  pixels.show(); // Turn off all pixels initially

  // Load preferences from ESP NVS
  loadConfiguration();

  // Show startup light indicator (Blue fade)
  for (int i = 0; i < NUM_LEDS; i++) {
    pixels.setPixelColor(i, pixels.Color(0, 0, 50));
  }
  pixels.setBrightness(120);
  pixels.show();
  delay(500);

  // Initialize WiFi (STA with AP captive fallback)
  initWiFi();

  // Setup servers
  setupWebServer();
  server.begin(); // Start the local WebServer!
  webSocket.begin();
  webSocket.onEvent(
      [](uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
        if (type == WStype_CONNECTED) {
          Serial.printf("[WS] Client #%u connected\n", num);
          broadcastState();
        } else if (type == WStype_TEXT) {
          handleLocalWebSocketMessage(num, payload, length);
        }
      });

  Serial.println("[SYSTEM] Ready.");
}

// --- Loop ---
void loop() {
  // Handle DNS requests in Captive AP mode
  if (isApMode) {
    dnsServer.processNextRequest();
  }

  // Handle client updates
  server.handleClient();
  webSocket.loop();

  if (WiFi.status() == WL_CONNECTED) {
    webSocketClient.loop();
  }

  // LED animation loop (non-blocking ticker)
  if (power) {
    updateLEDs();
  } else {
    // Fade to Black
    pixels.clear();
    pixels.show();
  }

  // Read internal temperature sensor
#ifdef SOC_TEMP_SENSOR_SUPPORTED
  deviceTemp = temperatureRead();
#else
  // Sim drift if not supported natively on board variant
  deviceTemp =
      36.0 + (sin(millis() / 50000.0) * 1.5) + (random(-10, 10) / 100.0);
#endif

  // Broadcast state logs periodically (every 10 seconds)
  if (millis() - lastStateBroadcast > 10000) {
    broadcastState();
    lastStateBroadcast = millis();
  }
}

// --- REST API & Server routes ---
void setupWebServer() {
  // CORS Headers for API accessibility during local Vite UI debugging
  server.enableCORS(true);

  // CSS Route
  server.on("/style.css", HTTP_GET,
            []() { server.send_P(200, "text/css", PAGE_CSS); });

  // Main Page Route
  server.on("/", HTTP_GET,
            []() { server.send_P(200, "text/html", PAGE_INDEX); });

  // Setup Route
  server.on("/setup", HTTP_GET,
            []() { server.send_P(200, "text/html", PAGE_SETUP); });

  // API Explorer Route
  server.on("/api", HTTP_GET,
            []() { server.send_P(200, "text/html", PAGE_API); });

  // Captive Portal Redirect logic
  server.onNotFound([]() {
    if (isApMode) {
      // Redirect all requests to standard captive setup portal IP
      server.sendHeader("Location", "http://192.168.4.1/setup", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "application/json", "{\"error\":\"Not Found\"}");
    }
  });

  // API status
  server.on("/api/status", HTTP_GET, []() {
    JsonDocument doc;
    doc["power"] = power;
    doc["brightness"] = brightness;

    JsonObject colorObj = doc["color"].to<JsonObject>();
    colorObj["r"] = lampColor.r;
    colorObj["g"] = lampColor.g;
    colorObj["b"] = lampColor.b;
    colorObj["hex"] = lampColor.hex;
    colorObj["temp"] = lampColor.temp;

    doc["effect"] = activeEffect;

    JsonObject sysObj = doc["system"].to<JsonObject>();
    sysObj["temp"] = deviceTemp;
    sysObj["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
    sysObj["uptime"] = millis() / 1000;
    sysObj["deviceId"] = String("LUMA-C6-") + deviceSuffix;
    sysObj["name"] = lampName;
    sysObj["room"] = roomName;
    sysObj["freeHeap"] = ESP.getFreeHeap();
    sysObj["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString()
                                                   : WiFi.softAPIP().toString();

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  // Power Route
  server.on("/api/power", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      JsonDocument doc;
      deserializeJson(doc, server.arg("plain"));
      if (doc.containsKey("power")) {
        power = doc["power"];
        server.send(200, "application/json", "{\"success\":true}");
        broadcastState();
        return;
      }
    }
    server.send(400, "application/json", "{\"error\":\"Missing payload\"}");
  });

  // Color Route
  server.on("/api/color", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      JsonDocument doc;
      deserializeJson(doc, server.arg("plain"));
      if (doc.containsKey("r") && doc.containsKey("g") &&
          doc.containsKey("b")) {
        lampColor.r = doc["r"];
        lampColor.g = doc["g"];
        lampColor.b = doc["b"];
        if (doc.containsKey("hex")) {
          strncpy(lampColor.hex, doc["hex"], sizeof(lampColor.hex) - 1);
        }
        if (doc.containsKey("temp")) {
          lampColor.temp = doc["temp"];
        }
        activeEffect = "none"; // Stop animation
        server.send(200, "application/json", "{\"success\":true}");
        broadcastState();
        return;
      }
    }
    server.send(400, "application/json", "{\"error\":\"Invalid payload\"}");
  });

  // Effect Route
  server.on("/api/effect", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      JsonDocument doc;
      deserializeJson(doc, server.arg("plain"));
      if (doc.containsKey("effect")) {
        activeEffect = doc["effect"].as<String>();
        server.send(200, "application/json", "{\"success\":true}");
        broadcastState();
        return;
      }
    }
    server.send(400, "application/json", "{\"error\":\"Missing effect\"}");
  });

  // Settings / Wi-Fi saving Route
  server.on("/api/settings", HTTP_POST, []() {
    String ssid = "";
    String pass = "";

    // Support both Form POST (for captive setup) and JSON POST (from React app)
    if (server.hasArg("ssid")) {
      ssid = server.arg("ssid");
      pass = server.arg("password");
      if (server.hasArg("name"))
        lampName = server.arg("name");
      if (server.hasArg("room"))
        roomName = server.arg("room");
    } else if (server.hasArg("plain")) {
      JsonDocument doc;
      deserializeJson(doc, server.arg("plain"));
      if (doc.containsKey("ssid")) {
        ssid = doc["ssid"].as<String>();
        pass = doc["password"].as<String>();
        if (doc.containsKey("name"))
          lampName = doc["name"].as<String>();
        if (doc.containsKey("room"))
          roomName = doc["room"].as<String>();
      }
    }

    if (ssid != "") {
      wifiSsid = ssid;
      wifiPassword = pass;
      saveConfiguration();

      server.send(
          200, "text/html",
          "<html><head><meta http-equiv='refresh' "
          "content='5;url=http://"
          "lumalight.local'><style>body{background:#0b0818;color:#fff;font-"
          "family:sans-serif;text-align:center;padding:50px;}</style></"
          "head><body><h2>Credentials Saved.</h2><p>Rebooting LumaLight and "
          "connecting to Wi-Fi... Please wait 5 seconds.</p></body></html>");
      delay(2000);
      ESP.restart();
    } else {
      server.send(400, "application/json",
                  "{\"error\":\"SSID cannot be empty\"}");
    }
  });

  // Reboot Device
  server.on("/api/reboot", HTTP_POST, []() {
    server.send(200, "application/json",
                "{\"message\":\"Rebooting device...\"}");
    delay(1500);
    ESP.restart();
  });

  // Factory Reset
  server.on("/api/factory_reset", HTTP_POST, []() {
    server.send(
        200, "application/json",
        "{\"message\":\"Factory reset initiated. Device wiping NVS...\"}");
    preferences.begin("nvs", false);
    preferences.clear();
    preferences.end();
    delay(2000);
    ESP.restart();
  });
}

// --- Local WebSocket Message Handler ---
void handleLocalWebSocketMessage(uint8_t num, uint8_t *payload, size_t length) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.println("[WS Local] JSON deserialization failed");
    return;
  }

  bool stateChanged = false;

  if (doc.containsKey("power")) {
    power = doc["power"];
    stateChanged = true;
  }
  if (doc.containsKey("brightness")) {
    brightness = doc["brightness"];
    stateChanged = true;
  }
  if (doc.containsKey("r") && doc.containsKey("g") && doc.containsKey("b")) {
    lampColor.r = doc["r"];
    lampColor.g = doc["g"];
    lampColor.b = doc["b"];
    if (doc.containsKey("hex")) {
      strncpy(lampColor.hex, doc["hex"], sizeof(lampColor.hex) - 1);
    }
    if (doc.containsKey("temp")) {
      lampColor.temp = doc["temp"];
    }
    activeEffect = "none";
    stateChanged = true;
  }
  if (doc.containsKey("effect")) {
    activeEffect = doc["effect"].as<String>();
    stateChanged = true;
  }

  if (stateChanged) {
    broadcastState();
  }
}

// --- Cloud Relay WebSocket Client Helpers ---
String getMacSuffix() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[7];
  sprintf(buf, "%02X%02X%02X", (uint8_t)(mac >> 16), (uint8_t)(mac >> 8),
          (uint8_t)mac);
  return String(buf);
}

void sendStateToCloud() {
  if (WiFi.status() == WL_CONNECTED && cloudWsConnected) {
    JsonDocument doc;
    doc["event"] = "state";
    doc["power"] = power;
    doc["brightness"] = brightness;

    JsonObject colorObj = doc["color"].to<JsonObject>();
    colorObj["r"] = lampColor.r;
    colorObj["g"] = lampColor.g;
    colorObj["b"] = lampColor.b;
    colorObj["hex"] = lampColor.hex;
    colorObj["temp"] = lampColor.temp;

    doc["effect"] = activeEffect;

    JsonObject sysObj = doc["system"].to<JsonObject>();
    sysObj["temp"] = deviceTemp;
    sysObj["rssi"] = WiFi.RSSI();
    sysObj["uptime"] = millis() / 1000;

    String response;
    serializeJson(doc, response);
    webSocketClient.sendTXT(response);
  }
}

void handleCloudMessage(char *payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.println("[WS Client] JSON deserialization failed");
    return;
  }

  String path = doc["path"].as<String>();
  Serial.printf("[WS Client] Cloud command path: %s\n", path.c_str());

  if (path == "/api/power") {
    if (doc.containsKey("power")) {
      power = doc["power"];
      if (doc.containsKey("brightness")) {
        brightness = doc["brightness"];
      }
      broadcastState();
    }
  } else if (path == "/api/color") {
    if (doc.containsKey("r") && doc.containsKey("g") && doc.containsKey("b")) {
      lampColor.r = doc["r"];
      lampColor.g = doc["g"];
      lampColor.b = doc["b"];
      if (doc.containsKey("hex")) {
        String h = doc["hex"];
        strncpy(lampColor.hex, h.c_str(), sizeof(lampColor.hex) - 1);
      }
      if (doc.containsKey("temp")) {
        lampColor.temp = doc["temp"];
      }
      activeEffect = "none";
      broadcastState();
    }
  } else if (path == "/api/effect") {
    if (doc.containsKey("effect")) {
      activeEffect = doc["effect"].as<String>();
      broadcastState();
    }
  }
}

void webSocketClientEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
  case WStype_DISCONNECTED:
    Serial.println("[WS Client] Disconnected from Cloud Relay");
    cloudWsConnected = false;
    break;
  case WStype_CONNECTED:
    Serial.println("[WS Client] Connected to Cloud Relay");
    cloudWsConnected = true;
    sendStateToCloud();
    break;
  case WStype_TEXT:
    Serial.printf("[WS Client] Message received: %s\n", payload);
    handleCloudMessage((char *)payload);
    break;
  case WStype_BIN:
    break;
  }
}

void initCloudWebSocket() {
  if (WiFi.status() != WL_CONNECTED)
    return;

  String deviceId = "LUMA-C6-" + deviceSuffix;
  Serial.printf("[WS Client] Connecting: %s\n", deviceId.c_str());

  secureClient.setInsecure(); // Disable SSL validation for simplicity

  webSocketClient.beginSSL("api.siddhart.qzz.io", 443,
                           ("/ws/relay?deviceId=" + deviceId).c_str());
  webSocketClient.onEvent(webSocketClientEvent);
  webSocketClient.setReconnectInterval(5000);
}

// --- WebSocket Broadcast ---
void broadcastState() {
  JsonDocument doc;
  doc["event"] = "state";
  doc["power"] = power;
  doc["brightness"] = brightness;

  JsonObject colorObj = doc["color"].to<JsonObject>();
  colorObj["r"] = lampColor.r;
  colorObj["g"] = lampColor.g;
  colorObj["b"] = lampColor.b;
  colorObj["hex"] = lampColor.hex;
  colorObj["temp"] = lampColor.temp;

  doc["effect"] = activeEffect;

  JsonObject sysObj = doc["system"].to<JsonObject>();
  sysObj["temp"] = deviceTemp;
  sysObj["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  sysObj["uptime"] = millis() / 1000;

  String text;
  serializeJson(doc, text);
  webSocket.broadcastTXT(text);

  // Send state update to Cloud Relay
  if (WiFi.status() == WL_CONNECTED && cloudWsConnected) {
    webSocketClient.sendTXT(text);
  }
}

// --- WiFi Setup Logic ---
void initWiFi() {
  if (wifiSsid == "") {
    isApMode = true;
  } else {
    Serial.printf("[WIFI] Connecting to SSID: %s\n", wifiSsid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

    // Wait up to 15 seconds for connection
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 30) {
      delay(500);
      Serial.print(".");
      retries++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[WIFI] Connected. IP: ");
      Serial.println(WiFi.localIP());
      isApMode = false;

      // Start mDNS
      if (MDNS.begin("lumalight")) {
        Serial.println("[MDNS] responder started: http://lumalight.local");
        MDNS.addService("http", "tcp", 80);
      } else {
        Serial.println("[MDNS] Error setting up MDNS responder!");
      }

      initCloudWebSocket(); // Start remote connection
      return;
    } else {
      Serial.println("[WIFI] Failed to connect. Falling back to AP Mode.");
      isApMode = true;
    }
  }

  // AP Portal setup
  if (isApMode) {
    WiFi.mode(WIFI_AP);
    String apName = "LumaLight-" + deviceSuffix;
    WiFi.softAP(apName.c_str());
    Serial.printf("[WIFI] Hotspot active: %s\n", apName.c_str());
    Serial.print("[WIFI] AP Portal IP: ");
    Serial.println(WiFi.softAPIP());

    // Set up DNS redirect to catch captive requests
    dnsServer.start(53, "*", WiFi.softAPIP());
  }
}

// --- NVS Storage ---
void loadConfiguration() {
  preferences.begin("nvs", false);
  deviceSuffix = preferences.getString("suffix", "");

  if (deviceSuffix == "") {
    uint64_t mac = ESP.getEfuseMac();
    uint32_t seed = (uint32_t)(mac & 0xFFFFFFFF) ^ (uint32_t)(mac >> 32);
    randomSeed(seed);

    const char *ADJECTIVES[] = {
        "Bright", "Glowing", "Cozy",      "Radiant",  "Vibrant", "Luminous",
        "Serene", "Dreamy",  "Magic",     "Charming", "Shining", "Soft",
        "Aura",   "Golden",  "Neon",      "Cosmic",   "Warm",    "Mystic",
        "Sunny",  "Zen",     "Sparkling", "Flashed",  "Sunset",  "Aurora",
        "Ember",  "Prism",   "Halo",      "Stellar",  "Solar",   "Lunar"};
    int numAdjectives = sizeof(ADJECTIVES) / sizeof(ADJECTIVES[0]);
    int adjIndex = random(0, numAdjectives);
    int num = random(1000, 10000);

    deviceSuffix = String(ADJECTIVES[adjIndex]) + "-" + String(num);
    preferences.putString("suffix", deviceSuffix);
    Serial.printf("[SYSTEM] Generated unique device suffix: %s\n",
                  deviceSuffix.c_str());
  } else {
    Serial.printf("[SYSTEM] Loaded existing device suffix: %s\n",
                  deviceSuffix.c_str());
  }

  wifiSsid = preferences.getString("ssid", "");
  wifiPassword = preferences.getString("password", "");

  String savedName = preferences.getString("name", "");
  if (savedName == "" || savedName == "LumaLamp-C6") {
    lampName = "LumaLight-" + deviceSuffix;
    preferences.putString("name", lampName);
  } else {
    lampName = savedName;
  }

  roomName = preferences.getString("room", "Living Room");
  power = preferences.getBool("power", true);
  brightness = preferences.getUChar("bright", 80);
  lampColor.r = preferences.getUChar("r", 168);
  lampColor.g = preferences.getUChar("g", 85);
  lampColor.b = preferences.getUChar("b", 247);
  lampColor.temp = preferences.getUShort("temp", 4000);
  activeEffect = preferences.getString("effect", "rainbow");

  // Clean up Hex representation of color if needed
  String hexStr = preferences.getString("hex", "");
  if (hexStr != "") {
    strncpy(lampColor.hex, hexStr.c_str(), sizeof(lampColor.hex) - 1);
  }

  preferences.end();
}

void saveConfiguration() {
  preferences.begin("nvs", false);
  preferences.putString("ssid", wifiSsid);
  preferences.putString("password", wifiPassword);
  preferences.putString("name", lampName);
  preferences.putString("room", roomName);
  preferences.putBool("power", power);
  preferences.putUChar("bright", brightness);
  preferences.putUChar("r", lampColor.r);
  preferences.putUChar("g", lampColor.g);
  preferences.putUChar("b", lampColor.b);
  preferences.putString("hex", String(lampColor.hex));
  preferences.putUShort("temp", lampColor.temp);
  preferences.putString("effect", activeEffect);
  preferences.end();
}

// --- WRGB LED Splitter and Drive ---
void setRGBW(int i, uint8_t r, uint8_t g, uint8_t b, uint16_t temp) {
  // Since we are using an RGB LED, we do not have a physical white channel.
  // Drive R, G, B channels directly.
  pixels.setPixelColor(i, pixels.Color(r, g, b));
}

void updateLEDs() {
  pixels.setBrightness(brightness);

  if (activeEffect == "none") {
    handleEffectNone();
  } else if (activeEffect == "rainbow") {
    handleEffectRainbow();
  } else if (activeEffect == "aurora") {
    handleEffectAurora();
  } else if (activeEffect == "sunset") {
    handleEffectSunset();
  } else if (activeEffect == "ocean") {
    handleEffectOcean();
  } else if (activeEffect == "fire") {
    handleEffectFire();
  } else if (activeEffect == "pulse") {
    handleEffectPulse();
  } else if (activeEffect == "breathing") {
    handleEffectBreathing();
  } else if (activeEffect == "relax") {
    handleEffectRelax();
  } else if (activeEffect == "gaming") {
    handleEffectGaming();
  } else if (activeEffect == "music") {
    handleEffectMusic();
  }
}

// --- LED Effects ---
void handleEffectNone() {
  for (int i = 0; i < NUM_LEDS; i++) {
    setRGBW(i, lampColor.r, lampColor.g, lampColor.b, lampColor.temp);
  }
  pixels.show();
}

void handleEffectRainbow() {
  if (millis() - lastEffectUpdate < 30)
    return;
  lastEffectUpdate = millis();

  static uint16_t j = 0;
  for (int i = 0; i < NUM_LEDS; i++) {
    pixels.setPixelColor(i,
                         pixels.ColorHSV(((i * 65536 / NUM_LEDS) + j) & 65535));
  }
  pixels.show();
  j += 256;
}

void handleEffectAurora() {
  if (millis() - lastEffectUpdate < 80)
    return;
  lastEffectUpdate = millis();

  static float phase = 0.0;
  phase += 0.04;
  for (int i = 0; i < NUM_LEDS; i++) {
    float angle = (i * 2 * PI / NUM_LEDS) + phase;
    uint8_t r = (uint8_t)(30 + sin(angle) * 20);
    uint8_t g = (uint8_t)(150 + sin(angle * 1.5) * 50);
    uint8_t b = (uint8_t)(200 + cos(angle) * 40);
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

void handleEffectSunset() {
  if (millis() - lastEffectUpdate < 60)
    return;
  lastEffectUpdate = millis();

  static float phase = 0.0;
  phase += 0.03;
  for (int i = 0; i < NUM_LEDS; i++) {
    float angle = (i * PI / NUM_LEDS) + phase;
    uint8_t r = 240;
    uint8_t g = (uint8_t)(60 + sin(angle) * 50);
    uint8_t b = (uint8_t)(40 + cos(angle * 0.8) * 30);
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

void handleEffectOcean() {
  if (millis() - lastEffectUpdate < 70)
    return;
  lastEffectUpdate = millis();

  static float phase = 0.0;
  phase += 0.02;
  for (int i = 0; i < NUM_LEDS; i++) {
    float angle = (i * 2 * PI / NUM_LEDS) + phase;
    uint8_t r = 0;
    uint8_t g = (uint8_t)(100 + sin(angle) * 60);
    uint8_t b = (uint8_t)(200 + cos(angle * 1.2) * 50);
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

void handleEffectFire() {
  if (millis() - lastEffectUpdate < 100)
    return;
  lastEffectUpdate = millis();

  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t flicker = random(0, 80);
    uint8_t r = 220 - flicker;
    uint8_t g = 70 - (flicker / 2);
    uint8_t b = 0;
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

void handleEffectPulse() {
  if (millis() - lastEffectUpdate < 40)
    return;
  lastEffectUpdate = millis();

  static float phase = 0.0;
  phase += 0.15;
  uint8_t val = (uint8_t)(127 + sin(phase) * 120);
  pixels.setBrightness((val * brightness) / 255);
  for (int i = 0; i < NUM_LEDS; i++) {
    setRGBW(i, lampColor.r, lampColor.g, lampColor.b, lampColor.temp);
  }
  pixels.show();
}

void handleEffectBreathing() {
  if (millis() - lastEffectUpdate < 50)
    return;
  lastEffectUpdate = millis();

  static float phase = 0.0;
  phase += 0.04;
  uint8_t val = (uint8_t)(60 + sin(phase) * 55);
  pixels.setBrightness((val * brightness) / 100);
  for (int i = 0; i < NUM_LEDS; i++) {
    setRGBW(i, lampColor.r, lampColor.g, lampColor.b, lampColor.temp);
  }
  pixels.show();
}

void handleEffectRelax() {
  for (int i = 0; i < NUM_LEDS; i++) {
    pixels.setPixelColor(i, pixels.Color(255, 140, 45)); // Warm gold
  }
  pixels.show();
}

void handleEffectGaming() {
  if (millis() - lastEffectUpdate < 50)
    return;
  lastEffectUpdate = millis();

  static uint16_t hue = 0;
  for (int i = 0; i < NUM_LEDS; i++) {
    pixels.setPixelColor(i, pixels.ColorHSV(hue + (i * 2000)));
  }
  pixels.show();
  hue += 1200;
}

void handleEffectMusic() {
  if (millis() - lastEffectUpdate < 60)
    return;
  lastEffectUpdate = millis();

  bool beat = random(0, 100) > 85;
  if (beat) {
    uint32_t randomColor =
        pixels.Color(random(0, 255), random(0, 255), random(0, 255));
    for (int i = 0; i < NUM_LEDS; i++) {
      pixels.setPixelColor(i, randomColor);
    }
  } else {
    // Fade down
    for (int i = 0; i < NUM_LEDS; i++) {
      uint32_t color = pixels.getPixelColor(i);
      // In NEO_GRB, getPixelColor returns a 32-bit integer formatted as
      // 0x00RRGGBB. We retrieve R, G, B accordingly.
      uint8_t r = (color >> 16) & 0xFF;
      uint8_t g = (color >> 8) & 0xFF;
      uint8_t b = color & 0xFF;
      pixels.setPixelColor(i, pixels.Color(r * 0.85, g * 0.85, b * 0.85));
    }
  }
  pixels.show();
}
