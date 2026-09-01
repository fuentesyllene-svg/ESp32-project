#include "webui.h"
#include "config.h"
#include "datalog.h"

#include <WebServer.h>
#include <LittleFS.h>

namespace {
WebServer server(80);
webui::StatusProvider status_ = nullptr;
webui::CommandHandler command_ = nullptr;

// Kept deliberately small and dependency-free: no CDN, no framework. The unit
// is expected to be reached from a phone standing in a lettuce plot with no
// internet, so everything the page needs is in the flash of the ESP32.
const char kIndex[] PROGMEM = R"HTML(<!doctype html><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Pest Trap</title>
<style>
body{font:14px system-ui,sans-serif;margin:0;background:#101418;color:#e8edf2}
header{padding:14px 16px;background:#182029;border-bottom:1px solid #263241}
h1{font-size:16px;margin:0}
main{padding:12px 16px;max-width:640px}
.card{background:#182029;border:1px solid #263241;border-radius:8px;padding:12px;margin:0 0 12px}
.row{display:flex;justify-content:space-between;padding:4px 0;border-bottom:1px solid #212b36}
.row:last-child{border:0}
.k{color:#93a4b5}
.v{font-variant-numeric:tabular-nums}
.state{font-weight:600}
button{background:#2a5c8f;color:#fff;border:0;border-radius:6px;padding:9px 12px;margin:4px 6px 0 0;font-size:13px}
a{color:#7fb3e8}
</style>
<header><h1>UV-Light and Fan Trap &mdash; <span id=id>...</span></h1></header>
<main>
<div class=card id=main-card></div>
<div class=card>
  <button onclick="cmd({cmd:'capture',sec:8})">Test capture (8 s)</button>
  <button onclick="cmd({cmd:'uv',pct:100})">UV on</button>
  <button onclick="cmd({cmd:'uv',pct:-1})">UV auto</button>
  <button onclick="cmd({cmd:'enable',on:true})">Enable</button>
  <button onclick="cmd({cmd:'enable',on:false})">Disable</button>
  <button onclick="cmd({cmd:'camcycle'})">Restart camera</button>
  <p><a href="/log.csv" download>Download trial log (CSV)</a></p>
</div>
</main>
<script>
function row(k,v){return '<div class=row><span class=k>'+k+'</span><span class=v>'+v+'</span></div>'}
async function tick(){
  try{
    const s = await (await fetch('/status.json')).json();
    document.getElementById('id').textContent = s.id;
    document.getElementById('main-card').innerHTML =
      row('State','<span class=state>'+s.state+'</span>')+
      row('Time', s.time)+
      row('Uptime', (s.uptime/3600).toFixed(1)+' h')+
      row('Battery', s.vbat.toFixed(2)+' V  ('+s.soc.toFixed(0)+' %)')+
      row('Panel', s.vpv.toFixed(2)+' V '+(s.charging?'(charging)':''))+
      row('Temp / RH', s.env_ok ? (s.temp.toFixed(1)+' &deg;C / '+s.rh.toFixed(0)+' %') : 'sensor fault')+
      row('Camera', s.cam_powered ? (s.cam_online ? 'online, state '+s.cam_state : 'OFFLINE') : 'powered down (day)')+
      row('Last frame', s.cat+' caterpillar, '+s.aph+' aphid, '+s.non+' non-target')+
      row('Captures', s.captures+' ('+s.cap_cam+' camera / '+s.cap_sonar+' ultrasonic)')+
      row('Fan run time', s.fan_s+' s')+
      row('Cloud', (s.mqtt?'connected':'offline')+' &middot; delivery '+s.delivery.toFixed(1)+' % &middot; queued '+s.spool)+
      row('Storage free', (s.fs_free/1024).toFixed(0)+' kB');
  }catch(e){}
}
async function cmd(o){
  await fetch('/cmd',{method:'POST',body:JSON.stringify(o)});
  setTimeout(tick,300);
}
tick(); setInterval(tick,3000);
</script>)HTML";

void handleRoot() {
  server.send_P(200, "text/html", kIndex);
}

void handleStatus() {
  if (!status_) { server.send(503, "application/json", "{}"); return; }
  server.send(200, "application/json", status_());
}

void handleLog() {
  File f = LittleFS.open(LOGFILE_PATH, "r");
  if (!f) { server.send(404, "text/plain", "no log"); return; }
  server.sendHeader("Content-Disposition",
                    "attachment; filename=\"" DEVICE_ID "-trial.csv\"");
  server.streamFile(f, "text/csv");
  f.close();
}

void handleCmd() {
  String body = server.arg("plain");
  if (body.isEmpty()) body = server.arg("q");
  if (command_ && !body.isEmpty()) command_(body);
  server.send(200, "application/json", "{\"ok\":true}");
}
}  // namespace

namespace webui {

void begin(StatusProvider status, CommandHandler command) {
  status_ = status;
  command_ = command;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status.json", HTTP_GET, handleStatus);
  server.on("/log.csv", HTTP_GET, handleLog);
  server.on("/cmd", HTTP_POST, handleCmd);
  server.on("/cmd", HTTP_GET, handleCmd);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
}

void loop() { server.handleClient(); }

}  // namespace webui
