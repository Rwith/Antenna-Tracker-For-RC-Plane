#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "config.h"

// ─── Shared status struct ─────────────────────────────────────────────────────
// Filled by main.cpp every loop iteration; read by the web server on demand.
struct TrackerStatus {
    // Home GPS (NEO-6M)
    bool    homeValid = false;
    double  homeLat   = 0.0;
    double  homeLon   = 0.0;
    float   homeAlt   = 0.0f;
    uint8_t homeSats  = 0;

    // Plane GPS (ELRS/CRSF)
    bool   planeValid = false;
    double planeLat   = 0.0;
    double planeLon   = 0.0;
    float  planeAlt   = 0.0f;

    // Tracking output
    bool  tracking  = false;   // actively commanding servos
    float bearing   = 0.0f;   // target azimuth (degrees)
    float elevation = 0.0f;   // target elevation (degrees)
    float distM     = 0.0f;   // horizontal distance to plane (m)
    float panAngle  = 0.0f;   // current compass heading (degrees)
    float tiltAngle = 0.0f;   // current tilt from PWM (degrees)

    // Signal health
    bool homeGpsOk = false;
    bool crsfOk    = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// HTML dashboard – stored in flash (PROGMEM) to keep it out of SRAM.
// The page polls /data (JSON) every second and updates all fields in place.
// ─────────────────────────────────────────────────────────────────────────────
static const char _DASH_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Antenna Tracker</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
<style>
:root{--bg:#0f1117;--card:#1a1d27;--border:#2a2d3a;--text:#e0e2ec;
      --dim:#8b8fa8;--green:#4ade80;--red:#f87171;--blue:#60a5fa;}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:'Courier New',monospace;
     font-size:13px;padding:14px}
header{margin-bottom:14px}
h1{font-size:19px;font-weight:bold;letter-spacing:.03em}
.sub{color:var(--dim);font-size:11px;margin-top:3px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(225px,1fr));gap:11px}
.card{background:var(--card);border:1px solid var(--border);border-radius:9px;padding:14px}
.ct{font-size:10px;text-transform:uppercase;letter-spacing:.12em;
    color:var(--dim);margin-bottom:11px}
.row{display:flex;justify-content:space-between;align-items:center;
     padding:4px 0;border-bottom:1px solid var(--border)}
.row:last-child{border-bottom:none}
.lbl{color:var(--dim)}
.val{font-weight:bold;font-size:14px}
.ok{color:var(--green)}.warn{color:var(--red)}.info{color:var(--blue)}
.dot{display:inline-block;width:7px;height:7px;border-radius:50%;
     margin-right:4px;background:currentColor}
.rose{display:flex;justify-content:center;margin-bottom:11px}
.map-card{grid-column:1/-1}
#map{height:340px;border-radius:6px;overflow:hidden}
.leaflet-popup-content-wrapper,.leaflet-popup-tip{background:#1a1d27;color:#e0e2ec;border:1px solid #2a2d3a}
.leaflet-control-attribution{background:rgba(26,29,39,.8)!important;color:#8b8fa8!important}
.leaflet-control-attribution a{color:#60a5fa!important}
</style>
</head>
<body>
<header>
  <h1>&#x1F4E1; Antenna Tracker</h1>
  <div class="sub" id="ts">Connecting&#x2026;</div>
</header>
<div class="grid">

  <div class="card">
    <div class="ct">Pan / Tilt</div>
    <div class="rose">
      <svg width="128" height="128" viewBox="-64 -64 128 128">
        <circle r="59" fill="none" stroke="#2a2d3a" stroke-width="1.5"/>
        <text x="0"   y="-42" text-anchor="middle" font-size="9" fill="#8b8fa8" font-family="monospace">N</text>
        <text x="44"  y="4"   text-anchor="middle" font-size="9" fill="#8b8fa8" font-family="monospace">E</text>
        <text x="0"   y="51"  text-anchor="middle" font-size="9" fill="#8b8fa8" font-family="monospace">S</text>
        <text x="-44" y="4"   text-anchor="middle" font-size="9" fill="#8b8fa8" font-family="monospace">W</text>
        <!-- target bearing: solid blue -->
        <line id="aB" x1="0" y1="0" x2="0" y2="-50"
              stroke="#60a5fa" stroke-width="2.5" stroke-linecap="round"/>
        <!-- current pan: dashed green -->
        <line id="aP" x1="0" y1="0" x2="0" y2="-41"
              stroke="#4ade80" stroke-width="2" stroke-linecap="round"
              stroke-dasharray="5 3"/>
        <circle r="3" fill="#1a1d27" stroke="#8b8fa8" stroke-width="1.5"/>
      </svg>
    </div>
    <div class="row"><span class="lbl">Target bearing</span><span class="val info" id="vBear">--</span></div>
    <div class="row"><span class="lbl">Pan (compass)</span> <span class="val ok"   id="vPan">--</span></div>
    <div class="row"><span class="lbl">Tilt angle</span>    <span class="val"      id="vTilt">--</span></div>
    <div class="row"><span class="lbl">Commanded elev</span><span class="val"      id="vElev">--</span></div>
  </div>

  <div class="card">
    <div class="ct">Tracking Status</div>
    <div class="row"><span class="lbl">State</span>     <span class="val" id="tState">--</span></div>
    <div class="row"><span class="lbl">Distance</span>  <span class="val" id="tDist">--</span></div>
    <div class="row"><span class="lbl">Alt diff</span>  <span class="val" id="tAltD">--</span></div>
    <div class="row"><span class="lbl">Home GPS</span>  <span class="val" id="tHGps">--</span></div>
    <div class="row"><span class="lbl">CRSF link</span> <span class="val" id="tCrsf">--</span></div>
  </div>

  <div class="card">
    <div class="ct">Home GPS (NEO-6M)</div>
    <div class="row"><span class="lbl">Fix</span>        <span class="val" id="hFix">--</span></div>
    <div class="row"><span class="lbl">Satellites</span> <span class="val" id="hSat">--</span></div>
    <div class="row"><span class="lbl">Latitude</span>   <span class="val" id="hLat">--</span></div>
    <div class="row"><span class="lbl">Longitude</span>  <span class="val" id="hLon">--</span></div>
    <div class="row"><span class="lbl">Altitude</span>   <span class="val" id="hAlt">--</span></div>
  </div>

  <div class="card">
    <div class="ct">Plane GPS (ELRS)</div>
    <div class="row"><span class="lbl">Fix</span>       <span class="val" id="pFix">--</span></div>
    <div class="row"><span class="lbl">Latitude</span>  <span class="val" id="pLat">--</span></div>
    <div class="row"><span class="lbl">Longitude</span> <span class="val" id="pLon">--</span></div>
    <div class="row"><span class="lbl">Altitude</span>  <span class="val" id="pAlt">--</span></div>
  </div>

  <div class="card map-card">
    <div class="ct">Live Map &nbsp;&#x25CF; <span class="ok">green</span> = tracker &nbsp;&#x25CF; <span class="info">blue</span> = plane</div>
    <div id="map"></div>
  </div>

</div>
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<script>
const deg=v=>v.toFixed(1)+'&#xB0;';
const mt =v=>v.toFixed(0)+' m';
const ind=(v,t,f)=>`<span class="${v?'ok':'warn'}"><span class="dot"></span>${v?t:f}</span>`;
function arrow(id,a,len){
  const r=(a-90)*Math.PI/180,el=document.getElementById(id);
  el.setAttribute('x2',(Math.cos(r)*len).toFixed(1));
  el.setAttribute('y2',(Math.sin(r)*len).toFixed(1));
}
// ── Map ────────────────────────────────────────────────────────────────────
const map=L.map('map').setView([20,0],2);
L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}.png',{
  attribution:'&copy; <a href="https://openstreetmap.org/copyright">OSM</a> &copy; <a href="https://carto.com/attributions">CARTO</a>',
  maxZoom:19
}).addTo(map);
function mkIcon(c){
  return L.divIcon({
    html:'<div style="width:13px;height:13px;border-radius:50%;background:'+c+';border:2px solid rgba(255,255,255,.4)"></div>',
    iconSize:[13,13],iconAnchor:[6,6],className:''
  });
}
let hM=null,pM=null,tL=null,autoFit=true;
map.on('dragstart zoomstart',()=>{autoFit=false;});
// ──────────────────────────────────────────────────────────────────────────
async function poll(){
  try{
    const d=await fetch('/data').then(r=>r.json());
    document.getElementById('ts').textContent='Updated '+new Date().toLocaleTimeString();
    arrow('aB',d.bearing,50); arrow('aP',d.panAngle,41);
    document.getElementById('vBear').textContent=deg(d.bearing);
    document.getElementById('vPan' ).textContent=deg(d.panAngle);
    document.getElementById('vTilt').textContent=deg(d.tiltAngle);
    document.getElementById('vElev').textContent=deg(d.elevation);
    document.getElementById('tState').innerHTML=d.tracking
      ?'<span class="ok"><span class="dot"></span>Tracking</span>'
      :'<span class="warn"><span class="dot"></span>Stopped</span>';
    document.getElementById('tDist').textContent=mt(d.distM);
    const ad=d.planeAlt-d.homeAlt;
    document.getElementById('tAltD').textContent=(ad>=0?'+':'')+ad.toFixed(0)+' m';
    document.getElementById('tHGps').innerHTML=ind(d.homeGpsOk,'OK','Timeout');
    document.getElementById('tCrsf').innerHTML=ind(d.crsfOk,   'OK','Timeout');
    document.getElementById('hFix').innerHTML =ind(d.homeValid, 'Valid','No fix');
    document.getElementById('hSat').textContent=d.homeSats;
    document.getElementById('hLat').textContent=d.homeLat.toFixed(7);
    document.getElementById('hLon').textContent=d.homeLon.toFixed(7);
    document.getElementById('hAlt').textContent=d.homeAlt.toFixed(1)+' m';
    document.getElementById('pFix').innerHTML =ind(d.planeValid,'Valid','No fix');
    document.getElementById('pLat').textContent=d.planeLat.toFixed(7);
    document.getElementById('pLon').textContent=d.planeLon.toFixed(7);
    document.getElementById('pAlt').textContent=d.planeAlt.toFixed(1)+' m';
    // ── Map markers ──────────────────────────────────────────────────────────
    if(d.homeValid){
      if(!hM){hM=L.marker([d.homeLat,d.homeLon],{icon:mkIcon('#4ade80')})
        .bindPopup('<b>Tracker</b><br>'+d.homeAlt.toFixed(1)+' m MSL').addTo(map);}
      else hM.setLatLng([d.homeLat,d.homeLon]);
    }
    if(d.planeValid){
      const pc='<b>Plane</b><br>'+d.planeAlt.toFixed(1)+' m MSL';
      if(!pM){pM=L.marker([d.planeLat,d.planeLon],{icon:mkIcon('#60a5fa')})
        .bindPopup(pc).addTo(map);}
      else{pM.setLatLng([d.planeLat,d.planeLon]);pM.getPopup().setContent(pc);}
    }
    if(d.homeValid&&d.planeValid){
      const pts=[[d.homeLat,d.homeLon],[d.planeLat,d.planeLon]];
      if(!tL){tL=L.polyline(pts,{color:'#60a5fa',weight:1.5,dashArray:'6 4',opacity:.65}).addTo(map);}
      else tL.setLatLngs(pts);
      if(autoFit)map.fitBounds(pts,{padding:[40,40],maxZoom:16});
    }else if(d.homeValid&&autoFit){map.setView([d.homeLat,d.homeLon],14);}
  }catch(e){
    document.getElementById('ts').textContent='&#x26A0; No response \u2013 retrying\u2026';
  }
}
poll(); setInterval(poll,1000);
</script>
</body>
</html>)rawhtml";

// ─────────────────────────────────────────────────────────────────────────────
class TrackerWebServer {
public:
    TrackerStatus status;

    void begin() {
        Serial.printf("[WiFi]  Connecting to %s", WIFI_SSID);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        uint32_t t = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS) {
            delay(250);
            Serial.print('.');
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WiFi]  Connected. Dashboard -> http://%s/\n",
                          WiFi.localIP().toString().c_str());
        } else {
            Serial.println("[WiFi]  Connection failed – tracker continues without dashboard.");
        }

        _srv.on("/",     [this]() { _handleRoot(); });
        _srv.on("/data", [this]() { _handleData(); });
        _srv.begin();
    }

    void handle() { _srv.handleClient(); }

private:
    WebServer _srv{80};

    void _handleRoot() {
        _srv.send_P(200, "text/html", _DASH_HTML);
    }

    void _handleData() {
        char buf[384];
        snprintf(buf, sizeof(buf),
            "{\"homeValid\":%s,\"homeLat\":%.7f,\"homeLon\":%.7f,"
            "\"homeAlt\":%.1f,\"homeSats\":%u,"
            "\"planeValid\":%s,\"planeLat\":%.7f,\"planeLon\":%.7f,"
            "\"planeAlt\":%.1f,\"tracking\":%s,"
            "\"bearing\":%.1f,\"elevation\":%.1f,\"distM\":%.0f,"
            "\"panAngle\":%.1f,\"tiltAngle\":%.1f,"
            "\"homeGpsOk\":%s,\"crsfOk\":%s}",
            status.homeValid  ? "true" : "false",
            status.homeLat, status.homeLon, status.homeAlt,
            static_cast<unsigned>(status.homeSats),
            status.planeValid ? "true" : "false",
            status.planeLat, status.planeLon, status.planeAlt,
            status.tracking   ? "true" : "false",
            status.bearing, status.elevation, status.distM,
            status.panAngle, status.tiltAngle,
            status.homeGpsOk  ? "true" : "false",
            status.crsfOk     ? "true" : "false"
        );
        _srv.send(200, "application/json", buf);
    }
};
