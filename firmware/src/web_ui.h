/**
 * DrFX Ultra OS - settings page, served straight from flash.
 *
 * One self-contained file: no CDN, no framework, works with no internet. Stored
 * in PROGMEM and sent with server.send_P so it never occupies RAM.
 *
 * The styling deliberately mirrors what the 240x240 panel shows - mono type,
 * hairline rules, small letter-spaced caps - so the settings page reads as the
 * same product rather than a generic admin form.
 *
 * The timezone list near the bottom is GENERATED. Edit shared/timezones.json
 * and run tools/gen_timezones.py; do not hand-edit between the TZLIST markers.
 */
#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DrFX Ultra OS</title><style>
:root{--bg:#07080a;--line:#1b2027;--line2:#11151a;--txt:#e8ecf1;--dim:#79828f;--dim2:#4b535e;
 --acc:#31ff9a;--ok:#31ff9a;--bad:#ff3b52;--warn:#ffb020;
 --mono:ui-monospace,SFMono-Regular,Menlo,Consolas,"Liberation Mono",monospace}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--txt);font:14px/1.5 var(--mono);-webkit-font-smoothing:antialiased}
.wrap{max-width:640px;margin:0 auto;padding:28px 18px 72px}
h1{font-size:13px;letter-spacing:.28em;color:var(--acc);margin:0 0 4px;font-weight:700}
.sub{color:var(--dim);font-size:11px;letter-spacing:.14em;margin:0 0 22px;text-transform:uppercase}
nav{display:flex;gap:0;border-bottom:1px solid var(--line);margin-bottom:22px;overflow-x:auto}
nav button{background:none;border:0;border-bottom:1px solid transparent;color:var(--dim2);
 padding:10px 14px;font:inherit;font-size:11px;letter-spacing:.16em;text-transform:uppercase;
 cursor:pointer;white-space:nowrap;margin-bottom:-1px}
nav button:hover{color:var(--dim)}
nav button.on{color:var(--txt);border-bottom-color:var(--acc)}
section{display:none}section.on{display:block}
label{display:block;margin:16px 0 6px;font-size:10px;letter-spacing:.2em;color:var(--dim2);text-transform:uppercase}
input,select{width:100%;background:#0b0e12;border:1px solid var(--line);color:var(--txt);
 border-radius:2px;padding:10px 11px;font:inherit;font-size:13px}
input[type=color]{padding:3px;height:38px}
input[type=range]{padding:0;border:0;background:none;accent-color:var(--acc)}
input:focus,select:focus{outline:0;border-color:var(--acc)}
.row{display:flex;gap:12px}.row>div{flex:1}
.hint{color:var(--dim2);font-size:11px;margin-top:6px;line-height:1.45}
button.act{background:var(--acc);border:1px solid var(--acc);color:#000;border-radius:2px;
 padding:10px 18px;font:inherit;font-size:11px;letter-spacing:.16em;text-transform:uppercase;
 font-weight:700;cursor:pointer}
button.ghost{background:none;border:1px solid var(--line);color:var(--txt);font-weight:400}
button.ghost:hover{border-color:var(--dim2)}
button.danger{background:none;border:1px solid #3a1720;color:var(--bad);font-weight:400}
.bar{display:flex;gap:8px;flex-wrap:wrap;margin-top:26px;padding-top:18px;border-top:1px solid var(--line)}
.kv{display:flex;justify-content:space-between;gap:14px;padding:9px 0;border-bottom:1px solid var(--line2);font-size:12.5px}
.kv span:first-child{color:var(--dim);font-size:10px;letter-spacing:.16em;text-transform:uppercase;
 white-space:nowrap;padding-top:2px}
.kv span:last-child{font-variant-numeric:tabular-nums;text-align:right}
#toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%) translateY(90px);
 background:#0b0e12;border:1px solid var(--line);padding:11px 18px;border-radius:2px;
 transition:.22s;font-size:12px;max-width:90vw}
#toast.on{transform:translateX(-50%) translateY(0)}
code{background:#0f1318;padding:2px 6px;border-radius:2px;color:var(--acc);font-size:11.5px;word-break:break-all}
.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}
.inline{display:flex;gap:8px;align-items:center}
.inline input{flex:1}
.chk{display:flex;align-items:center;gap:9px;margin:18px 0 0;font-size:12.5px;color:var(--txt);
 letter-spacing:0;text-transform:none}
.chk input{width:auto;margin:0}
</style></head><body><div class="wrap">
<h1>DRFX ULTRA OS</h1><p class="sub" id="sub">loading&hellip;</p>

<nav>
  <button class="on" data-t="status">Status</button>
  <button data-t="wifi">Wi-Fi</button>
  <button data-t="bridge">Bridge</button>
  <button data-t="clock">Clock</button>
  <button data-t="display">Display</button>
  <button data-t="admin">Admin</button>
</nav>

<section id="status" class="on">
  <div id="stat"></div>
  <div class="bar">
    <button class="act" onclick="test()">Show a test signal</button>
    <button class="ghost act" onclick="load()">Refresh</button>
  </div>
</section>

<section id="wifi">
  <label>Network name (2.4 GHz only)</label>
  <div class="inline"><input id="ssid" list="nets" placeholder="MyWiFi"><datalist id="nets"></datalist>
  <button class="ghost act" onclick="scan()">Scan</button></div>
  <label>Password</label><input id="pass" type="password" placeholder="leave blank to keep current">
  <label>Backup network (optional)</label><input id="ssid2" placeholder="">
  <label>Backup password</label><input id="pass2" type="password" placeholder="leave blank to keep current">
  <label>Device name on your network</label><input id="host" placeholder="godmode">
  <p class="hint">Reachable afterwards at <code>http://godmode.local</code></p>
  <div class="bar"><button class="act" onclick="save()">Save &amp; reboot</button></div>
</section>

<section id="bridge">
  <label>Bridge URL</label><input id="bridge" placeholder="https://fx-godmode-bridge.yourname.workers.dev">
  <p class="hint">Your Cloudflare Worker address, no slash at the end. This is what makes TradingView's HTTPS-only webhooks work.</p>
  <label>Device key</label><input id="devKey" type="password" placeholder="leave blank to keep current">
  <p class="hint">Must match the <code>DEVICE_KEY</code> secret in the Worker.</p>
  <label>Device ID</label><input id="devId" placeholder="main">
  <p class="hint">Use different IDs to drive more than one screen from the same Worker.</p>
  <div class="row">
    <div><label>Check every (seconds)</label><input id="pollSec" type="number" min="2" max="300"></div>
    <div><label>Signal expires after (minutes)</label><input id="staleMin" type="number" min="0" max="10080"></div>
  </div>
  <p class="hint">0 = never expire. When a signal expires the screen falls back to the clock.</p>
  <div class="bar"><button class="act" onclick="save()">Save</button></div>
</section>

<section id="clock">
  <label>Time zone</label>
  <div class="inline"><select id="tz"></select>
  <button class="ghost act" onclick="detectTz()">Detect</button></div>
  <p class="hint">Daylight saving is handled automatically - the rule for each city carries its own
  changeover dates, so the screen is right on the mornings either side of the switch.</p>
  <div id="tzpreview" class="kv" style="margin-top:18px"><span>On the device now</span><span id="tznow">&mdash;</span></div>
  <div class="kv"><span>In your browser</span><span id="brnow">&mdash;</span></div>
  <label class="chk"><input id="showClock" type="checkbox">Show the clock when there is no signal</label>
  <p class="hint">With this off, the screen shows a plain "no signal" card instead.</p>
  <div class="bar"><button class="act" onclick="save()">Save</button></div>
</section>

<section id="display">
  <div class="row">
    <div><label>Daytime brightness</label><input id="brightDay" type="range" min="5" max="255"></div>
    <div><label>Night brightness</label><input id="brightNight" type="range" min="0" max="255"></div>
  </div>
  <div class="row">
    <div><label>Night starts (hour)</label><input id="nightStart" type="number" min="0" max="23"></div>
    <div><label>Night ends (hour)</label><input id="nightEnd" type="number" min="0" max="23"></div>
  </div>
  <p class="hint">Uses the time zone set on the Clock tab.</p>
  <label>Screen rotation</label>
  <select id="rotation"><option value="0">0&deg;</option><option value="1">90&deg;</option><option value="2">180&deg;</option><option value="3">270&deg;</option></select>
  <label>Theme</label>
  <div class="row">
    <div><label>Accent</label><input id="cAccent" type="color"></div>
    <div><label>Buy</label><input id="cBuy" type="color"></div>
    <div><label>Sell</label><input id="cSell" type="color"></div>
  </div>
  <div class="row">
    <div><label>Text</label><input id="cText" type="color"></div>
    <div><label>Background</label><input id="cBg" type="color"></div>
  </div>
  <p class="hint">Labels and hairlines are derived from your text and background colours, so the
  screens stay readable whatever you pick.</p>
  <div class="bar"><button class="act" onclick="save()">Save</button><button class="ghost act" onclick="test()">Preview on screen</button></div>
</section>

<section id="admin">
  <label>Settings username</label><input id="adminUser">
  <label>Settings password</label><input id="adminPass" type="password" placeholder="leave blank to keep current">
  <p class="hint">Protects this page and the firmware updater.</p>
  <div class="bar">
    <button class="act" onclick="save()">Save</button>
    <button class="ghost act" onclick="location.href='/update'">Firmware update</button>
    <button class="ghost act" onclick="post('/api/reboot')">Reboot</button>
    <button class="danger act" onclick="if(confirm('Erase all settings?'))post('/api/factory')">Factory reset</button>
  </div>
</section>

<div id="toast"></div></div><script>
var C={};
var $=function(i){return document.getElementById(i)};
document.querySelectorAll('nav button').forEach(function(b){b.onclick=function(){
  document.querySelectorAll('nav button').forEach(function(x){x.classList.remove('on')});
  document.querySelectorAll('section').forEach(function(x){x.classList.remove('on')});
  b.classList.add('on');$(b.dataset.t).classList.add('on');}});

/* TZLIST-BEGIN - generated by tools/gen_timezones.py, do not edit by hand */
var TZ=[
["UTC","UTC","UTC","UTC0"],
["Europe","Europe/London","London","GMT0BST,M3.5.0/1,M10.5.0"],
["Europe","Europe/Dublin","Dublin","IST-1GMT0,M10.5.0,M3.5.0/1"],
["Europe","Europe/Lisbon","Lisbon","WET0WEST,M3.5.0/1,M10.5.0"],
["Europe","Europe/Paris","Paris","CET-1CEST,M3.5.0,M10.5.0/3"],
["Europe","Europe/Berlin","Frankfurt / Berlin","CET-1CEST,M3.5.0,M10.5.0/3"],
["Europe","Europe/Zurich","Zurich","CET-1CEST,M3.5.0,M10.5.0/3"],
["Europe","Europe/Amsterdam","Amsterdam","CET-1CEST,M3.5.0,M10.5.0/3"],
["Europe","Europe/Madrid","Madrid","CET-1CEST,M3.5.0,M10.5.0/3"],
["Europe","Europe/Rome","Rome","CET-1CEST,M3.5.0,M10.5.0/3"],
["Europe","Europe/Athens","Athens","EET-2EEST,M3.5.0/3,M10.5.0/4"],
["Europe","Europe/Helsinki","Helsinki","EET-2EEST,M3.5.0/3,M10.5.0/4"],
["Europe","Europe/Istanbul","Istanbul","<+03>-3"],
["Europe","Europe/Moscow","Moscow","MSK-3"],
["Europe","Atlantic/Reykjavik","Reykjavik","GMT0"],
["Americas","America/New_York","New York","EST5EDT,M3.2.0,M11.1.0"],
["Americas","America/Toronto","Toronto","EST5EDT,M3.2.0,M11.1.0"],
["Americas","America/Chicago","Chicago","CST6CDT,M3.2.0,M11.1.0"],
["Americas","America/Denver","Denver","MST7MDT,M3.2.0,M11.1.0"],
["Americas","America/Phoenix","Phoenix (no DST)","MST7"],
["Americas","America/Los_Angeles","Los Angeles","PST8PDT,M3.2.0,M11.1.0"],
["Americas","America/Vancouver","Vancouver","PST8PDT,M3.2.0,M11.1.0"],
["Americas","America/Anchorage","Anchorage","AKST9AKDT,M3.2.0,M11.1.0"],
["Americas","Pacific/Honolulu","Honolulu","HST10"],
["Americas","America/Mexico_City","Mexico City","CST6"],
["Americas","America/Bogota","Bogota","<-05>5"],
["Americas","America/Sao_Paulo","Sao Paulo","<-03>3"],
["Americas","America/Argentina/Buenos_Aires","Buenos Aires","<-03>3"],
["Asia","Asia/Jerusalem","Tel Aviv","IST-2IDT,M3.4.4/26,M10.5.0"],
["Asia","Asia/Riyadh","Riyadh","<+03>-3"],
["Asia","Asia/Tehran","Tehran","<+0330>-3:30"],
["Asia","Asia/Dubai","Dubai","<+04>-4"],
["Asia","Asia/Karachi","Karachi","PKT-5"],
["Asia","Asia/Kolkata","Mumbai / Kolkata","IST-5:30"],
["Asia","Asia/Dhaka","Dhaka","<+06>-6"],
["Asia","Asia/Bangkok","Bangkok","<+07>-7"],
["Asia","Asia/Jakarta","Jakarta","WIB-7"],
["Asia","Asia/Singapore","Singapore","<+08>-8"],
["Asia","Asia/Hong_Kong","Hong Kong","HKT-8"],
["Asia","Asia/Shanghai","Shanghai","CST-8"],
["Asia","Asia/Taipei","Taipei","CST-8"],
["Asia","Asia/Seoul","Seoul","KST-9"],
["Asia","Asia/Tokyo","Tokyo","JST-9"],
["Africa","Africa/Lagos","Lagos","WAT-1"],
["Africa","Africa/Cairo","Cairo","EET-2EEST,M4.5.5/0,M10.5.4/24"],
["Africa","Africa/Johannesburg","Johannesburg","SAST-2"],
["Africa","Africa/Nairobi","Nairobi","EAT-3"],
["Pacific","Australia/Perth","Perth","AWST-8"],
["Pacific","Australia/Brisbane","Brisbane","AEST-10"],
["Pacific","Australia/Adelaide","Adelaide","ACST-9:30ACDT,M10.1.0,M4.1.0/3"],
["Pacific","Australia/Sydney","Sydney","AEST-10AEDT,M10.1.0,M4.1.0/3"],
["Pacific","Pacific/Auckland","Auckland","NZST-12NZDT,M9.5.0,M4.1.0/3"],
];
/* TZLIST-END */

/* Grouped picker. The value is the POSIX rule the firmware stores; the IANA
   name rides along in a data attribute so the device can show a human label. */
(function(){var s=$('tz'),g=null,og=null;
 for(var i=0;i<TZ.length;i++){var z=TZ[i];
  if(z[0]!==g){g=z[0];og=document.createElement('optgroup');og.label=g;s.appendChild(og);}
  var o=document.createElement('option');o.value=z[3];o.dataset.name=z[1];o.textContent=z[2];og.appendChild(o);}
})();

/* Preselect whatever the browser thinks we are in - right far more often than
   not, and it saves scrolling a list of fifty cities. */
function detectTz(){
 var want='';try{want=Intl.DateTimeFormat().resolvedOptions().timeZone||''}catch(e){}
 var opts=$('tz').options;
 for(var i=0;i<opts.length;i++){if(opts[i].dataset.name===want){$('tz').selectedIndex=i;
  toast('Detected '+want);tzPreview();return true;}}
 toast(want?('No entry for '+want+' - pick the nearest city'):'Could not detect a zone',1);
 return false;
}

function toast(t,bad){var e=$('toast');e.textContent=t;e.style.borderColor=bad?'#3a1720':'#1b2027';
 e.classList.add('on');setTimeout(function(){e.classList.remove('on')},2800);}
function hex(n){return '#'+('000000'+(n>>>0).toString(16)).slice(-6);}
function unhex(s){return parseInt(s.slice(1),16);}
function pad(n){return (n<10?'0':'')+n;}

/* Live preview of the selected zone, worked out in the browser from the IANA
   name. The device is the authority; this is only here so a wrong choice is
   obvious before you save it. */
function tzPreview(){
 var o=$('tz').options[$('tz').selectedIndex];
 var n=o?o.dataset.name:'';
 var now=new Date();
 try{$('tznow').textContent=n?new Intl.DateTimeFormat('en-GB',{timeZone:n,weekday:'short',
   hour:'2-digit',minute:'2-digit',second:'2-digit',hour12:false}).format(now).toUpperCase():'-';}
 catch(e){$('tznow').textContent='-';}
 $('brnow').textContent=pad(now.getHours())+':'+pad(now.getMinutes())+':'+pad(now.getSeconds());
}
$('tz').addEventListener('change',tzPreview);
setInterval(function(){if($('clock').classList.contains('on'))tzPreview();},1000);

/* Populating the form and refreshing the status panel MUST stay separate.
   They used to be one function on a 15-second timer, which meant the timer
   overwrote whatever you were halfway through typing - paste a bridge URL,
   pause to find your device key, and the URL was silently reverted before you
   ever pressed Save. Only loadStatus() is on the timer now. */
function loadConfig(){
 fetch('/api/config').then(function(r){return r.json()}).then(function(c){C=c;
  ['ssid','ssid2','host','bridge','devId','adminUser'].forEach(function(k){$(k).value=c[k]||''});
  ['pollSec','staleMin','brightDay','brightNight','nightStart','nightEnd'].forEach(function(k){$(k).value=c[k]});
  $('rotation').value=c.rotation;$('showClock').checked=!!c.showClock;
  ['cAccent','cBuy','cSell','cText','cBg'].forEach(function(k){$(k).value=hex(c[k])});

  // Match on the stored POSIX rule. Several cities share a rule (Paris and
  // Berlin are both CET), so prefer the entry whose IANA name also matches.
  var opts=$('tz').options,exact=-1,loose=-1;
  for(var i=0;i<opts.length;i++){
   if(opts[i].value===c.tz){if(opts[i].dataset.name===c.tzName){exact=i;break}if(loose<0)loose=i}}
  if(exact>=0)$('tz').selectedIndex=exact;
  else if(loose>=0)$('tz').selectedIndex=loose;
  else if(!c.tz||c.tz==='UTC0')$('tz').selectedIndex=0;
  tzPreview();

  if(c.hasPass)$('pass').placeholder='saved - leave blank to keep';
  if(c.hasDevKey)$('devKey').placeholder='saved - leave blank to keep';
 }).catch(function(){toast('Could not read settings',1)});
}

function loadStatus(){
 fetch('/api/status').then(function(r){return r.json()}).then(function(s){
  $('sub').innerHTML='v'+s.fw+' &middot; '+(s.ap?'setup mode':s.ssid+' &middot; '+s.ip);
  var g=s.signal,k=s.clock||{},rows=[
   ['Connection', s.ap?'<span class="bad">setup mode</span>':'<span class="ok">'+s.ssid+'</span>'],
   ['IP address', s.ip],
   ['Signal strength', s.rssi+' dBm'],
   ['Bridge', s.httpCode===200?'<span class="ok">new signal received</span>':(s.httpCode===204?'<span class="ok">connected, nothing new</span>':'<span class="bad">'+(s.error||'not configured')+'</span>')],
   ['Device clock', s.timeOk?((k.time||'')+' '+(k.abbr||'')+' <span class="dim">'+(k.offset||'')+'</span>'):'<span class="warn">waiting for NTP</span>'],
   ['Time zone', (k.tzName||'-')+(k.night?' <span class="warn">(night mode)</span>':'')],
   ['Small TLS buffers', s.mfln?'yes (low memory use)':'no (16 kB buffer)'],
   ['Settings on flash', s.cfgOnFlash?'<span class="ok">saved</span>':'<span class="bad">NOT SAVED - will reset on reboot</span>'],
   ['Free memory', s.heap+' bytes'],
   ['Uptime', Math.floor(s.uptime/3600)+'h '+Math.floor(s.uptime%3600/60)+'m'],
   ['Current signal', g.valid?(g.symbol+' '+g.side+' &middot; score '+g.score+' &middot; '+g.ageSec+'s ago'+(g.fresh?'':' <span class="bad">(expired)</span>')):'none yet']
  ];
  $('stat').innerHTML=rows.map(function(r){return '<div class="kv"><span>'+r[0]+'</span><span>'+r[1]+'</span></div>'}).join('');
 }).catch(function(){});
}

function save(){
 var o=$('tz').options[$('tz').selectedIndex];
 var b={ssid:$('ssid').value,pass:$('pass').value,ssid2:$('ssid2').value,pass2:$('pass2').value,
  host:$('host').value,bridge:$('bridge').value.replace(/\/+$/,''),devKey:$('devKey').value,devId:$('devId').value,
  adminUser:$('adminUser').value,adminPass:$('adminPass').value,
  pollSec:+$('pollSec').value,staleMin:+$('staleMin').value,rotation:+$('rotation').value,
  brightDay:+$('brightDay').value,brightNight:+$('brightNight').value,
  nightStart:+$('nightStart').value,nightEnd:+$('nightEnd').value,
  tz:o?o.value:'UTC0',tzName:o?o.dataset.name:'UTC',showClock:$('showClock').checked,
  cAccent:unhex($('cAccent').value),cBuy:unhex($('cBuy').value),cSell:unhex($('cSell').value),
  cText:unhex($('cText').value),cBg:unhex($('cBg').value)};
 fetch('/api/config',{method:'POST',body:JSON.stringify(b)})
  .then(function(r){return r.json()})
  .then(function(r){
   if(!r.ok){toast('SAVE FAILED - could not write to flash. Settings will be lost on reboot.',1);return;}
   toast(r.reboot?'Saved. Rebooting - reconnect in about 20 seconds.':'Saved.');
   $('pass').value='';$('pass2').value='';$('devKey').value='';$('adminPass').value='';
   if(!r.reboot)setTimeout(load,600);})
  .catch(function(){toast('Save failed',1)});
}
function post(u){fetch(u,{method:'POST'}).then(function(){toast('Done')}).catch(function(){toast('Failed',1)});}
function test(){fetch('/api/test',{method:'POST'}).then(function(){toast('Test signal on the screen');setTimeout(loadStatus,500)});}
function scan(){toast('Scanning&hellip;');fetch('/api/scan').then(function(r){return r.json()}).then(function(l){
 $('nets').innerHTML=l.sort(function(a,b){return b.rssi-a.rssi}).map(function(n){return '<option value="'+n.ssid+'">'}).join('');
 toast(l.length+' networks found');});}

function load(){loadConfig();loadStatus();}
load();
setInterval(loadStatus,15000);
</script></body></html>)HTMLPAGE";
