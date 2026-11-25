#pragma once

// Self-contained dashboard page served by the ESP32 (no internet/CDN needed).
// Polls /api/status every second and /api/history every 10 seconds.
const char DASHBOARD_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Polimax Machine 1</title>
<style>
:root{--bg:#0f1318;--panel:#171d25;--line:#26303c;--fg:#e7ecf1;--muted:#8794a3;--run:#34d27b;--stop:#ff5d5d;--accent:#4aa3ff}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font-family:system-ui,-apple-system,"Segoe UI",sans-serif;padding:20px max(16px,env(safe-area-inset-left))}
.wrap{max-width:1100px;margin:0 auto;display:grid;gap:16px}
header{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:8px}
.brand{font-size:13px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}
.brand b{color:var(--fg)}
.conn{display:flex;align-items:center;gap:8px;font-size:13px;color:var(--muted);font-variant-numeric:tabular-nums}
.dot{width:9px;height:9px;border-radius:50%;background:var(--muted)}
.dot.ok{background:var(--run);box-shadow:0 0 10px var(--run)}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:18px;min-width:0}
.top{display:grid;grid-template-columns:minmax(0,1.2fr) minmax(0,2fr);gap:16px}
@media(max-width:760px){.top{grid-template-columns:minmax(0,1fr)}}
.label{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted)}
.state{font-size:52px;font-weight:800;line-height:1.05;margin:10px 0 6px}
.state.run{color:var(--run)}.state.stop{color:var(--stop)}.state.off{color:var(--muted)}
.since{color:var(--muted);font-variant-numeric:tabular-nums}
.kpis{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}
@media(max-width:520px){.kpis{grid-template-columns:repeat(2,minmax(0,1fr))}}
.kpi .v{font-size:26px;font-weight:700;margin-top:6px;font-variant-numeric:tabular-nums}
.kpi .s{font-size:12px;color:var(--muted);margin-top:2px}
.bar{height:6px;border-radius:3px;background:#2a333f;margin-top:10px;overflow:hidden}
.bar i{display:block;height:100%;background:var(--run);width:0}
.chart-head{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:10px}
canvas{display:block;width:100%}
.legend{display:flex;gap:14px;font-size:12px;color:var(--muted)}
.legend span::before{content:"";display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:6px;vertical-align:-1px;background:var(--c)}
.grid2{display:grid;grid-template-columns:minmax(0,2fr) minmax(0,1fr);gap:16px}
@media(max-width:760px){.grid2{grid-template-columns:minmax(0,1fr)}}
ul{list-style:none;margin:0;padding:0;display:grid;gap:2px;max-height:260px;overflow:auto}
li{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid var(--line);font-variant-numeric:tabular-nums}
li .t{color:var(--muted)}
.pill{font-size:12px;font-weight:700;padding:2px 8px;border-radius:999px}
.pill.run{background:#163a27;color:var(--run)}.pill.stop{background:#3d1b1d;color:var(--stop)}
.empty{color:var(--muted);font-size:14px}
</style></head><body><div class="wrap">
<header><div class="brand"><b>Polimax</b> · Machine 1</div>
<div class="conn"><span class="dot" id="dot"></span><span id="conn">connecting…</span></div></header>

<section class="top">
 <div class="panel"><div class="label">Machine status</div>
  <div class="state off" id="state">…</div><div class="since" id="since">–</div></div>
 <div class="panel kpis">
  <div class="kpi"><div class="label">Working time</div><div class="v" id="work">–</div><div class="s">since device start</div></div>
  <div class="kpi"><div class="label">Downtime</div><div class="v" id="down">–</div><div class="s">since device start</div></div>
  <div class="kpi"><div class="label">Availability</div><div class="v" id="avail">–</div><div class="bar"><i id="availbar"></i></div></div>
  <div class="kpi"><div class="label">Pulses total</div><div class="v" id="pulses">–</div><div class="s">from IR sensor</div></div>
  <div class="kpi"><div class="label">Pulses / min</div><div class="v" id="ppm">–</div><div class="s">last 60 s</div></div>
  <div class="kpi"><div class="label">Wi-Fi signal</div><div class="v" id="rssi">–</div><div class="s" id="uptime">uptime –</div></div>
 </div>
</section>

<section class="panel"><div class="chart-head"><div class="label">Machine state · last 30 min</div>
 <div class="legend"><span style="--c:var(--run)">Running</span><span style="--c:var(--stop)">Stopped</span></div></div>
 <canvas id="timeline" height="70"></canvas></section>

<section class="grid2">
 <div class="panel"><div class="chart-head"><div class="label">Pulses per minute · last 30 min</div></div>
  <canvas id="rate" height="200"></canvas></div>
 <div class="panel"><div class="chart-head"><div class="label">Events</div></div>
  <ul id="events"><li class="empty">No starts or stops yet</li></ul></div>
</section>
</div><script>
const $=id=>document.getElementById(id);
const css=n=>getComputedStyle(document.documentElement).getPropertyValue(n).trim();
const fmt=s=>{s=Math.max(0,Math.round(s));const h=Math.floor(s/3600),m=Math.floor(s%3600/60),x=s%60;
  return h?`${h}h ${m}m`:m?`${m}m ${x}s`:`${x}s`};
let hist=null,lastStatus=null;

function setup(c){const r=c.getBoundingClientRect(),d=devicePixelRatio||1,h=+c.getAttribute('height');
  c.width=r.width*d;c.height=h*d;c.style.height=h+'px';const g=c.getContext('2d');g.scale(d,d);return[g,r.width,h]}

function drawTimeline(){const c=$('timeline');const[g,w,h]=setup(c);if(!hist)return;
  const span=hist.interval*hist.capacity,now=hist.now,y=6,bh=h-30;
  g.fillStyle=css('--line');g.fillRect(0,y,w,bh);
  hist.samples.forEach(([t,p,run])=>{const x0=w*(1-(now-t+hist.interval)/span),x1=w*(1-(now-t)/span);
    if(x1<0)return;g.fillStyle=run>=50?css('--run'):css('--stop');g.globalAlpha=.35+.65*Math.abs(run-50)/50;
    g.fillRect(Math.max(0,x0),y,Math.max(1,x1-x0)+.5,bh)});
  g.globalAlpha=1;g.fillStyle=css('--muted');g.font='11px system-ui';g.textBaseline='top';
  [30,20,10,0].forEach(m=>{const x=w*(1-m*60/span);g.textAlign=m==30?'left':m==0?'right':'center';
    g.fillText(m?`-${m} min`:'now',x,y+bh+8)})}

function drawRate(){const c=$('rate');const[g,w,h]=setup(c);if(!hist)return;
  const pad={l:34,b:22,t:8},cw=w-pad.l,ch=h-pad.b-pad.t,span=hist.interval*hist.capacity,now=hist.now;
  const per=hist.samples.map(([t,p])=>[t,p*60/hist.interval]);
  const max=Math.max(10,...per.map(v=>v[1]));const top=Math.ceil(max/10)*10;
  g.font='11px system-ui';g.fillStyle=css('--muted');g.strokeStyle=css('--line');g.lineWidth=1;
  for(let i=0;i<=4;i++){const v=top*i/4,yy=pad.t+ch-ch*v/top;g.beginPath();g.moveTo(pad.l,yy+.5);g.lineTo(w,yy+.5);g.stroke();
    g.textAlign='right';g.textBaseline='middle';g.fillText(Math.round(v),pad.l-6,yy)}
  const bw=cw/hist.capacity;g.fillStyle=css('--accent');
  per.forEach(([t,v])=>{const x=pad.l+cw*(1-(now-t+hist.interval)/span);if(x<pad.l-1||!v)return;
    const bhh=ch*v/top;g.fillRect(x+1,pad.t+ch-bhh,Math.max(1,bw-2),bhh)});
  g.textAlign='center';g.textBaseline='top';
  [30,20,10,0].forEach(m=>{const x=pad.l+cw*(1-m*60/span);g.textAlign=m==30?'left':m==0?'right':'center';
    g.fillText(m?`-${m} min`:'now',x,pad.t+ch+6)})}

function renderEvents(){if(!hist)return;const ul=$('events');const nowMs=Date.now();
  if(!hist.events.length){ul.innerHTML='<li class="empty">No starts or stops yet</li>';return}
  ul.innerHTML=hist.events.slice().reverse().map(([t,run])=>{
    const at=new Date(nowMs-(hist.now-t)*1000).toLocaleTimeString();
    return `<li><span class="t">${at}</span><span class="pill ${run?'run':'stop'}">${run?'STARTED':'STOPPED'}</span></li>`}).join('')}

async function status(){
  try{const d=await(await fetch('/api/status',{cache:'no-store'})).json();lastStatus=d;
    const run=d.machine_status==='RUNNING',s=$('state');s.textContent=d.machine_status;s.className='state '+(run?'run':'stop');
    $('since').textContent=(run?'Running for ':'Stopped for ')+fmt(d.state_seconds);
    $('work').textContent=fmt(d.working_seconds);$('down').textContent=fmt(d.downtime_seconds);
    const tot=d.working_seconds+d.downtime_seconds,av=tot?Math.round(100*d.working_seconds/tot):0;
    $('avail').textContent=av+'%';$('availbar').style.width=av+'%';
    $('pulses').textContent=d.pulse_count.toLocaleString();$('ppm').textContent=d.pulses_last_minute;
    $('rssi').textContent=d.wifi_rssi+' dBm';$('uptime').textContent='uptime '+fmt(d.uptime_seconds);
    $('dot').className='dot ok';$('conn').textContent='live · '+new Date().toLocaleTimeString();
  }catch(e){$('dot').className='dot';$('conn').textContent='offline';const s=$('state');s.textContent='OFFLINE';s.className='state off'}}

async function history(){
  try{hist=await(await fetch('/api/history',{cache:'no-store'})).json();drawTimeline();drawRate();renderEvents()}catch(e){}}

status();history();setInterval(status,1000);setInterval(history,10000);
addEventListener('resize',()=>{drawTimeline();drawRate()});
</script></body></html>)HTML";
