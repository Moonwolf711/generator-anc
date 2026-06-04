// esp12e_dashboard.ino -- WiFi telemetry dashboard for generator-anc.
//
// Runs on the onboard ESP-12E (ESP8266) of the BurgessWorld Arduino-Teensy4 carrier.
// The Teensy streams a CSV telemetry line over the board's hardware-serial bridge:
//     ANC,f0,rpm,o1,o2,o3,o4,o5,o6,cpu,lock
// This sketch parses it and serves a live web dashboard (RPM, per-order effort bars,
// tach-lock, CPU). The ESP makes its own WiFi access point -- connect a phone to it
// and browse to http://192.168.4.1
//
// Flashing the ESP on this board: hold ESP-PGM, tap reset, release ESP-PGM to enter
// the ESP8266 bootloader, then upload (Board: "Generic ESP8266 Module" / NodeMCU).
// The Teensy<->ESP link is the same hardware serial the Teensy prints telemetry to.

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

const char* AP_SSID = "generator-anc";
const char* AP_PASS = "";              // open AP; set a password if you want

ESP8266WebServer server(80);

float    g_f0 = 0, g_rpm = 0, g_cpu = 0;
float    g_ord[6] = {0,0,0,0,0,0};
int      g_lock = 0;
uint32_t g_last = 0;
char     buf[160];
int      buflen = 0;

void parseLine(const char* s) {
    // expects: ANC,f0,rpm,o1..o6,cpu,lock
    if (strncmp(s, "ANC,", 4) != 0) return;
    float v[10]; int n = 0;
    const char* p = s + 4;
    while (n < 10 && *p) {
        v[n++] = atof(p);
        const char* c = strchr(p, ',');
        if (!c) break;
        p = c + 1;
    }
    if (n >= 10) {
        g_f0 = v[0]; g_rpm = v[1];
        for (int i = 0; i < 6; ++i) g_ord[i] = v[2 + i];
        g_cpu = v[8]; g_lock = (int)v[9];
        g_last = millis();
    }
}

String dataJson() {
    String j = "{\"f0\":" + String(g_f0,1) + ",\"rpm\":" + String(g_rpm,0) +
               ",\"cpu\":" + String(g_cpu,1) + ",\"lock\":" + String(g_lock) +
               ",\"age\":" + String((millis()-g_last)/1000.0,1) + ",\"ord\":[";
    for (int i = 0; i < 6; ++i) { j += String(g_ord[i],3); if (i<5) j += ","; }
    j += "]}";
    return j;
}

const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1">
<title>generator-anc</title><style>
body{background:#0d1b2a;color:#e0e1dd;font-family:system-ui;margin:0;padding:18px}
h1{color:#4cc9f0;font-size:20px;margin:0 0 12px}
.rpm{font-size:54px;color:#ffb703;font-weight:700}.sub{color:#8ecae6}
.bar{height:22px;background:#1b2a3a;border-radius:5px;margin:6px 0;overflow:hidden}
.fill{height:100%;background:linear-gradient(90deg,#4cc9f0,#ffb703)}
.lock{display:inline-block;padding:3px 10px;border-radius:6px;font-weight:700}
.on{background:#2a9d4a;color:#fff}.off{background:#7a2a2a;color:#fff}
.row{display:flex;align-items:center;gap:10px;margin:4px 0}.lab{width:90px;color:#8ecae6;font-size:13px}
</style></head><body>
<h1>generator-anc &mdash; live</h1>
<div class=rpm id=rpm>--</div><div class=sub>engine RPM &nbsp; f0=<span id=f0>--</span> Hz</div>
<div style="margin:10px 0"><span id=lk class="lock off">NO TACH</span>
&nbsp; CPU <span id=cpu>--</span>% &nbsp; <span class=sub id=age></span></div>
<div id=bars></div>
<script>
const HZ=[30,60,90,120,150,180];
async function tick(){try{let d=await(await fetch('/data')).json();
rpm.textContent=Math.round(d.rpm);f0.textContent=d.f0.toFixed(1);cpu.textContent=d.cpu;
lk.textContent=d.lock?'TACH LOCK':'NO TACH';lk.className='lock '+(d.lock?'on':'off');
age.textContent=d.age>2?('stale '+d.age+'s'):'';
let mx=Math.max(0.01,...d.ord);let h='';
for(let i=0;i<6;i++){let pct=Math.min(100,100*d.ord[i]/mx);
h+=`<div class=row><div class=lab>ord ${i+1}<br>${HZ[i]}Hz</div><div class=bar style=flex:1><div class=fill style=width:${pct}%></div></div></div>`}
bars.innerHTML=h;}catch(e){}}
setInterval(tick,400);tick();
</script></body></html>
)HTML";

void setup() {
    Serial.begin(115200);                 // <- Teensy telemetry comes in here
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    server.on("/", []() { server.send_P(200, "text/html", PAGE); });
    server.on("/data", []() { server.send(200, "application/json", dataJson()); });
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
