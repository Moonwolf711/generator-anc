// esp12e_dashboard.ino -- WiFi cockpit for generator-anc (ESP-12E SoftAP).
//
// Runs on the onboard ESP-12E (ESP8266) of the BurgessWorld Arduino-Teensy4 carrier.
// The Teensy streams telemetry over the board's hardware-serial bridge:
//     ANC,f0,rpm,o1,o2,o3,o4,o5,o6,cpu,lock,mu,orders,gain,mode
// This serves a self-contained control page (page_html.h, generated from live.html by
// tools/embed_page.py). The page polls /data; its sliders/buttons hit /set and /cmd,
// which forward commands BACK to the Teensy over the same serial link (full-duplex).
//
// Connect a phone to the "generator-anc" WiFi and browse to http://192.168.4.1
// No internet needed (SoftAP) -- that's why the page carries zero CDN dependencies.
//
// Flashing the ESP on this board: hold ESP-PGM, tap reset, release ESP-PGM to enter the
// ESP8266 bootloader, then upload (Board: "Generic ESP8266 Module" / NodeMCU).

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "page_html.h"   // const char PAGE[] PROGMEM = R"HTML( ... )HTML";  (generated)

const char* AP_SSID = "generator-anc";
const char* AP_PASS = "";              // open AP; set a password if you want

ESP8266WebServer server(80);

// MUST MATCH NUM_ORDERS in generator_anc_teensy.ino -- the CSV telemetry protocol is coupled to it.
static constexpr int NUM_ORDERS = 6;
float    g_f0 = 0, g_rpm = 0, g_cpu = 0, g_mu = 0, g_gain = 0;
float    g_ord[NUM_ORDERS]  = {0};
float    g_smag[NUM_ORDERS] = {0};
int      g_lock = 0, g_orders = NUM_ORDERS, g_mode = 0, g_cal = 0;
uint32_t g_last = 0;
char     buf[200];
int      buflen = 0;

const char* modeStr(int m) {
    switch (m) {
        case 1:  return "calibrating";
        case 2:  return "stopped";
        case 3:  return "running";
        case 4:  return "S-FAIL";
        default: return "idle";
    }
}

void parseLine(const char* s) {
    // ANC,f0,rpm,o1..o6,cpu,lock[,mu,orders,gain,mode,cal,s1..s6]
    if (strncmp(s, "ANC,", 4) != 0) return;
    constexpr int N = NUM_ORDERS;
    constexpr int MAXF = 9 + 2 * N;            // f0,rpm,o*N,cpu,lock,mu,orders,gain,mode,cal,s*N
    float v[MAXF]; int n = 0;
    const char* p = s + 4;
    while (n < MAXF && *p) {
        v[n++] = atof(p);
        const char* c = strchr(p, ',');
        if (!c) break;
        p = c + 1;
    }
    if (n >= 4 + N) {                          // need at least f0,rpm,o*N,cpu,lock
        g_f0 = v[0]; g_rpm = v[1];
        for (int i = 0; i < N; ++i) g_ord[i] = v[2 + i];
        g_cpu = v[2 + N]; g_lock = (int)v[3 + N];
        if (n >= 8 + N) { g_mu = v[4 + N]; g_orders = (int)v[5 + N]; g_gain = v[6 + N]; g_mode = (int)v[7 + N]; }
        if (n >= MAXF)  { g_cal = (int)v[8 + N]; for (int i = 0; i < N; ++i) g_smag[i] = v[9 + N + i]; }
        g_last = millis();
    }
}

String dataJson() {
    String j = "{\"f0\":" + String(g_f0,1) + ",\"rpm\":" + String(g_rpm,0) +
               ",\"cpu\":" + String(g_cpu,1) + ",\"lock\":" + String(g_lock) +
               ",\"age\":" + String((millis()-g_last)/1000.0,1) +
               ",\"mu\":" + String(g_mu,4) + ",\"orders\":" + String(g_orders) +
               ",\"gain\":" + String(g_gain,2) + ",\"mode\":\"" + modeStr(g_mode) +
               "\",\"cal\":" + String(g_cal) + ",\"ord\":[";
    for (int i = 0; i < NUM_ORDERS; ++i) { j += String(g_ord[i],3); if (i < NUM_ORDERS - 1) j += ","; }
    j += "],\"smag\":[";
    for (int i = 0; i < NUM_ORDERS; ++i) { j += String(g_smag[i],3); if (i < NUM_ORDERS - 1) j += ","; }
    j += "]}";
    return j;
}

// /set?p=<mu|orders|gain>&v=<value> -> forward "SET <p> <v>" to the Teensy
void handleSet() {
    String p = server.arg("p"), v = server.arg("v");
    if (p.length() && v.length()) { Serial.print("SET "); Serial.print(p); Serial.print(' '); Serial.println(v); }
    server.send(200, "text/plain", "ok");
}

// /cmd?c=<c|r|s|?> -> forward the single command char to the Teensy
void handleCmd() {
    String c = server.arg("c");
    if (c.length()) Serial.println(c);
    server.send(200, "text/plain", "ok");
}

void setup() {
    Serial.begin(115200);                 // <- Teensy telemetry in, commands out (same UART)
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    server.on("/",     []() { server.send_P(200, "text/html", PAGE); });
    server.on("/data", []() { server.send(200, "application/json", dataJson()); });
    server.on("/set",  handleSet);
    server.on("/cmd",  handleCmd);
    server.begin();
}

void loop() {
    server.handleClient();
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || buflen >= (int)sizeof(buf) - 1) {
            buf[buflen] = 0;
            parseLine(buf);
            buflen = 0;
        } else if (c != '\r') {
            buf[buflen++] = c;
        }
    }
}
