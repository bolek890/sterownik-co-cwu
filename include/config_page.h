// Strona konfiguracji /config: Wi-Fi, Firebase, dostep, piny, aktualizacja oprogramowania.
// Bez zewnetrznych zasobow - dziala tez w sieci konfiguracyjnej sterownika (bez internetu).
#pragma once

static const char CONFIG_HTML[] = R"HTML(<!doctype html>
<html lang="pl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Konfiguracja sterownika</title>
<style>
:root{--bg:#f3f4f6;--card:#fff;--fg:#1c1f24;--mut:#6b7280;--line:#e2e5e9;--acc:#1864ab;--ok:#2b8a3e;--bad:#c92a2a;--btn:#1c1f24;--btnfg:#fff}
@media(prefers-color-scheme:dark){:root{--bg:#131518;--card:#1d2024;--fg:#e8eaed;--mut:#9aa0a6;--line:#2f3338;--acc:#5ea8ff;--ok:#5fd07a;--bad:#ff6b6b;--btn:#e8eaed;--btnfg:#131518}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
header,main{max-width:720px;margin:auto;padding:0 16px}
header{display:flex;justify-content:space-between;align-items:center;gap:12px;padding-top:16px;padding-bottom:10px}
h1{font-size:19px;margin:0}
h2{font-size:14px;margin:0 0 4px;color:var(--mut);font-weight:600;text-transform:uppercase;letter-spacing:.04em}
a{color:var(--acc)}
main{display:grid;gap:14px;padding-bottom:40px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px 16px}
.hint{color:var(--mut);font-size:13px;margin:2px 0 10px}
label.f{display:block;margin:10px 0 0}
label.f>span{display:block;font-size:13px;color:var(--mut);margin-bottom:3px}
input[type=text],input[type=password],input[type=number],input[type=url],input[type=email]{width:100%;font:inherit;padding:8px 10px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--fg)}
.clr{font-size:13px;color:var(--mut);display:inline-flex;gap:5px;align-items:center;margin-top:4px}
.pins{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:0 12px}
button{font:inherit;border:0;border-radius:8px;padding:9px 16px;background:var(--btn);color:var(--btnfg);cursor:pointer}
button.ghost{background:transparent;color:var(--fg);border:1px solid var(--line)}
button:disabled{opacity:.5;cursor:default}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.nets{list-style:none;padding:0;margin:8px 0 0;max-height:220px;overflow:auto;border:1px solid var(--line);border-radius:8px;display:none}
.nets li{padding:8px 10px;border-bottom:1px solid var(--line);cursor:pointer;display:flex;justify-content:space-between}
.nets li:last-child{border:0}
.nets li:hover{background:var(--bg)}
.status{font-size:13px;color:var(--mut)}
.ok{color:var(--ok)}.bad{color:var(--bad)}
.okic{color:var(--ok);vertical-align:-3px}
.bar{height:10px;background:var(--line);border-radius:5px;overflow:hidden;margin:10px 0 4px;display:none}
.bar i{display:block;height:100%;width:0;background:var(--acc);transition:width .2s}
.save{position:sticky;bottom:0;background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px 16px;display:flex;gap:12px;align-items:center;justify-content:space-between;flex-wrap:wrap}
#msg{font-size:14px}
.kv{display:grid;grid-template-columns:auto 1fr;gap:2px 12px;font-size:14px}
.kv div:nth-child(odd){color:var(--mut)}
</style></head><body>
<header><h1>Konfiguracja sterownika</h1><a href="/panel">Panel ›</a></header>
<main>
<section class="card"><h2>Stan</h2><div class="kv" id="state"></div></section>

<form id="cfg" autocomplete="off">
<section class="card"><h2>Wi-Fi</h2>
<p class="hint">Sieć 2,4 GHz, z którą sterownik ma się łączyć.</p>
<div class="row"><button type="button" class="ghost" id="scan">Szukaj sieci</button><span class="status" id="scanst"></span></div>
<ul class="nets" id="nets"></ul>
<label class="f"><span>Nazwa sieci (SSID)</span><input type="text" name="ssid" maxlength="32" autocomplete="off"></label>
<label class="f"><span>Hasło</span><input type="password" name="wifiPass" maxlength="63" autocomplete="new-password"></label>
</section>

<section class="card" style="margin-top:14px"><h2>Firebase</h2>
<p class="hint">Dane jak w starym sterowniku. Zapis co 5 min pod <code>/readings/&lt;czas&gt;</code>. Puste pola = wysyłka wyłączona.</p>
<label class="f"><span>Klucz API (API_KEY)</span><input type="text" name="fbKey"></label>
<label class="f"><span>Adres bazy (DATABASE_URL)</span><input type="url" name="fbUrl" placeholder="https://….firebaseio.com/"></label>
<label class="f"><span>E-mail użytkownika (opcjonalnie – puste = konto anonimowe)</span><input type="email" name="fbEmail" autocomplete="off" placeholder="zostaw puste"></label>
<label class="f"><span>Hasło użytkownika</span><input type="password" name="fbPass" autocomplete="new-password"></label>
</section>

<section class="card" style="margin-top:14px"><h2>Dostęp</h2>
<label class="f"><span>Nazwa urządzenia w sieci (adres <span id="hostex">…</span>.local)</span><input type="text" name="host" maxlength="32"></label>
<label class="f"><span>Użytkownik panelu</span><input type="text" name="webUser" autocomplete="off"></label>
<label class="f"><span>Hasło panelu (puste = bez logowania)</span><input type="password" name="webPass" autocomplete="new-password"></label>
<label class="f"><span>Hasło aktualizacji z PlatformIO (OTA, puste = wyłączona)</span><input type="password" name="otaPass" autocomplete="new-password"></label>
</section>

<section class="card" style="margin-top:14px"><h2>Piny</h2>
<p class="hint">Numery GPIO. Puste pole przy pinie opcjonalnym = niepodłączony. <span id="pinhint"></span></p>
<div class="pins" id="pins"></div>
<p class="hint" id="fixed" style="margin-top:10px"></p>
</section>

<div class="save" style="margin-top:14px"><span id="msg" class="status">Zmiany działają po ponownym uruchomieniu.</span>
<button type="submit" id="savebtn">Zapisz i uruchom ponownie</button></div>
</form>

<section class="card"><h2>Aktualizacja oprogramowania</h2>
<p class="hint">Plik <code id="fwfile">firmware.bin</code> z PlatformIO (dla <span id="board">…</span>). Ustawienia i statystyki zostają.</p>
<div class="row"><input type="file" id="fw" accept=".bin"><button type="button" id="upload" disabled>Wgraj</button></div>
<div class="bar" id="bar"><i></i></div><div class="status" id="upmsg"></div>
</section>
</main>
<script>
const $=s=>document.querySelector(s);let C=null;
// "ptaszek" zamiast napisu OK (aria-label dla czytnikow ekranu)
const CHECK='<svg class="okic" viewBox="0 0 16 16" width="16" height="16" role="img" aria-label="OK"><path d="M3 8.5l3.2 3.2L13 4.8" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/></svg>';
const okOr=v=>v==='OK'?CHECK:v;
async function j(u,o){const r=await fetch(u,o);const t=await r.json().catch(()=>({}));if(!r.ok)throw new Error(t.error||('HTTP '+r.status));return t}
function secret(name,isSet){const el=document.querySelector(`[name=${name}]`);el.value='';
  el.placeholder=isSet?'•••••• ustawione – puste = bez zmian':'nie ustawiono';
  const old=el.parentNode.querySelector('.clr');if(old)old.remove();
  if(isSet)el.insertAdjacentHTML('afterend',`<label class="clr"><input type="checkbox" name="${name}_clear"> usuń</label>`)}
async function load(){
  C=await j('/api/config');const f=$('#cfg').elements,w=C.wifi;
  f.ssid.value=w.ssid;f.fbKey.value=C.firebase.apiKey;f.fbUrl.value=C.firebase.url;f.fbEmail.value=C.firebase.email;
  f.host.value=C.access.host;f.webUser.value=C.access.webUser;$('#hostex').textContent=C.access.host;
  secret('wifiPass',w.passSet);secret('fbPass',C.firebase.passSet);secret('webPass',C.access.webPassSet);secret('otaPass',C.access.otaPassSet);
  $('#pinhint').textContent=C.pinHint;$('#fwfile').textContent=C.firmwareFile;$('#board').textContent=C.board;
  $('#pins').innerHTML=C.pins.map(p=>`<label class="f"><span>${p.label}</span><input type="number" name="${p.key}" min="0" max="48" value="${p.value<0?'':p.value}" placeholder="${p.optional?'brak':''}" ${p.optional?'':'required'}></label>`).join('');
  $('#fixed').textContent='Stałe (config.h): '+Object.entries(C.fixedPins).filter(([,v])=>v>=0).map(([k,v])=>`${k} GPIO${v}`).join(', ');
  const kv={'Wersja':`${C.version} (${C.board})`,'Wi-Fi':w.connected?`<span class="ok">połączono</span> z ${w.ssid} (${w.rssi} dBm), IP ${w.ip}`:`<span class="bad">brak połączenia</span>`,
    'Sieć konfiguracyjna':w.apActive?`włączona: ${w.apSsid} → 192.168.4.1`:'wyłączona',
    'Firebase':okOr(C.firebase.status)+(C.firebase.error?` – ${C.firebase.error}`:'')};
  $('#state').innerHTML=Object.entries(kv).map(([k,v])=>`<div>${k}</div><div>${v}</div>`).join('');
}
async function waitBack(msgEl,text){msgEl.textContent=text;const t0=Date.now();
  await new Promise(r=>setTimeout(r,4000));
  for(;;){try{await j('/api/config');location.reload();return}catch(e){}
    if(Date.now()-t0>60000){msgEl.innerHTML='Sterownik nie odpowiada pod tym adresem. Jeśli zmieniono sieć Wi-Fi, połącz się z nią i otwórz <b>'+$('#cfg').elements.host.value+'.local</b> lub adres IP z ekranu sterownika.';return}
    await new Promise(r=>setTimeout(r,2000))}}
$('#cfg').onsubmit=async e=>{e.preventDefault();const b=$('#savebtn'),m=$('#msg');b.disabled=true;m.className='status';
  try{await j('/api/config',{method:'POST',body:new URLSearchParams(new FormData(e.target))});
    m.className='ok';await waitBack(m,'Zapisano. Ponowne uruchamianie…')}
  catch(err){m.className='bad';m.textContent=err.message;b.disabled=false}};
async function scan(){const st=$('#scanst');st.textContent='Szukam…';$('#scan').disabled=true;
  try{for(let i=0;i<30;i++){const r=await j('/api/wifi/scan');if(!r.scanning){
      const l=$('#nets');r.networks.sort((a,b)=>b.rssi-a.rssi);
      l.innerHTML=r.networks.map(n=>`<li data-s="${encodeURIComponent(n.ssid)}"><span>${n.ssid.replace(/</g,'&lt;')}</span><span class="status">${n.open?'otwarta · ':''}${n.rssi} dBm</span></li>`).join('')||'<li>Nie znaleziono sieci</li>';
      l.style.display='block';st.textContent='';break}
    await new Promise(r=>setTimeout(r,800))}}catch(e){st.textContent='Błąd: '+e.message}
  $('#scan').disabled=false}
$('#scan').onclick=scan;
$('#nets').onclick=e=>{const li=e.target.closest('li');if(!li||!li.dataset.s)return;
  const f=$('#cfg').elements;f.ssid.value=decodeURIComponent(li.dataset.s);f.wifiPass.focus()};
$('#fw').onchange=()=>{$('#upload').disabled=!$('#fw').files.length};
$('#upload').onclick=()=>{const file=$('#fw').files[0];if(!file)return;
  const bar=$('#bar'),m=$('#upmsg'),x=new XMLHttpRequest(),fd=new FormData();fd.append('firmware',file,file.name);
  bar.style.display='block';m.className='status';m.textContent='Wysyłanie…';$('#upload').disabled=true;
  x.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded/e.total*100);bar.firstChild.style.width=p+'%';m.textContent=`Wysyłanie ${p}%`}};
  x.onload=()=>{let r={};try{r=JSON.parse(x.responseText)}catch(e){}
    if(x.status===200&&r.ok){m.className='ok';waitBack(m,'Wgrano. Ponowne uruchamianie…')}
    else{m.className='bad';m.textContent='Błąd: '+(r.error||('HTTP '+x.status));$('#upload').disabled=false}};
  x.onerror=()=>{m.className='bad';m.textContent='Przerwane połączenie';$('#upload').disabled=false};
  x.open('POST','/update');x.send(fd)};
load().catch(e=>{$('#state').textContent='Błąd: '+e.message});
</script></body></html>)HTML";
