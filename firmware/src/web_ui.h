/**
 * DrFX GodMode - settings page, served straight from flash.
 *
 * One self-contained file: no CDN, no framework, works with no internet.
 * Stored in PROGMEM and sent with server.send_P so it never occupies RAM.
 */
#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DrFX GodMode</title><style>
:root{--bg:#0b0b12;--card:#14142400;--line:#242440;--txt:#e6e6f0;--dim:#8b8ba7;--acc:#8b5cf6;--ok:#22dd77;--bad:#ff4d5e}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--txt);font:15px/1.55 system-ui,-apple-system,Segoe UI,sans-serif}
.wrap{max-width:620px;margin:0 auto;padding:24px 18px 64px}
h1{font-size:17px;letter-spacing:.18em;color:var(--acc);margin:0 0 2px}
.sub{color:var(--dim);font-size:13px;margin:0 0 20px}
nav{display:flex;gap:4px;border-bottom:1px solid var(--line);margin-bottom:20px;overflow-x:auto}
nav button{background:none;border:0;border-bottom:2px solid transparent;color:var(--dim);padding:9px 13px;font:inherit;font-size:14px;cursor:pointer;white-space:nowrap}
nav button.on{color:var(--txt);border-bottom-color:var(--acc)}
section{display:none}section.on{display:block}
label{display:block;margin:14px 0 5px;font-size:13px;color:var(--dim)}
input,select{width:100%;background:#101021;border:1px solid var(--line);color:var(--txt);border-radius:8px;padding:10px 12px;font:inherit}
input[type=color]{padding:3px;height:40px}
input:focus,select:focus{outline:0;border-color:var(--acc)}
.row{display:flex;gap:12px}.row>div{flex:1}
.hint{color:var(--dim);font-size:12px;margin-top:5px}
button.act{background:var(--acc);border:0;color:#fff;border-radius:8px;padding:11px 18px;font:inherit;font-weight:600;cursor:pointer}
button.ghost{background:none;border:1px solid var(--line);color:var(--txt)}
button.danger{background:none;border:1px solid #55223a;color:var(--bad)}
.bar{display:flex;gap:10px;flex-wrap:wrap;margin-top:24px;padding-top:18px;border-top:1px solid var(--line)}
.kv{display:flex;justify-content:space-between;padding:9px 0;border-bottom:1px solid var(--line);font-size:14px}
.kv span:first-child{color:var(--dim)}
.kv span:last-child{font-variant-numeric:tabular-nums}
#toast{position:fixed;left:50%;bottom:22px;transform:translateX(-50%) translateY(90px);background:#1c1c33;border:1px solid var(--line);padding:11px 20px;border-radius:10px;transition:.25s;font-size:14px}
#toast.on{transform:translateX(-50%) translateY(0)}
code{background:#16162a;padding:2px 6px;border-radius:4px;color:#c4b5fd;font-size:12px;word-break:break-all}
.ok{color:var(--ok)}.bad{color:var(--bad)}
</style></head><body><div class="wrap">
<h1>DRFX GOD MODE</h1><p class="sub" id="sub">loading&hellip;</p>

<nav>
  <button class="on" data-t="status">Status</button>
  <button data-t="wifi">Wi-Fi</button>
  <button data-t="bridge">Bridge</button>
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
  <div class="row"><div><input id="ssid" list="nets" placeholder="MyWiFi"><datalist id="nets"></datalist></div>
  <div style="flex:0 0 auto"><button class="ghost act" onclick="scan()">Scan</button></div></div>
  <label>Password</label><input id="pass" type="password" placeholder="leave blank to keep current">
  <label>Backup network (optional)</label><input id="ssid2" placeholder="">
  <label>Backup password</label><input id="pass2" type="password" placeholder="leave blank to keep current">
  <label>Device name on your network</label><input id="host" placeholder="godmode">
  <p class="hint">Reachable afterwards at <code>http://godmode.local</code></p>
  <div class="bar"><button class="act" onclick="save()">Save &amp; reboot</button></div>
</section>

<section id="bridge">
  <label>Bridge URL</label><input id="bridge" placeholder="https://drfx-godmode-bridge.yourname.workers.dev">
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

<section id="display">
  <div class="row">
    <div><label>Daytime brightness</label><input id="brightDay" type="range" min="5" max="255"></div>
    <div><label>Night brightness</label><input id="brightNight" type="range" min="0" max="255"></div>
  </div>
  <div class="row">
    <div><label>Night starts (hour)</label><input id="nightStart" type="number" min="0" max="23"></div>
    <div><label>Night ends (hour)</label><input id="nightEnd" type="number" min="0" max="23"></div>
  </div>
  <label>Screen rotation</label>
  <select id="rotation"><option value="0">0&deg;</option><option value="1">90&deg;</option><option value="2">180&deg;</option><option value="3">270&deg;</option></select>
  <label>Time zone offset from UTC</label>
  <select id="tzMinutes"></select>
  <label><input id="showClock" type="checkbox" style="width:auto;margin-right:8px">Show a clock when there is no signal</label>
  <div class="row">
    <div><label>Accent</label><input id="cAccent" type="color"></div>
    <div><label>Buy</label><input id="cBuy" type="color"></div>
    <div><label>Sell</label><input id="cSell" type="color"></div>
  </div>
  <div class="row">
    <div><label>Text</label><input id="cText" type="color"></div>
    <div><label>Background</label><input id="cBg" type="color"></div>
  </div>
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

/* 15-minute steps so the +05:45 and +12:45 zones exist too */
(function(){var s=$('tzMinutes');for(var m=-720;m<=840;m+=15){var o=document.createElement('option');
 var sg=m<0?'-':'+',a=Math.abs(m);o.value=m;o.textContent='UTC'+sg+String(Math.floor(a/60)).padStart(2,'0')+':'+String(a%60).padStart(2,'0');s.appendChild(o);}})();

function toast(t,bad){var e=$('toast');e.textContent=t;e.style.borderColor=bad?'#55223a':'#242440';
 e.classList.add('on');setTimeout(function(){e.classList.remove('on')},2600);}
function hex(n){return '#'+('000000'+(n>>>0).toString(16)).slice(-6);}
function unhex(s){return parseInt(s.slice(1),16);}

/* Populating the form and refreshing the status panel MUST stay separate.
   They used to be one function on a 15-second timer, which meant the timer
   overwrote whatever you were halfway through typing - paste a bridge URL,
   pause to find your device key, and the URL was silently reverted before you
   ever pressed Save. Only loadStatus() is on the timer now. */
function loadConfig(){
 fetch('/api/config').then(function(r){return r.json()}).then(function(c){C=c;
  ['ssid','ssid2','host','bridge','devId','adminUser'].forEach(function(k){$(k).value=c[k]||''});
  ['pollSec','staleMin','brightDay','brightNight','nightStart','nightEnd'].forEach(function(k){$(k).value=c[k]});
  $('rotation').value=c.rotation;$('tzMinutes').value=c.tzMinutes;$('showClock').checked=!!c.showClock;
  ['cAccent','cBuy','cSell','cText','cBg'].forEach(function(k){$(k).value=hex(c[k])});
  if(c.hasPass)$('pass').placeholder='saved - leave blank to keep';
  if(c.hasDevKey)$('devKey').placeholder='saved - leave blank to keep';
 }).catch(function(){toast('Could not read settings',1)});
}

function loadStatus(){
 fetch('/api/status').then(function(r){return r.json()}).then(function(s){
  $('sub').innerHTML='v'+s.fw+' &middot; '+(s.ap?'setup mode':s.ssid+' &middot; '+s.ip);
  var g=s.signal,rows=[
   ['Connection', s.ap?'<span class="bad">setup mode</span>':'<span class="ok">'+s.ssid+'</span>'],
   ['IP address', s.ip],
   ['Signal strength', s.rssi+' dBm'],
   ['Bridge', s.httpCode===200?'<span class="ok">new signal received</span>':(s.httpCode===204?'<span class="ok">connected, nothing new</span>':'<span class="bad">'+(s.error||'not configured')+'</span>')],
   ['Small TLS buffers', s.mfln?'yes (low memory use)':'no (16 kB buffer)'],
   ['Clock synced', s.timeOk?'yes':'not yet'],
   ['Settings on flash', s.cfgOnFlash?'<span class="ok">saved</span>':'<span class="bad">NOT SAVED - will reset on reboot</span>'],
   ['Free memory', s.heap+' bytes'],
   ['Uptime', Math.floor(s.uptime/3600)+'h '+Math.floor(s.uptime%3600/60)+'m'],
   ['Current signal', g.valid?(g.symbol+' '+g.side+' &middot; score '+g.score+' &middot; '+g.ageSec+'s ago'+(g.fresh?'':' <span class="bad">(expired)</span>')):'none yet']
  ];
  $('stat').innerHTML=rows.map(function(r){return '<div class="kv"><span>'+r[0]+'</span><span>'+r[1]+'</span></div>'}).join('');
 }).catch(function(){});
}

function save(){
 var b={ssid:$('ssid').value,pass:$('pass').value,ssid2:$('ssid2').value,pass2:$('pass2').value,
  host:$('host').value,bridge:$('bridge').value.replace(/\/+$/,''),devKey:$('devKey').value,devId:$('devId').value,
  adminUser:$('adminUser').value,adminPass:$('adminPass').value,
  pollSec:+$('pollSec').value,staleMin:+$('staleMin').value,rotation:+$('rotation').value,
  brightDay:+$('brightDay').value,brightNight:+$('brightNight').value,
  nightStart:+$('nightStart').value,nightEnd:+$('nightEnd').value,
  tzMinutes:+$('tzMinutes').value,showClock:$('showClock').checked,
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
