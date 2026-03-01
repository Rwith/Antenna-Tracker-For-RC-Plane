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
    float panAngle     = 0.0f;   // current compass heading (degrees)
    float tiltAngle    = 0.0f;   // current tilt from PWM (degrees)
    float planeSpeed   = 0.0f;   // groundspeed km/h (from CRSF)
    float planeHeading = 0.0f;   // true heading deg (from CRSF)

    // Signal health
    bool homeGpsOk = false;
    bool crsfOk    = false;

    // RF link stats (CRSF 0x14)
    int8_t  linkRSSI    = 0;
    uint8_t linkLQ      = 0;
    int8_t  linkSNR     = 0;
    uint8_t linkTxPwr   = 0;   // TX power index
    uint8_t linkRfMode  = 0;
    bool    linkValid   = false;

    // Flight statistics (accumulated in main.cpp)
    float    maxSpeedKmh = 0.0f;
    float    maxAltM     = 0.0f;   // max altitude MSL (m)
    float    maxRelAltM  = 0.0f;   // max altitude above home (m)
    float    maxDistM    = 0.0f;   // max horizontal distance (m)
    uint32_t flightMs    = 0;      // elapsed flight time (ms, timer starts at first movement)
    bool     statsResetReq = false; // set by /reset handler; cleared by main loop
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
<link rel="stylesheet" href="https://unpkg.com/maplibre-gl@4.7.1/dist/maplibre-gl.css"/>
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
#map{height:420px;border-radius:6px;overflow:hidden}
.maplibregl-ctrl-attrib{background:rgba(26,29,39,.8)!important;color:#8b8fa8!important}
.maplibregl-ctrl-attrib a,.maplibregl-ctrl-attrib button{color:#60a5fa!important;background:none!important}
.maplibregl-popup-content{background:#1a1d27;color:#e0e2ec;border:1px solid #2a2d3a;border-radius:6px;padding:8px 12px;font-family:'Courier New',monospace;font-size:12px}
.maplibregl-popup-tip{border-top-color:#2a2d3a}
.maplibregl-ctrl button{background:#1a1d27!important;border-color:#2a2d3a!important}
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
    <div class="row">
      <span class="lbl">Min track dist</span>
      <span style="display:flex;align-items:center;gap:4px">
        <input id="cfgMin" type="number" min="1" max="500" step="1" value="5"
               style="width:48px;background:#0f1117;color:#e0e2ec;border:1px solid #2a2d3a;border-radius:3px;padding:1px 4px;font-family:inherit;font-size:12px">
        <span class="lbl">m</span>
        <button onclick="setMin()" style="background:#2a2d3a;color:#60a5fa;border:1px solid #2a2d3a;border-radius:3px;padding:1px 7px;cursor:pointer;font-family:inherit;font-size:11px">Set</button>
      </span>
    </div>
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
    <div class="row"><span class="lbl">Speed</span>     <span class="val info" id="pSpd">--</span></div>
    <div class="row"><span class="lbl">Heading</span>   <span class="val" id="pHdg">--</span></div>
  </div>

  <div class="card">
    <div class="ct">RF Link (ELRS)</div>
    <div class="row">
      <span class="lbl">Link quality</span>
      <span style="display:flex;align-items:center;gap:6px">
        <div style="width:55px;height:5px;border-radius:3px;background:#2a2d3a;overflow:hidden">
          <div id="rLQfill" style="height:100%;width:0%;border-radius:3px;background:var(--green);transition:width .5s,background .5s"></div>
        </div>
        <span class="val" id="rLQ">--</span>
      </span>
    </div>
    <div class="row"><span class="lbl">RSSI</span>     <span class="val" id="rRSSI">--</span></div>
    <div class="row"><span class="lbl">SNR</span>      <span class="val" id="rSNR">--</span></div>
    <div class="row"><span class="lbl">TX power</span> <span class="val" id="rPwr">--</span></div>
    <div class="row"><span class="lbl">RF mode</span>  <span class="val" id="rMode">--</span></div>
  </div>

  <div class="card">
    <div class="ct" style="display:flex;justify-content:space-between;align-items:center">
      <span>Flight Stats</span>
      <button onclick="resetStats()" style="background:#2a2d3a;color:#f87171;border:1px solid #2a2d3a;border-radius:3px;padding:1px 7px;cursor:pointer;font-family:inherit;font-size:11px">Reset</button>
    </div>
    <div class="row"><span class="lbl">Flight time</span>  <span class="val info" id="fsTime">0:00</span></div>
    <div class="row"><span class="lbl">Max speed</span>    <span class="val"      id="fsSpd">--</span></div>
    <div class="row"><span class="lbl">Max alt (MSL)</span><span class="val"      id="fsAlt">--</span></div>
    <div class="row"><span class="lbl">Max alt (AGL)</span><span class="val"      id="fsAgl">--</span></div>
    <div class="row"><span class="lbl">Max distance</span> <span class="val"      id="fsDist">--</span></div>
  </div>

  <div class="card" style="grid-column:1/-1">
    <div class="ct" style="display:flex;justify-content:space-between;align-items:center">
      <span>Live Graph</span>
      <select id="graphMetric" onchange="switchMetric()" style="background:#0f1117;color:#e0e2ec;border:1px solid #2a2d3a;border-radius:3px;padding:2px 8px;font-family:inherit;font-size:11px;cursor:pointer">
        <option value="speed">Speed (km/h)</option>
        <option value="altitude">Altitude (m)</option>
        <option value="distance">Distance (m)</option>
        <option value="rssi">RSSI (dBm)</option>
        <option value="lq">Link Quality (%)</option>
        <option value="tilt">Tilt Angle (&#xB0;)</option>
      </select>
    </div>
    <div style="position:relative;height:160px"><canvas id="liveChart"></canvas></div>
  </div>

  <div class="card map-card">
    <div class="ct">Live Map &nbsp;&#x25CF; <span class="ok">green</span> = tracker &nbsp;&#x25CF; <span class="info">blue</span> = plane</div>
    <div id="map"></div>
  </div>

</div>
<script src="https://unpkg.com/maplibre-gl@4.7.1/dist/maplibre-gl.js"></script>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.7/dist/chart.umd.min.js"></script>
<script>
const deg=v=>v.toFixed(1)+'&#xB0;';
const mt =v=>v.toFixed(0)+' m';
const ind=(v,t,f)=>`<span class="${v?'ok':'warn'}"><span class="dot"></span>${v?t:f}</span>`;
const TX_PWR=['Off','10 mW','25 mW','100 mW','500 mW','1 W','2 W','250 mW','50 mW'];
function fmtT(ms){const s=Math.floor(ms/1000),m=Math.floor(s/60),h=Math.floor(m/60);
  return h?h+':'+String(m%60).padStart(2,'0')+':'+String(s%60).padStart(2,'0')
          :m+':'+String(s%60).padStart(2,'0');}
function arrow(id,a,len){
  const r=(a-90)*Math.PI/180,el=document.getElementById(id);
  el.setAttribute('x2',(Math.cos(r)*len).toFixed(1));
  el.setAttribute('y2',(Math.sin(r)*len).toFixed(1));
}
// ── Map (MapLibre GL) ────────────────────────────────────────────────────
const TRAIL_MAX=300;
const trailCoords=[];
let mapReady=false,autoFit=true,cfgLoaded=false;
let hMarker=null,pMarker=null;
const map=new maplibregl.Map({
  container:'map',
  style:{version:8,sources:{
    basemap:{type:'raster',tileSize:256,
      tiles:['https://a.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}.png',
             'https://b.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}.png',
             'https://c.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}.png'],
      attribution:'&copy; <a href="https://openstreetmap.org/copyright">OSM</a> &copy; <a href="https://carto.com/attributions">CARTO</a>'},
  },layers:[{id:'basemap',type:'raster',source:'basemap'}]},
  center:[0,20],zoom:2
});
map.addControl(new maplibregl.NavigationControl(),'top-right');
map.on('load',()=>{
  map.addSource('trail',{type:'geojson',data:{type:'Feature',geometry:{type:'LineString',coordinates:[]}}});
  map.addLayer({id:'trail',type:'line',source:'trail',paint:{'line-color':'#60a5fa','line-width':1.5,'line-opacity':.35}});
  map.addSource('sight',{type:'geojson',data:{type:'Feature',geometry:{type:'LineString',coordinates:[]}}});
  map.addLayer({id:'sight',type:'line',source:'sight',paint:{'line-color':'#60a5fa','line-width':1.5,'line-dasharray':[5,3],'line-opacity':.65}});
  mapReady=true;
});
map.on('dragstart zoomstart',()=>{autoFit=false;});
function mkEl(h){const e=document.createElement('div');e.innerHTML=h;return e;}
// ── Live graph (Chart.js) ─────────────────────────────────────────────────
const GRAPH_MAX=120;
const METRICS={
  speed:    {label:'Speed',        unit:'km/h', color:'#60a5fa',fill:'rgba(96,165,250,.12)'},
  altitude: {label:'Altitude',     unit:'m',    color:'#4ade80',fill:'rgba(74,222,128,.12)'},
  distance: {label:'Distance',     unit:'m',    color:'#f97316',fill:'rgba(249,115,22,.12)'},
  rssi:     {label:'RSSI',         unit:'dBm',  color:'#f87171',fill:'rgba(248,113,113,.12)'},
  lq:       {label:'Link Quality', unit:'%',    color:'#a78bfa',fill:'rgba(167,139,250,.12)'},
  tilt:     {label:'Tilt Angle',   unit:'\u00B0',color:'#facc15',fill:'rgba(250,204,21,.12)'},
};
const gBuf={speed:[],altitude:[],distance:[],rssi:[],lq:[],tilt:[]};
const gLbl=[];let curMetric='speed';
Chart.defaults.color='#8b8fa8';
const chart=new Chart(document.getElementById('liveChart'),{
  type:'line',
  data:{labels:gLbl,datasets:[{label:'Speed (km/h)',data:gBuf.speed,
    borderColor:'#60a5fa',backgroundColor:'rgba(96,165,250,.12)',
    borderWidth:1.5,pointRadius:0,tension:0.35,fill:true}]},
  options:{animation:false,responsive:true,maintainAspectRatio:false,
    plugins:{legend:{display:false},tooltip:{mode:'index',intersect:false,
      callbacks:{label:ctx=>ctx.parsed.y.toFixed(1)+' '+(METRICS[curMetric]||{unit:''}).unit}}},
    scales:{
      x:{ticks:{maxTicksLimit:8,font:{family:'Courier New',size:10}},grid:{color:'#2a2d3a'}},
      y:{ticks:{font:{family:'Courier New',size:10}},grid:{color:'#2a2d3a'}}
    }}});
function switchMetric(){
  curMetric=document.getElementById('graphMetric').value;
  const m=METRICS[curMetric];const ds=chart.data.datasets[0];
  ds.label=m.label+' ('+m.unit+')';ds.data=gBuf[curMetric];
  ds.borderColor=m.color;ds.backgroundColor=m.fill;
  chart.update('none');
}
function pushGraph(d){
  const now=new Date().toLocaleTimeString([],{hour:'2-digit',minute:'2-digit',second:'2-digit'});
  gLbl.push(now);if(gLbl.length>GRAPH_MAX)gLbl.shift();
  const p=(k,v)=>{gBuf[k].push(v);if(gBuf[k].length>GRAPH_MAX)gBuf[k].shift();};
  p('speed',d.planeSpeed);p('altitude',d.planeAlt);p('distance',d.distM);
  p('rssi',d.linkRSSI);p('lq',d.linkLQ);p('tilt',d.tiltAngle);
  chart.update('none');
}
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
    document.getElementById('pSpd').textContent=d.planeSpeed.toFixed(1)+' km/h';
    document.getElementById('pHdg').textContent=deg(d.planeHeading);
    if(!cfgLoaded){document.getElementById('cfgMin').value=d.minTrackDist.toFixed(0);cfgLoaded=true;}
    // ── RF Link ───────────────────────────────────────────────────────────────
    if(d.linkValid){
      const lq=d.linkLQ;
      document.getElementById('rLQ').textContent=lq+'%';
      document.getElementById('rLQfill').style.width=lq+'%';
      document.getElementById('rLQfill').style.background=lq>=70?'var(--green)':lq>=40?'#facc15':'var(--red)';
      document.getElementById('rRSSI').textContent=d.linkRSSI+' dBm';
      document.getElementById('rSNR').textContent=d.linkSNR+' dB';
      document.getElementById('rPwr').textContent=TX_PWR[d.linkTxPwr]||'?';
      document.getElementById('rMode').textContent='Mode '+d.linkRfMode;
    }
    // ── Flight stats ──────────────────────────────────────────────────────────
    document.getElementById('fsTime').textContent=fmtT(d.flightMs);
    document.getElementById('fsSpd' ).textContent=d.maxSpeed.toFixed(1)+' km/h';
    document.getElementById('fsAlt' ).textContent=d.maxAlt.toFixed(1)+' m';
    document.getElementById('fsAgl' ).textContent=d.maxRelAlt.toFixed(1)+' m';
    document.getElementById('fsDist').textContent=mt(d.maxDist);
    pushGraph(d);
    // ── Map ────────────────────────────────────────────────────────────────────
    if(mapReady){
      if(d.homeValid){
        if(!hMarker){hMarker=new maplibregl.Marker({element:mkEl('<div style="width:13px;height:13px;border-radius:50%;background:#4ade80;border:2px solid rgba(255,255,255,.4)"></div>')})
          .setLngLat([d.homeLon,d.homeLat]).setPopup(new maplibregl.Popup({offset:10}).setHTML('<b>Tracker</b><br>'+d.homeAlt.toFixed(1)+' m MSL')).addTo(map);}
        else hMarker.setLngLat([d.homeLon,d.homeLat]);
      }
      if(d.planeValid){
        const ph='<svg width="18" height="18" viewBox="-9 -9 18 18" style="transform:rotate('+d.planeHeading+'deg)"><polygon points="0,-8 5,6 0,3 -5,6" fill="#60a5fa" stroke="rgba(255,255,255,.4)" stroke-width="1.5"/></svg>';
        if(!pMarker){pMarker=new maplibregl.Marker({element:mkEl(ph)})
          .setLngLat([d.planeLon,d.planeLat]).setPopup(new maplibregl.Popup({offset:10}).setHTML('<b>Plane</b><br>'+d.planeAlt.toFixed(1)+' m MSL &bull; '+d.planeSpeed.toFixed(1)+' km/h')).addTo(map);}
        else{pMarker.setLngLat([d.planeLon,d.planeLat]);pMarker.getElement().innerHTML=ph;}
        trailCoords.push([d.planeLon,d.planeLat]);
        if(trailCoords.length>TRAIL_MAX)trailCoords.shift();
        map.getSource('trail').setData({type:'Feature',geometry:{type:'LineString',coordinates:trailCoords}});
      }
      if(d.homeValid&&d.planeValid){
        map.getSource('sight').setData({type:'Feature',geometry:{type:'LineString',coordinates:[[d.homeLon,d.homeLat],[d.planeLon,d.planeLat]]}});
        if(autoFit)map.fitBounds([[Math.min(d.homeLon,d.planeLon)-.002,Math.min(d.homeLat,d.planeLat)-.002],
                                   [Math.max(d.homeLon,d.planeLon)+.002,Math.max(d.homeLat,d.planeLat)+.002]],{padding:60,maxZoom:16});
      }else if(d.homeValid&&autoFit){map.flyTo({center:[d.homeLon,d.homeLat],zoom:14});}
    }
  }catch(e){
    document.getElementById('ts').textContent='&#x26A0; No response \u2013 retrying\u2026';
  }
}
async function resetStats(){ await fetch('/reset'); }
async function setMin(){
  const v=parseFloat(document.getElementById('cfgMin').value);
  if(v>=1&&v<=500) await fetch('/config?minDist='+v);
}
poll(); setInterval(poll,1000);
</script>
</body>
</html>)rawhtml";

// ─────────────────────────────────────────────────────────────────────────────
class TrackerWebServer {
public:
    TrackerStatus status;
    float minTrackDist = MIN_PLANE_DISTANCE_M;   // adjustable at runtime via /config

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

        _srv.on("/",       [this]() { _handleRoot();   });
        _srv.on("/data",   [this]() { _handleData();   });
        _srv.on("/config", [this]() { _handleConfig(); });
        _srv.on("/reset",  [this]() { _handleReset();  });
        _srv.begin();
    }

    void handle() { _srv.handleClient(); }

private:
    WebServer _srv{80};

    void _handleRoot() {
        _srv.send_P(200, "text/html", _DASH_HTML);
    }

    void _handleData() {
        char buf[768];
        snprintf(buf, sizeof(buf),
            "{\"homeValid\":%s,\"homeLat\":%.7f,\"homeLon\":%.7f,"
            "\"homeAlt\":%.1f,\"homeSats\":%u,"
            "\"planeValid\":%s,\"planeLat\":%.7f,\"planeLon\":%.7f,"
            "\"planeAlt\":%.1f,\"planeSpeed\":%.1f,\"planeHeading\":%.1f,"
            "\"tracking\":%s,"
            "\"bearing\":%.1f,\"elevation\":%.1f,\"distM\":%.0f,"
            "\"panAngle\":%.1f,\"tiltAngle\":%.1f,"
            "\"homeGpsOk\":%s,\"crsfOk\":%s,"
            "\"minTrackDist\":%.1f,"
            "\"linkRSSI\":%d,\"linkLQ\":%u,\"linkSNR\":%d,"
            "\"linkTxPwr\":%u,\"linkRfMode\":%u,\"linkValid\":%s,"
            "\"maxSpeed\":%.1f,\"maxAlt\":%.1f,\"maxRelAlt\":%.1f,"
            "\"maxDist\":%.0f,\"flightMs\":%lu}",
            status.homeValid  ? "true" : "false",
            status.homeLat, status.homeLon, status.homeAlt,
            static_cast<unsigned>(status.homeSats),
            status.planeValid ? "true" : "false",
            status.planeLat, status.planeLon, status.planeAlt,
            status.planeSpeed, status.planeHeading,
            status.tracking   ? "true" : "false",
            status.bearing, status.elevation, status.distM,
            status.panAngle, status.tiltAngle,
            status.homeGpsOk  ? "true" : "false",
            status.crsfOk     ? "true" : "false",
            minTrackDist,
            static_cast<int>(status.linkRSSI),
            static_cast<unsigned>(status.linkLQ),
            static_cast<int>(status.linkSNR),
            static_cast<unsigned>(status.linkTxPwr),
            static_cast<unsigned>(status.linkRfMode),
            status.linkValid  ? "true" : "false",
            status.maxSpeedKmh, status.maxAltM, status.maxRelAltM,
            status.maxDistM, static_cast<unsigned long>(status.flightMs)
        );
        _srv.send(200, "application/json", buf);
    }

    void _handleConfig() {
        if (_srv.hasArg("minDist")) {
            float v = _srv.arg("minDist").toFloat();
            if (v >= 1.0f && v <= 500.0f) minTrackDist = v;
        }
        _srv.send(200, "application/json", "{\"ok\":true}");
    }

    void _handleReset() {
        status.statsResetReq = true;
        _srv.send(200, "application/json", "{\"ok\":true}");
    }
};
