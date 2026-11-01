// Panel WWW sterownika - jedna strona, bez zewnetrznych zasobow (dziala bez internetu).
#pragma once

static const char PANEL_HTML[] = R"HTML(<!doctype html>
<html lang="pl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sterownik CO/CWU</title>
<style>
:root{--bg:#f3f4f6;--card:#fff;--fg:#1c1f24;--mut:#6b7280;--line:#e2e5e9;--piec:#d9480f;--boiler:#1864ab;--ok:#2b8a3e;--bad:#c92a2a;--warn:#e8590c;--btn:#1c1f24;--btnfg:#fff}
@media(prefers-color-scheme:dark){:root{--bg:#131518;--card:#1d2024;--fg:#e8eaed;--mut:#9aa0a6;--line:#2f3338;--piec:#ff8b50;--boiler:#5ea8ff;--ok:#5fd07a;--bad:#ff6b6b;--warn:#ffa94d;--btn:#e8eaed;--btnfg:#131518}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
header,main{max-width:1040px;margin:auto;padding:0 16px}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;padding-top:16px;padding-bottom:12px}
h1{font-size:19px;margin:0}
h2{font-size:14px;margin:0 0 10px;color:var(--mut);font-weight:600;text-transform:uppercase;letter-spacing:.04em}
main{display:grid;gap:14px;grid-template-columns:repeat(auto-fit,minmax(290px,1fr));padding-bottom:40px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px 16px;min-width:0}
.wide{grid-column:1/-1}
.muted{color:var(--mut)}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;background:var(--mut);margin-right:6px}
.dot.on{background:var(--ok)}.dot.off{background:var(--bad)}
.sensor .name{display:flex;justify-content:space-between;align-items:center}
.sensor .t{font-size:46px;font-weight:650;letter-spacing:-1px;font-variant-numeric:tabular-nums;line-height:1.1;margin:6px 0}
.sensor .t small{font-size:20px;font-weight:500;color:var(--mut)}
.piec .t{color:var(--piec)}.boiler .t{color:var(--boiler)}
.sensor.err .t{color:var(--bad);font-size:28px}
.bar{height:6px;background:var(--line);border-radius:3px;overflow:hidden;margin:8px 0 4px}
.bar i{display:block;height:100%;background:var(--ok)}
.bar i.hot{background:var(--bad)}
.trend{font-variant-numeric:tabular-nums}
.alarm{grid-column:1/-1;background:var(--bad);color:#fff;border-radius:12px;padding:12px 16px;display:none;align-items:center;justify-content:space-between;gap:12px}
.alarm.acked{background:var(--warn)}
.alarm ul{margin:0;padding-left:18px}
button{font:inherit;border:0;border-radius:8px;padding:8px 14px;background:var(--btn);color:var(--btnfg);cursor:pointer}
button.ghost{background:transparent;color:var(--fg);border:1px solid var(--line)}
.alarm button{background:#fff;color:#000}
table{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums}
th,td{padding:6px 4px;text-align:right;border-bottom:1px solid var(--line)}
th:first-child,td:first-child{text-align:left}
th{color:var(--mut);font-weight:600;font-size:13px}
.tabs{display:flex;gap:6px}
.tabs button{padding:4px 10px;font-size:13px}
.tabs button.sel{background:var(--btn);color:var(--btnfg)}
.tabs button:not(.sel){background:transparent;color:var(--fg);border:1px solid var(--line)}
.charthead{display:flex;justify-content:space-between;align-items:center;gap:8px;margin-bottom:8px;flex-wrap:wrap}
canvas{width:100%;height:260px;display:block}
.legend span{margin-right:14px;font-size:13px}
.legend i{display:inline-block;width:14px;height:3px;vertical-align:middle;margin-right:5px}
.pump{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid var(--line)}
.pump:last-child{border:0}
.tag{font-size:12px;font-weight:700;padding:3px 8px;border-radius:6px;border:1px solid var(--line)}
.tag.on{background:var(--ok);color:#fff;border-color:var(--ok)}
form fieldset{border:1px solid var(--line);border-radius:10px;padding:10px 12px;margin:0 0 12px}
legend{font-weight:600;padding:0 6px}
.row{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:5px 0}
.row input[type=number]{width:96px;font:inherit;padding:5px 8px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--fg);text-align:right}
.row input[type=checkbox]{width:20px;height:20px}
.formgrid{display:grid;gap:0 16px;grid-template-columns:repeat(auto-fit,minmax(300px,1fr))}
.kv{display:grid;grid-template-columns:auto 1fr;gap:4px 14px}
.kv div:nth-child(odd){color:var(--mut)}
#msg{margin-left:10px}
.okic{color:var(--ok);vertical-align:-3px}
</style></head><body>
<header><h1>Sterownik CO / CWU</h1><div><span id="conn" class="dot"></span><span id="clock" class="muted">—</span> · <a href="/config" style="color:inherit">Konfiguracja</a></div></header>
<main>
<div id="alarm" class="alarm"><div><b>Alarm</b><ul id="alarmList"></ul></div><button id="ack">Wycisz</button></div>

<section class="card sensor piec" id="c-piec"><div class="name"><h2>Piec</h2><span class="muted" id="piec-lim"></span></div>
<div class="t" id="piec-t">—</div><div class="bar"><i id="piec-bar"></i></div>
<div class="trend muted" id="piec-trend"></div></section>

<section class="card sensor boiler" id="c-boiler"><div class="name"><h2>Bojler</h2><span class="muted" id="boiler-lim"></span></div>
<div class="t" id="boiler-t">—</div><div class="bar"><i id="boiler-bar"></i></div>
<div class="trend muted" id="boiler-trend"></div></section>

<section class="card" id="c-pumps"><h2>Pompy</h2>
<div class="pump"><div><b>Pompa CO</b><div class="muted" id="co-reason"></div></div><span class="tag" id="co-tag">—</span></div>
<div class="pump"><div><b>Pompa CWU</b><div class="muted" id="cwu-reason"></div></div><span class="tag" id="cwu-tag">—</span></div>
</section>

<section class="card wide"><div class="charthead"><h2 style="margin:0">Wykres temperatur</h2>
<div class="legend"><span><i style="background:var(--piec)"></i>Piec</span><span><i style="background:var(--boiler)"></i>Bojler</span><span class="muted">- - - limity</span></div>
<div class="tabs" id="range"><button data-h="1">1 h</button><button data-h="6" class="sel">6 h</button><button data-h="12">12 h</button><button data-h="24">24 h</button></div></div>
<canvas id="chart"></canvas></section>

<section class="card"><h2>Statystyki</h2>
<table><thead><tr><th></th><th>Piec dziś</th><th>Bojler dziś</th><th>Piec wcz.</th><th>Bojler wcz.</th></tr></thead><tbody id="stats"></tbody></table>
<p class="muted" id="pumpstats" style="margin:10px 0 0"></p></section>

<section class="card"><h2>System</h2><div class="kv" id="sys"></div>
<p style="margin:12px 0 0;display:flex;gap:8px;flex-wrap:wrap"><button class="ghost" id="resetStats">Zeruj statystyki</button><button class="ghost" id="buzz">Test brzęczyka</button><span class="muted" id="buzzmsg"></span></p></section>

<section class="card wide"><h2>Ustawienia</h2>
<form id="settings"><div class="formgrid" id="fields"></div>
<button type="submit">Zapisz</button><span id="msg" class="muted"></span></form></section>
</main>
<script>
const $=s=>document.querySelector(s);
// "ptaszek" zamiast napisu OK (aria-label dla czytnikow ekranu)
const CHECK='<svg class="okic" viewBox="0 0 16 16" width="16" height="16" role="img" aria-label="OK"><path d="M3 8.5l3.2 3.2L13 4.8" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/></svg>';
const okOr=v=>v==='OK'?CHECK:v;
let S=null,H=null,range=6;
const ARROW={up:'↗',down:'↘',flat:'→',none:'·'};
const f1=v=>v==null?'—':Number(v).toFixed(1);
const css=n=>getComputedStyle(document.documentElement).getPropertyValue(n).trim();
async function j(u,o){const r=await fetch(u,o);if(!r.ok)throw new Error(r.status);return r.json()}
function hm(min){min=Math.round(min);return Math.floor(min/60)+' h '+String(min%60).padStart(2,'0')+' min'}
function ago(s){return s<0?'—':s<90?s+' s temu':Math.round(s/60)+' min temu'}

function sensor(id,s){
  const card=$('#c-'+id);card.classList.toggle('err',!s.ok);
  $('#'+id+'-t').innerHTML=s.ok?f1(s.temp)+'<small> °C</small>':(s.found?'Błąd odczytu':'Brak czujnika');
  $('#'+id+'-lim').textContent='limit '+f1(s.limit)+' °C';
  const p=s.ok?Math.max(0,Math.min(100,s.temp/s.limit*100)):0,b=$('#'+id+'-bar');
  b.style.width=p+'%';b.classList.toggle('hot',p>=100);
  $('#'+id+'-trend').textContent=s.trend==='none'?'Trend: zbieranie danych…':
    `${ARROW[s.trend]} Δ ${f1(s.trendDiff)} °C  ·  od zmiany trendu ${s.trendTime}`;
}
function pump(id,p){
  const t=$('#'+id+'-tag');t.textContent=p.on?'ZAŁ':'WYŁ';t.classList.toggle('on',p.on);
  $('#'+id+'-reason').textContent=p.reason+(p.output?'':' · brak wyjścia (tylko podgląd)');
}
function stats(d,y){
  const g=(o,k,s)=>o?(o[s][k]==null?'—':f1(o[s][k])):'—';
  const row=(l,k)=>`<tr><td>${l}</td><td>${g(d,k,'piec')}</td><td>${g(d,k,'boiler')}</td><td>${g(y,k,'piec')}</td><td>${g(y,k,'boiler')}</td></tr>`;
  const ab=(o,s)=>o?o[s].minutesAbove+' min':'—';
  $('#stats').innerHTML=row('Minimum °C','min')+row('Maksimum °C','max')+row('Średnia °C','avg')+
    `<tr><td>Powyżej limitu</td><td>${ab(d,'piec')}</td><td>${ab(d,'boiler')}</td><td>${ab(y,'piec')}</td><td>${ab(y,'boiler')}</td></tr>`+
    `<tr><td>Alarmy</td><td colspan="2">${d.alarms}</td><td colspan="2">${y?y.alarms:'—'}</td></tr>`;
  $('#pumpstats').textContent=`Dziś: pompa CO ${hm(d.pumps.coMinutes)} (${d.pumps.coStarts}× zał.), pompa CWU ${hm(d.pumps.cwuMinutes)} (${d.pumps.cwuStarts}× zał.)`;
}
async function status(){
  try{
    const s=await j('/api/status');$('#conn').className='dot on';
    $('#clock').textContent=s.time?new Date(s.time*1000).toLocaleString('pl-PL'):'brak czasu NTP';
    sensor('piec',s.piec);sensor('boiler',s.boiler);pump('co',s.pumps.co);pump('cwu',s.pumps.cwu);
    // pump section switched off in the settings: hide its card and daily pump line
    $('#c-pumps').style.display=s.pumpsEnabled?'':'none';$('#pumpstats').style.display=s.pumpsEnabled?'':'none';
    const a=$('#alarm');
    if(s.alarms.length){a.style.display='flex';a.classList.toggle('acked',s.alarms.every(x=>x.acked));
      $('#alarmList').innerHTML=s.alarms.map(x=>`<li>${x.text}${x.acked?' (wyciszony)':''}</li>`).join('');
      $('#ack').style.display=s.alarms.every(x=>x.acked)?'none':'';}
    else a.style.display='none';
    stats(s.today,s.yesterday);
    const n=s.net,up=s.uptime;
    const kv={'Wi-Fi':n.wifi?`${n.ssid} (${n.rssi} dBm)`:'brak połączenia','Adres':n.wifi?`${n.ip} · ${n.host}.local`:'—',
      'Firebase':`${okOr(n.firebase)}${n.fbAgo>=0?' · '+ago(n.fbAgo):''} · wysłano ${n.fbSent}${n.fbError?' · '+n.fbError:''}`,
      'Czujniki na magistrali':n.sensorsOnBus,'Czas pracy':`${Math.floor(up/86400)} d ${String(Math.floor(up%86400/3600)).padStart(2,'0')}:${String(Math.floor(up%3600/60)).padStart(2,'0')}`,
      'Temperatura układu':n.chipTemp==null?'—':`${f1(n.chipTemp)} °C (CPU ${n.cpuMhz} MHz)`,
      'Wolna pamięć':Math.round(n.heap/1024)+' kB','Wersja':s.version};
    $('#sys').innerHTML=Object.entries(kv).map(([k,v])=>`<div>${k}</div><div>${v}</div>`).join('');
  }catch(e){$('#conn').className='dot off'}
}
function draw(){
  if(!H)return;const c=$('#chart'),dpr=devicePixelRatio||1,W=c.clientWidth,Hh=c.clientHeight;
  c.width=W*dpr;c.height=Hh*dpr;const g=c.getContext('2d');g.scale(dpr,dpr);
  const n=range*60,P=H.piec.slice(-n),B=H.boiler.slice(-n),L=[];
  if(S){L.push([S.values.alarmPiec,css('--piec')]);L.push([S.values.alarmBoiler,css('--boiler')])}
  let lo=1e9,hi=-1e9;for(const v of P.concat(B))if(v!=null){lo=Math.min(lo,v);hi=Math.max(hi,v)}
  for(const[l]of L){lo=Math.min(lo,l);hi=Math.max(hi,l)}
  if(lo>hi){lo=0;hi=100}lo=Math.floor((lo-2)/5)*5;hi=Math.ceil((hi+2)/5)*5;if(hi-lo<10)hi=lo+10;
  const pl=38,pr=10,pt=8,pb=24,w=W-pl-pr,h=Hh-pt-pb;
  const X=i=>pl+w*(i+(n-P.length))/(n-1),Y=v=>pt+h*(hi-v)/(hi-lo);
  g.font='12px system-ui';g.fillStyle=css('--mut');g.strokeStyle=css('--line');g.lineWidth=1;
  const step=(hi-lo)>40?10:5;
  for(let v=lo;v<=hi;v+=step){g.beginPath();g.moveTo(pl,Y(v));g.lineTo(W-pr,Y(v));g.stroke();g.textAlign='right';g.fillText(v+'°',pl-6,Y(v)+4)}
  const hs=range<=1?0.25:range<=6?1:range<=12?2:4;g.textAlign='center';
  for(let t=0;t<=range;t+=hs){const x=pl+w*(1-t/range);g.beginPath();g.moveTo(x,pt);g.lineTo(x,pt+h);g.stroke();
    g.fillText(t===0?'teraz':'-'+(t<1?t*60+' min':t+' h'),x,Hh-6)}
  g.setLineDash([5,4]);g.lineWidth=1.5;
  for(const[l,col]of L){g.strokeStyle=col;g.globalAlpha=.6;g.beginPath();g.moveTo(pl,Y(l));g.lineTo(W-pr,Y(l));g.stroke()}
  g.setLineDash([]);g.globalAlpha=1;g.lineWidth=2;g.lineJoin='round';
  for(const[arr,col]of[[P,css('--piec')],[B,css('--boiler')]]){g.strokeStyle=col;g.beginPath();let pen=false;
    arr.forEach((v,i)=>{if(v==null){pen=false;return}pen?g.lineTo(X(i),Y(v)):g.moveTo(X(i),Y(v));pen=true});g.stroke()}
  if(!P.length){g.fillStyle=css('--mut');g.textAlign='center';g.fillText('Zbieranie danych — pierwsza próbka po 1 minucie',pl+w/2,pt+h/2)}
}
async function history(){try{H=await j('/api/history');draw()}catch(e){}}
async function loadSettings(){
  S=await j('/api/settings');const groups={};
  for(const f of S.fields)(groups[f.group]=groups[f.group]||[]).push(f);
  $('#fields').innerHTML=Object.entries(groups).filter(([gname])=>S.values.pumps||!/^Pompa /.test(gname)).map(([gname,fs])=>{
    const note=(gname==='Pompa CO'&&!S.pumpOutputs.co)||(gname==='Pompa CWU'&&!S.pumpOutputs.cwu)?
      '<div class="muted" style="font-size:13px">Brak skonfigurowanego wyjścia – logika działa tylko jako podgląd.</div>':'';
    return `<fieldset><legend>${gname}</legend>${note}`+fs.map(f=>{const v=S.values[f.key];
      const inp=f.type==='bool'?`<input type="checkbox" name="${f.key}" ${v?'checked':''}>`:
        `<span><input type="number" name="${f.key}" value="${v}" min="${f.min}" max="${f.max}" step="${f.step}"> ${f.unit}</span>`;
      return `<label class="row"><span>${f.label}</span>${inp}</label>`}).join('')+'</fieldset>'}).join('');
  draw();
}
$('#settings').onsubmit=async e=>{e.preventDefault();const body=new URLSearchParams();
  for(const f of S.fields){const el=e.target.elements[f.key];if(!el)continue;body.append(f.key,f.type==='bool'?(el.checked?'1':'0'):el.value)}
  try{S=await j('/api/settings',{method:'POST',body});await loadSettings();status();$('#msg').innerHTML='Zapisano '+CHECK}
  catch(err){$('#msg').textContent='Błąd zapisu: '+err.message}
  setTimeout(()=>$('#msg').textContent='',4000)};
$('#ack').onclick=async()=>{await fetch('/api/ack',{method:'POST'});status()};
$('#resetStats').onclick=async()=>{if(confirm('Wyzerować statystyki dzisiejsze i wczorajsze?')){await fetch('/api/stats/reset',{method:'POST'});status()}};
$('#buzz').onclick=async()=>{const m=$('#buzzmsg');
  try{await j('/api/buzzer/test',{method:'POST'});m.textContent='Piszczy? Jeśli nie, zmień typ brzęczyka w ustawieniach (Alarmy) i spróbuj ponownie.'}
  catch(e){m.textContent='Brak pinu brzęczyka – ustaw go na stronie Konfiguracja.'}};
$('#range').onclick=e=>{const h=e.target.dataset.h;if(!h)return;range=+h;
  document.querySelectorAll('#range button').forEach(b=>b.classList.toggle('sel',b===e.target));draw()};
addEventListener('resize',draw);
matchMedia('(prefers-color-scheme:dark)').addEventListener('change',draw);
loadSettings().catch(()=>{});status();history();
setInterval(status,5000);setInterval(history,60000);
</script></body></html>)HTML";
