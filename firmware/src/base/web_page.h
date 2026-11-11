// TermoMade base — the single web page (served at /). Plain HTML + JS, no external files (no internet).
// Test it on a PC with tools/web_mock/mock_server.py (serves this page with a fake API).
#pragma once

static const char kWebPage[] = R"HTML(<!doctype html>
<html lang="it"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TermoMade</title>
<style>
:root{--bg:#111;--card:#1d1f23;--fg:#f2f2f2;--dim:#9aa0a6;--acc:#ff9f1a;--teal:#0d98a8;--red:#c62828;--btn:#2c3038}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px system-ui,sans-serif}
main{max-width:520px;margin:0 auto;padding:12px 16px 40px}
h1{font-size:18px;margin:4px 0 12px;display:flex;justify-content:space-between}
.card{background:var(--card);border-radius:12px;padding:14px;margin-bottom:12px}
.row{display:flex;gap:8px;align-items:center;justify-content:space-between;flex-wrap:wrap}
.big{font-size:64px;font-weight:700;line-height:1}
.hot{color:var(--acc)}
.dim{color:var(--dim);font-size:14px}
button{background:var(--btn);color:var(--fg);border:0;border-radius:10px;padding:14px 10px;font:600 16px system-ui;flex:1;min-width:72px}
button.on{background:var(--teal)} button.warn{background:var(--red)} button.acc{background:var(--acc);color:#111}
button.sq{flex:0 0 64px;font-size:28px;padding:8px}
.sp{font-size:36px;font-weight:700}
#banner{background:var(--red);border-radius:10px;padding:10px;margin-bottom:12px;display:none}
#advice{color:#7cc4ff}
details{background:var(--card);border-radius:12px;padding:10px 14px;margin-bottom:12px}
summary{font-weight:600;padding:6px 0;cursor:pointer}
table{width:100%;border-collapse:collapse}
td{padding:6px 2px;border-top:1px solid #33363c;font-size:14px;overflow-wrap:anywhere}
td:last-child{text-align:right;white-space:nowrap}
input,select{background:#0c0d0f;color:var(--fg);border:1px solid #444;border-radius:8px;padding:8px;font-size:15px;width:96px}
input.ok{border-color:#2e7d32} input.bad{border-color:var(--red)}
label{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:6px 0;font-size:15px}
pre{white-space:pre-wrap;font-size:13px;color:var(--dim);margin:0}
#off{flex:0 0 auto;min-width:64px;padding:8px 12px;font-size:14px}
</style></head><body><main>
<h1><span id="mode">TermoMade</span><span><span id="clock">--:--</span></span></h1>
<div id="banner"></div>
<div class="card">
 <div class="row">
  <div><div class="big" id="t">--.-°</div><div class="dim" id="rh"></div></div>
  <div style="text-align:right"><div class="dim">Impostata</div><div class="sp" id="sp">--°</div>
   <div class="row" style="justify-content:flex-end;margin-top:6px">
    <button class="sq" onclick="adj(-0.5)">−</button><button class="sq" onclick="adj(0.5)">+</button></div></div>
 </div>
 <p id="status" style="margin:12px 0 2px"></p><div id="advice"></div>
</div>
<div class="card"><div class="row">
 <button id="m0" onclick="cmd('mode',0)">AUTO</button><button id="m1" onclick="cmd('mode',1)">GIORNO</button>
 <button id="m2" onclick="cmd('mode',2)">NOTTE</button><button id="m3" onclick="away()">VIA</button>
 <button id="off" onclick="off()">OFF</button>
</div></div>
<div class="card"><div class="row">
 <span class="dim" id="today"></span>
 <button onclick="cmd('boost',60)" style="flex:0 0 auto">Boost 60'</button>
 <button onclick="cmd('stop')" style="flex:0 0 auto">Stop</button>
</div></div>

<details id="cfg"><summary>Impostazioni</summary>
 <label>Giorno (comfort) dalle <input type="time" id="sd"></label>
 <label>Notte dalle <input type="time" id="sn"></label>
 <div class="row"><button onclick="sched()">Applica orario a tutti i giorni</button></div>
 <hr style="border-color:#33363c">
 <label>Spegnimento schermo (s) <input type="number" id="st" min="10" max="255"></label>
 <label>Luminosità massima <input type="number" id="sx" min="10" max="255"></label>
 <label>Luminosità al buio <input type="number" id="sm" min="1" max="255"></label>
 <label>Luminosità automatica <select id="sa"><option value="1">sì</option><option value="0">no</option></select></label>
 <div class="row"><button onclick="applyScreen()">Applica schermo</button>
  <button onclick="cmd('save')">Salva adesso</button></div>
</details>
<details id="adv"><summary>Parametri avanzati (tutti)</summary>
 <p class="dim">Ogni modifica vale subito ed è salvata dopo 10 s. I valori fuori scala sono corretti dal termostato.</p>
 <table id="ptab"></table>
</details>
<details><summary>Diagnostica</summary><pre id="diag"></pre></details>
<p class="dim" id="conn"></p>
</main><script>
let S=null,offArm=0,timeSent=0,cfgFilled=false;
const $=id=>document.getElementById(id);
const post=(url,o)=>fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
  body:new URLSearchParams(o)}).then(r=>r.json()).catch(()=>({ok:false}));
function cmd(c,v,more){return post('/api/cmd',Object.assign({c:c},v===undefined?{}:{v:v},more||{})).then(r=>{setTimeout(poll,400);return r})}
function adj(d){if(!S)return;const hi=S.mode==3?12:25;let v=Math.min(hi,Math.max(5,S.sp+d));S.sp=v;$('sp').textContent=v.toFixed(1)+'°';cmd('sp',v)}
function away(){if(S&&(S.mode==3||S.mode==4))cmd('arrive');else cmd('mode',3)}
function off(){if(S&&S.mode==4)return;if(Date.now()<offArm){offArm=0;cmd('mode',4)}else{offArm=Date.now()+3000;render()}}
const mmss=s=>Math.floor(s/60)+':'+String(s%60).padStart(2,'0');
const hm=m=>String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0');
const mins=t=>{const a=t.split(':');return a.length==2?(+a[0])*60+(+a[1]):NaN};
function statusText(s){
 if(!s.heating)return'Riscaldamento spento';
 if(s.waiting)return'Avvio: attendo i sensori…';
 if(s.arrival)return s.valve?'Arrivo: riscaldo al massimo, '+Math.ceil(s.arrival_left_s/60)+' min':'Arrivo: la valvola apre tra '+s.lock_s+' s';
 if(s.valve)return s.next_s<0?'Riscaldamento continuo':'Riscaldamento: chiude tra '+mmss(s.next_s);
 if(s.next_s<0)return'Nessuna richiesta di calore';
 return(s.lock_s>0&&s.lock_s>=s.next_s?'Pausa valvola: riapre tra ':'In pausa: prossimo impulso tra ')+mmss(s.next_s)}
function alarmText(s){const a=s.alarms;
 if(s.limp)return'EMERGENZA: nessun sensore valido';
 if(a&1)return'Sensore stanza non risponde';if(a&8)return'Sensori in disaccordo';
 if(a&16)return'Ora non impostata';if(a&32)return'Valvola aperta da molto tempo';
 if(a&64)return'Superato il budget giornaliero';return''}
function render(){const s=S;if(!s)return;
 const names=['AUTO','GIORNO','NOTTE','VIA','SPENTO'];
 let m=names[s.mode];
 if(s.boost)m='BOOST '+Math.ceil(s.boost_left_s/60)+"'";
 else if(s.arrival)m='ARRIVO '+Math.ceil(s.arrival_left_s/60)+"'";
 else if(s.mode==0)m='AUTO · '+(s.period==0?'Giorno':'Notte')+(s.preheat?' (pre)':s.override?' *':'');
 $('mode').textContent=m;
 $('clock').textContent=s.time_valid?String(s.hh).padStart(2,'0')+':'+String(s.mm).padStart(2,'0'):'--:--';
 $('t').textContent=(s.t==null?'--.-':s.t.toFixed(1))+'°';$('t').className='big'+(s.valve?' hot':'');
 $('rh').textContent=s.rh==null?'':'Umidità '+s.rh.toFixed(0)+'%';
 $('sp').textContent=s.heating?s.sp.toFixed(1)+'°':'--';
 $('status').textContent=statusText(s);$('status').className=s.valve?'hot':'';
 const al=alarmText(s);$('banner').style.display=al?'block':'none';$('banner').textContent=al;
 $('advice').textContent=s.humidity==2?'Aria secca: umidificare':s.humidity==3?'Aria umida: arieggiare':'';
 for(let i=0;i<3;i++)$('m'+i).className=s.mode==i?'on':'';
 const arr=s.mode==3||s.mode==4;$('m3').textContent=arr?'ARRIVO':'VIA';$('m3').className=arr?'acc':'';
 const armed=Date.now()<offArm;$('off').textContent=armed?'OK?':'OFF';$('off').className=(armed||s.mode==4)?'warn':'';
 $('today').textContent='Oggi: '+Math.round(s.billed_s/60)+' min aperta, '+s.pulses+' impulsi';
 $('diag').textContent=`T ${s.t} (${s.src})  stanza ${s.t_room}  CYD ${s.t_base} (corr ${s.offset})
sp ${s.sp}  u ${s.u}  P ${s.p}  I ${s.i}
valvola ${s.valve?'APERTA':'chiusa'} (${s.reason})  prossima ${s.next_s}s  blocco ${s.lock_s}s  relè ${s.relay?'ON':'off'}
allarmi 0x${s.alarms.toString(16)}  rugiada ${s.dew}  luce ${s.light}%
nodo ${s.node?s.node_age_s+' s fa, '+s.rssi+' dBm':'NON accoppiato'}  SD ${s.sd?'ok':'NO'}
fw ${s.fw}  acceso da ${Math.floor(s.uptime_s/3600)}h${Math.floor(s.uptime_s%3600/60)}m  reset ${s.reset}`;
 if(!cfgFilled){cfgFilled=true;if(s.sched_day>=0)$('sd').value=hm(s.sched_day);if(s.sched_night>=0)$('sn').value=hm(s.sched_night);
  $('st').value=s.scr_timeout;$('sx').value=s.scr_max;$('sm').value=s.scr_min;$('sa').value=s.scr_auto}
}
function syncTime(s){ // the phone is the best clock: set it when unknown or more than 30 s off
 const now=Math.floor(Date.now()/1000);
 if(Date.now()-timeSent>60000&&(!s.time_valid||Math.abs(s.unix-now)>30)){timeSent=Date.now();cmd('time',now)}}
function poll(){fetch('/api/status').then(r=>r.json()).then(s=>{S=s;$('conn').textContent='';render();syncTime(s)})
 .catch(()=>{$('conn').textContent='Termostato non raggiungibile (sei collegato al Wi-Fi TermoMade?)'})}
function sched(){const a=mins($('sd').value),b=mins($('sn').value);if(isNaN(a)||isNaN(b)||a==b)return;cmd('sched',a,{v2:b})}
function applyScreen(){cmd('screen',$('st').value,{v2:$('sx').value,v3:$('sm').value,v4:$('sa').value})}
function loadParams(){fetch('/api/params').then(r=>r.json()).then(ps=>{const t=$('ptab');t.innerHTML='';
 ps.forEach(p=>{const tr=t.insertRow();tr.insertCell().innerHTML=p.name+(p.unit?' <span class="dim">'+p.unit+'</span>':'');
  const i=document.createElement('input');i.type='number';i.step=p.kind?1:p.step;i.value=p.value;
  i.onchange=()=>post('/api/set',{name:p.name,value:i.value}).then(r=>{i.className=r.ok?'ok':'bad';setTimeout(loadParams,1500)});
  tr.insertCell().appendChild(i)})})}
$('adv').addEventListener('toggle',e=>{if(e.target.open)loadParams()});
poll();setInterval(poll,2000);setInterval(render,500);
</script></body></html>
)HTML";
