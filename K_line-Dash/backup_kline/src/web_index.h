#pragma once
#include <pgmspace.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0b0d10">
<title>Fiesta Dash</title>
<style>
:root,[data-theme=escuro]{--bg:#0b0d10;--card:#14181d;--line:#232a32;--txt:#e8edf2;--mut:#8a96a3;--acc:#38bdf8;--acc-txt:#04121b;--ok:#22c55e;--warn:#f59e0b;--bad:#ef4444;
 --trk:#1f262e;--redzone:#5b1a1a;--tick:#3a4450;--field:#0e1216;--btn:#1c232b;--nav:rgba(11,13,16,.92);
 --dng-bg:#3a1414;--dng-bd:#6b2020;--dng-txt:#fca5a5;--code-bg:#2a1a0c;--code-bd:#6b3d10;--code-txt:#fbbf24;--pre:#b9c4cf;
 --font:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;--num:var(--font);--r:14px;--bw:14}
[data-theme=claro]{--bg:#eef1f5;--card:#fff;--line:#d5dbe3;--txt:#121820;--mut:#5b6775;--acc:#0369a1;--acc-txt:#fff;--ok:#15803d;--warn:#b45309;--bad:#dc2626;
 --trk:#e3e8ee;--redzone:#fecaca;--tick:#a9b4c0;--field:#f6f8fa;--btn:#e9edf2;--nav:rgba(238,241,245,.94);
 --dng-bg:#fee2e2;--dng-bd:#fca5a5;--dng-txt:#991b1b;--code-bg:#fef3c7;--code-bd:#f59e0b;--code-txt:#92400e;--pre:#334155}
[data-theme=esportivo]{--bg:#070707;--card:#121212;--line:#2a2a2a;--txt:#f5f5f5;--mut:#9a9a9a;--acc:#ff3b30;--acc-txt:#fff;--ok:#30d158;--warn:#ffd60a;--bad:#ff3b30;
 --trk:#1e1e1e;--redzone:#4a0d0a;--tick:#444;--field:#0c0c0c;--btn:#1b1b1b;--nav:rgba(7,7,7,.94);
 --dng-bg:#3d0c0a;--dng-bd:#7a1a14;--dng-txt:#ff8a80;--code-bg:#2b1600;--code-bd:#7a4300;--code-txt:#ffd60a;--pre:#c8c8c8;
 --num:'Arial Narrow',Impact,sans-serif;--r:4px;--bw:18}
[data-theme=retro]{--bg:#020702;--card:#061006;--line:#11331a;--txt:#7dff9a;--mut:#3e9a55;--acc:#39ff6a;--acc-txt:#021004;--ok:#39ff6a;--warn:#ffcc33;--bad:#ff5544;
 --trk:#0c2412;--redzone:#3a1410;--tick:#1e5a2c;--field:#030a03;--btn:#0a1c0d;--nav:rgba(2,7,2,.94);
 --dng-bg:#2a0c08;--dng-bd:#6a2016;--dng-txt:#ff8877;--code-bg:#1d1800;--code-bd:#665200;--code-txt:#ffcc33;--pre:#62d97f;
 --font:ui-monospace,Consolas,'Courier New',monospace;--num:ui-monospace,Consolas,monospace;--r:2px;--bw:10}
[data-theme=noturno]{--bg:#000;--card:#090303;--line:#2a0c0c;--txt:#e05252;--mut:#8a3030;--acc:#c62828;--acc-txt:#000;--ok:#b23b3b;--warn:#ff7043;--bad:#ff1744;
 --trk:#1a0707;--redzone:#3a0808;--tick:#4a1515;--field:#050101;--btn:#140505;--nav:rgba(0,0,0,.94);
 --dng-bg:#2a0505;--dng-bd:#5a1010;--dng-txt:#ff6b6b;--code-bg:#200a00;--code-bd:#5a2500;--code-txt:#ff8a50;--pre:#b84040}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--bg);color:var(--txt);font:15px/1.4 var(--font);transition:background .3s,color .3s}
body{padding:12px 16px calc(80px + env(safe-area-inset-bottom));max-width:1100px;margin:auto}
header{display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:12px}
h1{font-size:17px;font-weight:650;letter-spacing:.2px}
h1 small{color:var(--mut);font-weight:400;font-size:13px;margin-left:6px}
.pill{display:inline-flex;align-items:center;gap:6px;padding:4px 10px;border-radius:99px;background:var(--card);border:1px solid var(--line);font-size:12.5px;color:var(--mut)}
.dot{width:8px;height:8px;border-radius:50%;background:var(--bad)}
.pill.on .dot{background:var(--ok)}
.pill.demo .dot{background:var(--acc)}
#mil{display:none;background:var(--code-bg);border-color:var(--code-bd);color:var(--code-txt);font-weight:600}
.sp{flex:1}
.view{display:none}.view.act{display:block}
.gauges{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:12px;margin-bottom:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:var(--r);padding:14px}
.gauge{position:relative;text-align:center}
.gauge svg{width:100%;max-width:330px;display:block;margin:auto}
.gauge .val{position:absolute;left:0;right:0;top:41%;font-size:clamp(38px,11vw,56px);font-weight:700;font-family:var(--num);font-variant-numeric:tabular-nums;letter-spacing:-1px}
.gauge .unit{position:absolute;left:0;right:0;top:66%;color:var(--mut);font-size:13px;text-transform:uppercase;letter-spacing:1.5px}
.trk{fill:none;stroke:var(--trk);stroke-width:var(--bw);stroke-linecap:round}
.red{fill:none;stroke:var(--redzone);stroke-width:var(--bw)}
.bar{fill:none;stroke:var(--acc);stroke-width:var(--bw);stroke-linecap:round;transition:stroke-dasharray .25s linear}
.tick{stroke:var(--tick);stroke-width:2}
.tl{fill:var(--mut);font-size:10px;text-anchor:middle;dominant-baseline:middle}
.tiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}
.tile .lb{color:var(--mut);font-size:12.5px}
.tile .v{font-size:28px;font-weight:650;font-family:var(--num);font-variant-numeric:tabular-nums;margin-top:2px}
.tile .v small{font-size:13px;color:var(--mut);font-weight:400;margin-left:3px}
.tile .mb{height:5px;background:var(--trk);border-radius:3px;margin-top:8px;overflow:hidden}
.tile .mb i{display:block;height:100%;width:0;background:var(--acc);transition:width .25s}
.tile.warn .v{color:var(--warn)}.tile.warn .mb i{background:var(--warn)}
.tile.bad .v{color:var(--bad)}.tile.bad .mb i{background:var(--bad)}
.tile.na{opacity:.45}
nav{position:fixed;left:0;right:0;bottom:0;display:flex;justify-content:center;gap:6px;padding:8px 16px calc(8px + env(safe-area-inset-bottom));background:var(--nav);backdrop-filter:blur(8px);border-top:1px solid var(--line)}
nav button{flex:1;max-width:200px}
button{background:var(--btn);color:var(--txt);border:1px solid var(--line);border-radius:10px;padding:10px 14px;font:inherit;cursor:pointer}
button.act,button.pri{background:var(--acc);color:var(--acc-txt);border-color:var(--acc);font-weight:600}
button.dng{background:var(--dng-bg);border-color:var(--dng-bd);color:var(--dng-txt)}
button:disabled{opacity:.5}
h2{font-size:15px;margin-bottom:10px}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-bottom:10px}
label{font-size:13px;color:var(--mut);display:flex;flex-direction:column;gap:4px}
input,select{background:var(--field);color:var(--txt);border:1px solid var(--line);border-radius:8px;padding:9px 10px;font:inherit;min-width:0}
input.hex{font-family:ui-monospace,Consolas,monospace;text-transform:uppercase}
.stack{display:grid;gap:12px}
.codes{display:flex;flex-wrap:wrap;gap:8px;margin:10px 0}
.code{font:600 18px ui-monospace,Consolas,monospace;padding:8px 12px;border-radius:8px;background:var(--code-bg);color:var(--code-txt);border:1px solid var(--code-bd)}
pre{background:var(--field);border:1px solid var(--line);border-radius:8px;padding:10px;font:12px/1.5 ui-monospace,Consolas,monospace;white-space:pre-wrap;word-break:break-all;max-height:320px;overflow:auto;color:var(--pre)}
.mut{color:var(--mut);font-size:13px}
.kv{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;font-size:13.5px}
.kv dt{color:var(--mut)}
#bTheme{padding:4px 10px;border-radius:99px;font-size:12.5px;color:var(--mut)}
.themes{display:grid;grid-template-columns:repeat(auto-fill,minmax(120px,1fr));gap:8px}
.themes button{display:flex;flex-direction:column;align-items:flex-start;gap:8px;text-align:left}
.themes button.act{outline:2px solid var(--acc);outline-offset:2px;background:var(--btn);color:var(--txt);font-weight:400}
.lays{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:8px}
.lays button{text-align:left;display:flex;flex-direction:column;gap:3px}
.lays button span{font-size:12px;color:var(--mut)}
.lays button.act{outline:2px solid var(--acc);outline-offset:2px;background:var(--btn);color:var(--txt);font-weight:400}
#bLay{padding:4px 10px;border-radius:99px;font-size:12.5px;color:var(--mut)}
/* clássico: ponteiros */
.cls{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:12px;margin-bottom:12px}
.mini{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:12px}
.ng{text-align:center}.ng svg{width:100%;max-width:330px;display:block;margin:auto}
.ng.sm{padding:8px}.ng.sm .lb{color:var(--mut);font-size:12px;margin-bottom:2px}
.face{fill:var(--field);stroke:var(--line);stroke-width:2}
.tk{stroke:var(--txt);stroke-width:2.5}.tk2{stroke:var(--tick);stroke-width:1.2}
.nl{fill:var(--txt);font-size:13px;text-anchor:middle;dominant-baseline:middle;font-family:var(--num)}
.rz{fill:none;stroke:var(--bad);stroke-width:6;opacity:.85}
.ndl{stroke:var(--bad);stroke-width:3.5;stroke-linecap:round;transform-origin:100px 100px;transition:transform .25s linear}
.hub{fill:var(--txt)}
.ng .rd{fill:var(--txt);font-size:22px;font-weight:700;text-anchor:middle;font-family:var(--num)}
.ng .un{fill:var(--mut);font-size:10px;text-anchor:middle;letter-spacing:1px}
/* barra esportiva */
.shift{display:flex;justify-content:center;gap:12px;margin-bottom:10px}
.shift i{width:16px;height:16px;border-radius:50%;background:var(--trk)}
.shift i.on{background:var(--warn)}.shift i.on.r{background:var(--bad)}
.shift.flash i{background:var(--bad);animation:fl .16s steps(2) infinite}
@keyframes fl{50%{opacity:.15}}
.segs{display:grid;grid-template-columns:repeat(40,1fr);gap:2px;height:48px}
.segs i{background:var(--trk);border-radius:2px;transition:background .1s}
.segs i.on{background:var(--acc)}.segs i.on.w{background:var(--warn)}.segs i.on.r{background:var(--bad)}
.segl{display:flex;justify-content:space-between;color:var(--mut);font-size:11px;margin-top:4px}
.srow{display:flex;align-items:flex-end;justify-content:space-between;gap:12px;margin-top:8px;flex-wrap:wrap}
.bigspd{font:700 clamp(72px,24vw,150px)/1 var(--num);font-variant-numeric:tabular-nums;letter-spacing:-3px}
.rpmd{font:650 clamp(30px,8vw,48px)/1 var(--num);font-variant-numeric:tabular-nums}
.bigspd small,.rpmd small{font-size:16px;color:var(--mut);font-weight:400;letter-spacing:1px;margin-left:6px}
.sbar{margin-bottom:12px}
.tiles.compact .tile{padding:10px 12px}.tiles.compact .tile .v{font-size:22px}
/* digital */
.dgrid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin-bottom:10px}
.dbig .lb{color:var(--mut);font-size:12.5px;text-transform:uppercase;letter-spacing:1.5px}
.dbig .v{font:700 clamp(44px,13vw,110px)/1.05 var(--num);font-variant-numeric:tabular-nums;letter-spacing:-2px}
.dbig .v small{font-size:15px;color:var(--mut);font-weight:400;letter-spacing:0;margin-left:4px}
.dbig.bad .v{color:var(--bad)}
.tiles.big .tile .v{font-size:38px}
.sw{display:flex;gap:4px}.sw i{width:18px;height:18px;border-radius:50%;border:1px solid rgba(128,128,128,.4)}
</style>
</head>
<body>
<header>
  <h1>Fiesta 1.6 <small>IAW 4AFR · K-line</small></h1>
  <span class="pill" id="st"><span class="dot"></span><span id="stt">—</span></span>
  <span class="pill" id="mil">⚠ Injeção</span>
  <span class="sp"></span>
  <span class="pill" id="hz">— req/s</span>
  <button id="bLay" title="Trocar layout">▦ <span id="layName">Layout</span></button>
  <button id="bTheme" title="Trocar cores">◐ <span id="thName">Tema</span></button>
</header>

<section class="view act" id="v-dash"><div id="dash"></div></section>

<section class="view" id="v-dtc">
  <div class="card">
    <h2>Códigos de falha (DTC)</h2>
    <div class="row">
      <button class="pri" id="bRead">Ler falhas</button>
      <button class="dng" id="bClr">Apagar falhas</button>
    </div>
    <div class="codes" id="codes"></div>
    <p class="mut" id="dtcMsg">Leitura via modo 03 (OBD). Para apagar: motor desligado, ignição ligada.</p>
  </div>
</section>

<section class="view" id="v-diag">
  <div class="stack">
    <div class="card">
      <h2>Layout do painel</h2>
      <div class="lays" id="lays"></div>
      <h2 style="margin-top:16px">Cores</h2>
      <div class="themes" id="themes"></div>
      <p class="mut" style="margin-top:8px">Layout e cores ficam salvos neste celular. Os botões ▦ e ◐ no topo alternam rapidamente.</p>
    </div>
    <div class="card">
      <h2>Conexão</h2>
      <dl class="kv" id="info"></dl>
    </div>
    <div class="card">
      <h2>Configuração</h2>
      <div class="row">
        <label>Inicialização<select id="cMode"><option value="0">Automático (varre endereços)</option><option value="1">Rápida (KWP2000)</option><option value="2">5 baud</option></select></label>
        <label>Endereço 5 baud<input class="hex" id="cInit" size="4" maxlength="2"></label>
        <label>Alvo KWP<input class="hex" id="cTgt" size="4" maxlength="2"></label>
        <label>Velocidade<select id="cBaud"><option>10400</option><option>9600</option><option>4800</option><option>2400</option><option>1200</option></select></label>
      </div>
      <div class="row">
        <button class="pri" id="bConn">Aplicar e reconectar</button>
        <button id="bDemo">Demonstração</button>
        <button id="bTrace">Trace TX/RX</button>
      </div>
      <p class="mut">Alvo 33 = endereçamento funcional OBD. Para falar direto com a ECU use o endereço físico dela (ex. 10).</p>
    </div>
    <div class="card">
      <h2>Console K-line</h2>
      <div class="row">
        <input class="hex" id="raw" placeholder="ex: 01 0C   ·   21 01   ·   1A 80" style="flex:1">
        <button class="pri" id="bRaw">Enviar</button>
      </div>
      <p class="mut" style="margin-bottom:8px">Envie só o serviço + dados; cabeçalho e checksum são adicionados automaticamente.</p>
      <pre id="rawOut">—</pre>
    </div>
    <div class="card">
      <h2>Log</h2>
      <pre id="log"></pre>
    </div>
  </div>
</section>

<nav>
  <button class="act" data-v="dash">Painel</button>
  <button data-v="dtc">Falhas</button>
  <button data-v="diag">Diagnóstico</button>
</nav>

<script>
const $=s=>document.querySelector(s);
const THEMES=[
 {id:'escuro',nm:'Escuro',c:['#0b0d10','#14181d','#38bdf8']},
 {id:'claro',nm:'Claro (dia)',c:['#eef1f5','#ffffff','#0369a1']},
 {id:'esportivo',nm:'Esportivo',c:['#070707','#121212','#ff3b30']},
 {id:'retro',nm:'Retrô LCD',c:['#020702','#061006','#39ff6a']},
 {id:'noturno',nm:'Noturno',c:['#000000','#090303','#c62828']},
];
function setTheme(id){
  const t=THEMES.find(x=>x.id===id)||THEMES[0];
  document.documentElement.dataset.theme=t.id;
  document.querySelector('meta[name=theme-color]').content=t.c[0];
  $('#thName').textContent=t.nm;
  document.querySelectorAll('#themes button').forEach(b=>b.classList.toggle('act',b.dataset.t===t.id));
  try{localStorage.setItem('theme',t.id)}catch(e){}
}
$('#themes').innerHTML=THEMES.map(t=>`<button data-t="${t.id}"><span class="sw">${t.c.map(c=>`<i style="background:${c}"></i>`).join('')}</span>${t.nm}</button>`).join('');
document.querySelectorAll('#themes button').forEach(b=>b.onclick=()=>setTheme(b.dataset.t));
$('#bTheme').onclick=()=>{const i=THEMES.findIndex(x=>x.id===document.documentElement.dataset.theme);setTheme(THEMES[(i+1)%THEMES.length].id)};
let savedTheme='escuro';try{savedTheme=localStorage.getItem('theme')||savedTheme}catch(e){}
setTheme(savedTheme);
const NS='http://www.w3.org/2000/svg';
let D=null,view='dash';

function arcGauge(el,{max,step,red,unit,lblDiv=1}){
  const R=80,cx=100,cy=100,a0=135,sweep=270;
  const pt=(a,r)=>[cx+r*Math.cos(a*Math.PI/180),cy+r*Math.sin(a*Math.PI/180)];
  const [x0,y0]=pt(a0,R),[x1,y1]=pt(a0+sweep,R);
  const d=`M${x0} ${y0}A${R} ${R} 0 1 1 ${x1} ${y1}`;
  let s=`<svg viewBox="0 0 200 175"><path class="trk" d="${d}" pathLength="100"/>`;
  if(red){const p=red/max*100;s+=`<path class="red" d="${d}" pathLength="100" stroke-dasharray="0 ${p} ${100-p} 100"/>`}
  s+=`<path class="bar" d="${d}" pathLength="100" stroke-dasharray="0 100"/>`;
  for(let v=0;v<=max;v+=step){const a=a0+sweep*v/max,[ax,ay]=pt(a,R-12),[bx,by]=pt(a,R-18),[lx,ly]=pt(a,R-29);
    s+=`<line class="tick" x1="${ax}" y1="${ay}" x2="${bx}" y2="${by}"/><text class="tl" x="${lx}" y="${ly}">${v/lblDiv}</text>`}
  el.innerHTML=s+`</svg><div class="val">—</div><div class="unit">${unit}</div>`;
  const bar=el.querySelector('.bar'),val=el.querySelector('.val');
  return v=>{
    if(v==null){val.textContent='—';bar.setAttribute('stroke-dasharray','0 100');return}
    const p=Math.max(0,Math.min(100,v/max*100));
    bar.setAttribute('stroke-dasharray',`${p} 100`);
    bar.style.stroke=red&&v>=red?'var(--bad)':'';
    val.textContent=Math.round(v);
  };
}

// Mostrador de ponteiro (240°), como no painel do carro
function needleGauge(el,{min=0,max,step,minor=0,red,unit,lblDiv=1,dec=0,small=false}){
  const cx=100,cy=100,R=88,a0=150,sw=240;
  const ang=v=>a0+sw*(Math.max(min,Math.min(max,v))-min)/(max-min);
  const pt=(a,r)=>[cx+r*Math.cos(a*Math.PI/180),cy+r*Math.sin(a*Math.PI/180)].map(n=>n.toFixed(1));
  let s=`<svg viewBox="0 0 200 ${small?172:200}"><circle class="face" cx="100" cy="100" r="96"/>`;
  if(red!=null){const a=ang(red),b=ang(max),[x0,y0]=pt(a,R-4),[x1,y1]=pt(b,R-4);
    s+=`<path class="rz" d="M${x0} ${y0}A${R-4} ${R-4} 0 ${b-a>180?1:0} 1 ${x1} ${y1}"/>`}
  if(minor)for(let v=min;v<=max+1e-9;v+=minor){const a=ang(v),[ax,ay]=pt(a,R),[bx,by]=pt(a,R-6);
    s+=`<line class="tk2" x1="${ax}" y1="${ay}" x2="${bx}" y2="${by}"/>`}
  for(let v=min;v<=max+1e-9;v+=step){const a=ang(v),[ax,ay]=pt(a,R),[bx,by]=pt(a,R-12),[lx,ly]=pt(a,R-25);
    s+=`<line class="tk" x1="${ax}" y1="${ay}" x2="${bx}" y2="${by}"/><text class="nl" x="${lx}" y="${ly}">${+(v/lblDiv).toFixed(1)}</text>`}
  s+=small?`<text class="rd" x="100" y="150">—</text>`
          :`<text class="un" x="100" y="68">${unit}</text><text class="rd" x="100" y="150">—</text>`;
  s+=`<line class="ndl" x1="84" y1="100" x2="${100+R-10}" y2="100" style="transform:rotate(${a0}deg)"/><circle class="hub" cx="100" cy="100" r="7"/></svg>`;
  el.innerHTML=s;
  const n=el.querySelector('.ndl'),rd=el.querySelector('.rd');
  return v=>{
    if(v==null){rd.textContent='—';n.style.transform=`rotate(${a0}deg)`;return}
    n.style.transform=`rotate(${ang(v)}deg)`;
    rd.textContent=v.toFixed(dec)+(small?' '+unit:'');
  };
}

const TILES=[
 {k:'ect',lb:'Temp. motor',u:'°C',d:0,min:40,max:120,st:v=>v>=108?'bad':v>=100?'warn':''},
 {k:'iat',lb:'Temp. admissão',u:'°C',d:0,min:0,max:80},
 {k:'tps',lb:'Borboleta',u:'%',d:0,min:0,max:100},
 {k:'map',lb:'Pressão coletor',u:'kPa',d:0,min:0,max:105},
 {k:'load',lb:'Carga',u:'%',d:0,min:0,max:100},
 {k:'adv',lb:'Avanço ignição',u:'°',d:1,min:-10,max:50},
 {k:'o2',lb:'Sonda lambda',u:'V',d:2,min:0,max:1},
 {k:'stft',lb:'Correção curta',u:'%',d:1,min:-25,max:25,st:v=>Math.abs(v)>=20?'bad':Math.abs(v)>=12?'warn':''},
 {k:'ltft',lb:'Correção longa',u:'%',d:1,min:-25,max:25,st:v=>Math.abs(v)>=20?'bad':Math.abs(v)>=12?'warn':''},
 {k:'bat',lb:'Bateria',u:'V',d:1,min:10,max:15,st:v=>v<11.8||v>15?'bad':v<12.4?'warn':''},
];

const mk=(cls,html='')=>{const e=document.createElement('div');e.className=cls;e.innerHTML=html;return e};

function tilesBlock(host,cls='',exclude=[]){
  const wrap=mk('tiles '+cls);host.appendChild(wrap);
  const items=TILES.filter(t=>!exclude.includes(t.k)).map(t=>{
    const e=mk('card tile na',`<div class="lb">${t.lb}</div><div class="v">—<small>${t.u}</small></div><div class="mb"><i></i></div>`);
    wrap.appendChild(e);return {t,el:e,v:e.querySelector('.v'),bar:e.querySelector('i')}});
  return vals=>items.forEach(({t,el,v,bar})=>{const x=vals[t.k];
    if(x==null){el.className='card tile na';v.firstChild.nodeValue='—';bar.style.width='0';return}
    el.className='card tile '+(t.st?t.st(x):'');
    v.firstChild.nodeValue=x.toFixed(t.d);
    bar.style.width=Math.max(0,Math.min(100,(x-t.min)/(t.max-t.min)*100))+'%'});
}

const LAYOUTS=[
 {id:'arcos',nm:'Arcos',ds:'Dois arcos + blocos de dados'},
 {id:'classico',nm:'Clássico',ds:'Ponteiros analógicos, como o painel do carro'},
 {id:'barra',nm:'Barra esportiva',ds:'Barra de giro com shift light e velocidade gigante'},
 {id:'digital',nm:'Digital',ds:'Só números grandes'},
];
const RPM_MAX=7000,RPM_RED=6250;

function buildDash(id){
  const host=$('#dash');host.innerHTML='';
  if(id==='classico'){
    const g=mk('cls');host.appendChild(g);
    const a=mk('card ng'),b=mk('card ng');g.append(a,b);
    const r=needleGauge(a,{max:RPM_MAX,step:1000,minor:250,red:RPM_RED,unit:'RPM x1000',lblDiv:1000});
    const sp=needleGauge(b,{max:200,step:20,minor:10,unit:'KM/H'});
    const m=mk('mini');host.appendChild(m);
    const ups=[['ect','Temp. motor',{min:40,max:120,step:20,red:108,unit:'°C'}],
               ['load','Carga',{max:100,step:25,unit:'%'}],
               ['bat','Bateria',{min:10,max:16,step:2,red:15,unit:'V',dec:1}]]
      .map(([k,lb,o])=>{const c=mk('card ng sm',`<div class="lb">${lb}</div><div></div>`);m.appendChild(c);
        return [k,needleGauge(c.lastChild,{...o,small:true})]});
    const t=tilesBlock(host,'',['ect','load','bat']);
    return v=>{r(v.rpm);sp(v.spd);ups.forEach(([k,f])=>f(v[k]));t(v)};
  }
  if(id==='barra'){
    const N=40,SH=5000;
    const c=mk('card sbar',`<div class="shift">${'<i></i>'.repeat(5)}</div><div class="segs">${'<i></i>'.repeat(N)}</div>
      <div class="segl">${[0,1,2,3,4,5,6,7].map(x=>`<span>${x}</span>`).join('')}</div>
      <div class="srow"><div class="bigspd">—<small>km/h</small></div><div class="rpmd">—<small>rpm</small></div></div>`);
    host.appendChild(c);
    const segs=[...c.querySelectorAll('.segs i')];
    segs.forEach((e,i)=>{const f=(i+1)/N*RPM_MAX;if(f>RPM_RED)e.classList.add('r');else if(f>SH)e.classList.add('w')});
    const sh=c.querySelector('.shift'),lights=[...sh.children];lights.forEach((e,i)=>{if(i>=3)e.classList.add('r')});
    const spd=c.querySelector('.bigspd').firstChild,rpm=c.querySelector('.rpmd').firstChild;
    const t=tilesBlock(host,'compact');
    return v=>{const r=v.rpm;
      segs.forEach((e,i)=>e.classList.toggle('on',r!=null&&r>=(i+.5)/N*RPM_MAX));
      lights.forEach((e,i)=>e.classList.toggle('on',r!=null&&r>=SH+(RPM_RED-SH)*i/5));
      sh.classList.toggle('flash',r!=null&&r>=RPM_RED);
      spd.nodeValue=v.spd==null?'—':Math.round(v.spd);
      rpm.nodeValue=r==null?'—':Math.round(r);
      t(v)};
  }
  if(id==='digital'){
    const g=mk('dgrid');host.appendChild(g);
    const big=[['rpm','Rotação','rpm',RPM_RED],['spd','Velocidade','km/h']].map(([k,lb,u,red])=>{
      const c=mk('card dbig',`<div class="lb">${lb}</div><div class="v">—<small>${u}</small></div>`);g.appendChild(c);
      return {k,red,c,v:c.querySelector('.v').firstChild}});
    const t=tilesBlock(host,'big');
    return v=>{big.forEach(b=>{const x=v[b.k];b.v.nodeValue=x==null?'—':Math.round(x);b.c.classList.toggle('bad',!!b.red&&x>=b.red)});t(v)};
  }
  const g=mk('gauges');host.appendChild(g);
  const a=mk('card gauge'),b=mk('card gauge');g.append(a,b);
  const r=arcGauge(a,{max:RPM_MAX,step:1000,red:RPM_RED,unit:'rpm',lblDiv:1000});
  const sp=arcGauge(b,{max:200,step:20,unit:'km/h'});
  const t=tilesBlock(host);
  return v=>{r(v.rpm);sp(v.spd);t(v)};
}

let dashUpdate=()=>{};
const dashVals=()=>D?{...(D.v||{}),bat:D.vbat!=null?D.vbat:(D.v||{}).vecu}:{};
function setLayout(id){
  const l=LAYOUTS.find(x=>x.id===id)||LAYOUTS[0];
  document.documentElement.dataset.layout=l.id;
  dashUpdate=buildDash(l.id);
  dashUpdate(dashVals());
  $('#layName').textContent=l.nm;
  document.querySelectorAll('#lays button').forEach(b=>b.classList.toggle('act',b.dataset.l===l.id));
  try{localStorage.setItem('layout',l.id)}catch(e){}
}
$('#lays').innerHTML=LAYOUTS.map(l=>`<button data-l="${l.id}">${l.nm}<span>${l.ds}</span></button>`).join('');
document.querySelectorAll('#lays button').forEach(b=>b.onclick=()=>setLayout(b.dataset.l));
$('#bLay').onclick=()=>{const i=LAYOUTS.findIndex(x=>x.id===document.documentElement.dataset.layout);setLayout(LAYOUTS[(i+1)%LAYOUTS.length].id)};
let savedLayout='arcos';try{savedLayout=localStorage.getItem('layout')||savedLayout}catch(e){}
setLayout(savedLayout);

function render(){
  const v=D.v||{};
  dashUpdate(dashVals());
  const st=$('#st');st.className='pill'+(D.demo?' demo':D.conn&&D.obd?' on':'');
  $('#stt').textContent=D.status;
  $('#hz').textContent=D.hz.toFixed(1)+' req/s';
  const m=$('#mil');m.style.display=v.mil?'inline-flex':'none';
  if(v.mil)m.textContent='⚠ Injeção · '+v.ndtc+' falha(s)';
  if(view==='diag'){
    $('#info').innerHTML=[['Status',D.status],['Protocolo',D.proto],['ECU',D.ecu],['Keybytes',D.kb],
      ['OBD padrão',D.obd?'sim':'não'],['PIDs',D.sup.join(', ')||'—'],['Erros de eco',D.echoErr],
      ['Bateria (ADC)',D.vbat!=null?D.vbat.toFixed(1)+' V':'—']]
      .map(([a,b])=>`<dt>${a}</dt><dd>${b}</dd>`).join('');
    $('#bDemo').classList.toggle('act',D.demo);$('#bTrace').classList.toggle('act',D.trace);
    if(!cfgLoaded){$('#cMode').value=D.cfg.mode;$('#cInit').value=D.cfg.init;$('#cTgt').value=D.cfg.tgt;$('#cBaud').value=D.cfg.baud;cfgLoaded=true}
  }
}
let cfgLoaded=false;

async function api(path,opt){const r=await fetch(path,opt);return r.json()}
const post=(p,body)=>api(p,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(body||{})});

async function poll(){
  try{const c=new AbortController();const t=setTimeout(()=>c.abort(),1500);
    D=await (await fetch('/api/data',{signal:c.signal})).json();clearTimeout(t);render()}
  catch(e){$('#st').className='pill';$('#stt').textContent='Sem conexão com o ESP32'}
  setTimeout(poll,250);
}
poll();

async function pollLog(){
  if(view==='diag'){try{const l=await api('/api/log');const p=$('#log');const atEnd=p.scrollTop+p.clientHeight>=p.scrollHeight-20;
    p.textContent=l.join('\n');if(atEnd)p.scrollTop=p.scrollHeight}catch(e){}}
  setTimeout(pollLog,1000);
}
pollLog();

document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{
  view=b.dataset.v;document.querySelectorAll('nav button').forEach(x=>x.classList.toggle('act',x===b));
  document.querySelectorAll('.view').forEach(s=>s.classList.toggle('act',s.id==='v-'+view));
  if(view==='diag')cfgLoaded=false;
});

async function busy(btn,fn){btn.disabled=true;try{await fn()}catch(e){alert('Erro: '+e)}btn.disabled=false}

$('#bRead').onclick=e=>busy(e.target,async()=>{
  $('#dtcMsg').textContent='Lendo...';const r=await api('/api/dtc');
  $('#codes').innerHTML=(r.codes||[]).map(c=>`<span class="code">${c}</span>`).join('');
  $('#dtcMsg').textContent=!r.ok?r.err:r.codes.length?`${r.codes.length} falha(s). Resposta: ${r.raw}`:`Nenhuma falha registrada. Resposta: ${r.raw}`});
$('#bClr').onclick=e=>{if(!confirm('Apagar todas as falhas da ECU? (motor desligado, ignição ligada)'))return;
  busy(e.target,async()=>{const r=await post('/api/dtc/clear');$('#dtcMsg').textContent=r.ok?'Falhas apagadas.':r.err;if(r.ok)$('#codes').innerHTML=''})};
$('#bConn').onclick=e=>busy(e.target,()=>post('/api/connect',{mode:$('#cMode').value,init:$('#cInit').value,tgt:$('#cTgt').value,baud:$('#cBaud').value}));
$('#bDemo').onclick=e=>busy(e.target,()=>post('/api/demo',{on:D&&D.demo?'0':'1'}));
$('#bTrace').onclick=e=>busy(e.target,()=>post('/api/trace',{on:D&&D.trace?'0':'1'}));
const sendRaw=()=>busy($('#bRaw'),async()=>{
  const r=await post('/api/raw',{hex:$('#raw').value});const o=$('#rawOut');
  const line=r.err?`! ${r.err}`:`> ${r.tx}\n`+(r.frames.length?r.frames.map(f=>`< [${f.src}] ${f.data}`).join('\n'):'< (sem resposta)');
  o.textContent=line+'\n\n'+(o.textContent==='—'?'':o.textContent)});
$('#bRaw').onclick=sendRaw;$('#raw').onkeydown=e=>{if(e.key==='Enter')sendRaw()};
</script>
</body>
</html>
)HTML";
