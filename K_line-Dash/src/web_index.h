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
#live{display:none;color:var(--ok);border-color:var(--ok)}
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
/* rede CAN */
.tblw{overflow-x:auto;margin-top:6px}
table.cant{border-collapse:collapse;font:13px/1.2 ui-monospace,Consolas,monospace;width:100%}
.cant th{color:var(--mut);font-weight:400;font-size:11px;text-align:center;padding:2px}
.cant td{padding:5px 2px;text-align:center;border-top:1px solid var(--line)}
.cant td.id{text-align:left;color:var(--acc);font-weight:600}
.cant td.hz{color:var(--mut);font-size:11px;text-align:right;padding-right:6px}
.cant td.b{cursor:pointer;border-radius:4px}
.cant td.st{color:var(--mut);opacity:.45}
.cant td.ch{background:var(--acc);color:var(--acc-txt)}
.cant td.sel{outline:2px solid var(--warn);outline-offset:-2px}
#spark{width:100%;height:60px;display:block;margin:8px 0;background:var(--field);border:1px solid var(--line);border-radius:8px}
#spark polyline{fill:none;stroke:var(--acc);stroke-width:2;vector-effect:non-scaling-stroke}
textarea{width:100%;background:var(--field);color:var(--txt);border:1px solid var(--line);border-radius:8px;padding:9px 10px;font:13px/1.5 ui-monospace,Consolas,monospace}
label.chk{flex-direction:row;align-items:center;gap:6px}
code{font-family:ui-monospace,Consolas,monospace;font-size:12.5px}
.sw{display:flex;gap:4px}.sw i{width:18px;height:18px;border-radius:50%;border:1px solid rgba(128,128,128,.4)}
</style>
</head>
<body>
<header>
  <h1>Fiesta 1.6 <small>IAW 4AFR · CAN</small></h1>
  <span class="pill" id="st"><span class="dot"></span><span id="stt">—</span></span>
  <span class="pill" id="mil">⚠ Injeção</span>
  <span class="sp"></span>
  <span class="pill" id="live" title="Transmissão contínua: o ESP32 envia cada dado no instante em que chega">● ao vivo</span>
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

<section class="view" id="v-net">
  <div class="stack">
    <div class="card">
      <h2>Rede CAN (modo escuta)</h2>
      <p class="mut" id="netMsg">—</p>
      <div class="row" style="margin-top:8px">
        <button class="pri" id="bSniffOn">Ativar modo escuta</button>
        <label class="chk"><input type="checkbox" id="onlyDyn"> Só IDs que mudam</label>
        <button id="bSniffReset">Zerar mín/máx</button>
      </div>
      <div class="row">
        <button id="bRec">● Gravar</button>
        <input id="recLabel" placeholder="ex: marcha lenta, 3000 rpm, ventoinha ligou" style="flex:1">
        <button id="bMark">Marcar</button>
        <button id="bSave">Baixar gravação</button>
      </div>
      <p class="mut" id="recInfo">Grave enquanto muda uma coisa de cada vez (acelerar, esquentar, ligar faróis) e marque cada momento.</p>
      <div class="tblw"><table class="cant" id="canT"></table></div>
      <p class="mut" style="margin-top:6px">Toque num byte para analisar. Fundo colorido = mudou agora; apagado = nunca mudou.</p>
    </div>
    <div class="card">
      <h2>Byte selecionado</h2>
      <div id="selInfo" class="mut">Nenhum. Toque num byte da tabela.</div>
      <svg id="spark" viewBox="0 0 300 60" preserveAspectRatio="none"><polyline points=""/></svg>
      <div class="row">
        <label>Tamanho<select id="sLen"><option value="1">1 byte</option><option value="2">2 bytes</option></select></label>
        <label>Ordem<select id="sOrd"><option value="B">B (big)</option><option value="L">L (little)</option></select></label>
        <label>Usar como<select id="sKey"></select></label>
        <label>Mult.<input id="sMul" value="1" size="6"></label>
        <label>Soma<input id="sAdd" value="0" size="6"></label>
      </div>
      <div class="row"><button class="pri" id="bAddSig">Adicionar sinal</button><span class="mut">Resultado: <b id="sRes">—</b></span></div>
    </div>
    <div class="card">
      <h2>Sinais mapeados</h2>
      <textarea id="sigs" rows="6" spellcheck="false" placeholder="rpm,201,0,2,B,0.25,0&#10;ect,420,0,1,B,1,-40"></textarea>
      <div class="row" style="margin-top:8px"><button class="pri" id="bSigs">Salvar sinais</button><span class="mut" id="sigMsg"></span></div>
      <p class="mut">Uma linha por sinal: <code>chave,ID,byte,tamanho,ordem,mult,soma[,máscara]</code>. Valor = bytes × mult + soma. Ordem B, L, BS ou LS (S = com sinal). Máscara opcional em hexadecimal, ex. <code>0FFF</code>. Linhas com # são comentários. Os sinais alimentam o Painel no modo escuta.</p>
    </div>
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
        <label>Modo<select id="cMode"><option value="0">Diagnóstico OBD (pede dados)</option><option value="1">Escuta da rede (só ouve)</option></select></label>
        <label>Velocidade CAN<select id="cRate"><option value="-1">Automática (ouve o barramento)</option><option value="0">500 kbps</option><option value="1">250 kbps</option><option value="2">125 kbps</option></select></label>
        <label>ID de pedido<input class="hex" id="cReq" size="5" maxlength="3"></label>
        <label>Cristal MCP2515<select id="cXtal"><option value="8">8 MHz</option><option value="16">16 MHz</option></select></label>
      </div>
      <div class="row">
        <button class="pri" id="bConn">Aplicar e reconectar</button>
        <button id="bDemo">Demonstração</button>
        <button id="bTrace">Trace TX/RX</button>
      </div>
      <p class="mut">Modo escuta: o ESP32 só ouve a rede (ex. ECU ↔ painel de instrumentos), sem transmitir nada; os dados vêm dos sinais mapeados na aba Rede. 7DF = pedido funcional (todas as ECUs respondem). 7E0 = direto na ECU do motor. Na detecção automática o ESP32 só escuta o barramento, sem transmitir, até achar a velocidade certa.</p>
    </div>
    <div class="card">
      <h2>Console OBD (CAN)</h2>
      <div class="row">
        <input class="hex" id="raw" placeholder="ex: 01 0C   ·   09 02   ·   22 F1 90" style="flex:1">
        <button class="pri" id="bRaw">Enviar</button>
        <button id="bProbe" title="Testa serviços OBD, KWP2000 e UDS">Sondar ECU</button>
      </div>
      <p class="mut" style="margin-bottom:8px">Envie só o serviço + dados (até 7 bytes). O quadro ISO-TP é montado automaticamente e respostas longas são remontadas.</p>
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
  <button data-v="net">Rede</button>
  <button data-v="diag">Diagnóstico</button>
</nav>

<script>
const $=s=>document.querySelector(s);
// Transmissão contínua (detalhes no fim do script)
const ST={sock:null,ids:new Map(),frames:0,fps:0,fpsN:0,v:{},vt:{},J:null,lastData:0,lastMs:0,msgs:0,msgN:0};
const live=()=>ST.sock&&ST.sock.readyState===1&&Date.now()-ST.lastData<2000;
const hex2=a=>a.map(b=>b.toString(16).padStart(2,'0').toUpperCase()).join('');

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
  const st=$('#st');st.className='pill'+(D.demo?' demo':D.conn&&(D.obd||D.mode==1)?' on':'');
  $('#stt').textContent=D.status;
  $('#live').style.display=live()?'inline-flex':'none';
  $('#hz').textContent=D.hz.toFixed(D.mode==1?0:1)+(D.mode==1&&!D.demo?' quadros/s':' req/s');
  const m=$('#mil');m.style.display=v.mil?'inline-flex':'none';
  if(v.mil)m.textContent='⚠ Injeção · '+v.ndtc+' falha(s)';
  if(view==='diag'){
    $('#info').innerHTML=[['Status',D.status],['Transmissão',live()?`contínua (WebSocket), ${ST.msgs} pacotes/s`:'consultas a cada 250 ms'],['Modo',D.mode==1?'escuta da rede (não transmite)':'diagnóstico OBD'],['MCP2515',D.chip?'OK':'não encontrado'],['Protocolo',D.proto],['ECU',D.ecu],
      ['Barramento',D.bus],['OBD padrão',D.obd?'sim':'não'],['PIDs',D.sup.join(', ')||'—'],
      ['Erros CAN',`${D.txErr} envios falhos · TEC ${D.tec} · REC ${D.rec}`],
      ['Bateria (ADC)',D.vbat!=null?D.vbat.toFixed(1)+' V':'—']]
      .map(([a,b])=>`<dt>${a}</dt><dd>${b}</dd>`).join('');
    $('#bDemo').classList.toggle('act',D.demo);$('#bTrace').classList.toggle('act',D.trace);
    if(!cfgLoaded){$('#cMode').value=D.cfg.mode;$('#cRate').value=D.cfg.rate;$('#cReq').value=D.cfg.req;$('#cXtal').value=D.cfg.xtal;cfgLoaded=true}
  }
}
let cfgLoaded=false;

async function api(path,opt){const r=await fetch(path,opt);return r.json()}
const post=(p,body)=>api(p,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(body||{})});

async function poll(){
  if(live()){D=stData();render();setTimeout(poll,50);return}
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
$('#bConn').onclick=e=>busy(e.target,()=>post('/api/connect',{mode:$('#cMode').value,rate:$('#cRate').value,req:$('#cReq').value,xtal:$('#cXtal').value}));
$('#bDemo').onclick=e=>busy(e.target,()=>post('/api/demo',{on:D&&D.demo?'0':'1'}));
$('#bTrace').onclick=e=>busy(e.target,()=>post('/api/trace',{on:D&&D.trace?'0':'1'}));
const sendRaw=()=>busy($('#bRaw'),async()=>{
  const r=await post('/api/raw',{hex:$('#raw').value});const o=$('#rawOut');
  const line=r.err?`! ${r.err}`:`> ${r.tx}\n`+(r.frames.length?r.frames.map(f=>`< [${f.src}] ${f.data}`).join('\n'):'< (sem resposta)');
  o.textContent=line+'\n\n'+(o.textContent==='—'?'':o.textContent)});
$('#bRaw').onclick=sendRaw;
$('#bProbe').onclick=e=>busy(e.target,async()=>{const o=$('#rawOut');o.textContent='Sondando (até 15 s)...';
  const r=await post('/api/probe');o.textContent=r.err?`! ${r.err}`:r.lines.join('\n')});$('#raw').onkeydown=e=>{if(e.key==='Enter')sendRaw()};

// ---------- Rede CAN (modo escuta) ----------
// Linha de /api/sniff: [id, hz, dlc, dados, mín, máx, mudouAgora, jáMudou]
let S=null,sel=null,hist=[],rec=null,recDone=null,recLast={};
const SIGKEYS=[['rpm','Rotação (rpm)'],['spd','Velocidade (km/h)'],['ect','Temp. motor (°C)'],['iat','Temp. admissão (°C)'],
 ['tps','Borboleta (%)'],['map','Pressão coletor (kPa)'],['load','Carga (%)'],['adv','Avanço (°)'],['o2','Sonda lambda (V)'],
 ['stft','Correção curta (%)'],['ltft','Correção longa (%)'],['vecu','Bateria (V)']];
$('#sKey').innerHTML=SIGKEYS.map(([k,n])=>`<option value="${k}">${n}</option>`).join('');
const hb=(h,i)=>parseInt(h.substr(i*2,2),16);

function selValue(row){
  const len=+$('#sLen').value,le=$('#sOrd').value==='L';
  if(sel.b+len>row[2])return null;
  let r=0;for(let i=0;i<len;i++)r=r*256+hb(row[3],le?sel.b+len-1-i:sel.b+i);
  return r;
}

function renderSel(){
  if(!sel||!S)return;
  const row=S.ids.find(r=>r[0]===sel.id);
  if(!row){$('#selInfo').textContent=`ID ${sel.id} não aparece mais na rede`;return}
  const b=sel.b,v=selValue(row);
  if(v!=null){hist.push(v);if(hist.length>200)hist.shift()}
  const two=b+1<row[2];
  $('#selInfo').innerHTML=`ID <b>${sel.id}</b>, byte <b>${b}</b>: agora <b>${hb(row[3],b)}</b> (0x${row[3].substr(b*2,2)}), mín ${hb(row[4],b)}, máx ${hb(row[5],b)}`+
    (two?`<br>Bytes ${b}-${b+1} juntos: big-endian <b>${hb(row[3],b)*256+hb(row[3],b+1)}</b>, little-endian <b>${hb(row[3],b+1)*256+hb(row[3],b)}</b>`:'')+
    `<br>Dica: rotação costuma ser 2 bytes ÷4 (mult 0.25) ou ÷8; temperatura 1 byte −40 (soma −40). Compare com o modo de teste do painel.`;
  const lo=Math.min(...hist),hi=Math.max(...hist),rg=hi-lo||1;
  $('#spark polyline').setAttribute('points',hist.map((x,i)=>`${(i/199*300).toFixed(1)},${(58-(x-lo)/rg*56).toFixed(1)}`).join(' '));
  const mul=parseFloat($('#sMul').value),add=parseFloat($('#sAdd').value)||0;
  $('#sRes').textContent=v==null?'—':`${(v*(isNaN(mul)?1:mul)+add).toFixed(2)} (cru ${v}; histórico ${lo} a ${hi})`;
}

function renderNet(){
  const on=D&&D.mode==1;
  $('#bSniffOn').style.display=on?'none':'';
  $('#netMsg').textContent=!on?'O modo escuta está desligado. Ative para ver as mensagens da rede (o ESP32 só ouve, não transmite nada).':
    `${S.ids.length} IDs · ${S.fps} quadros/s · ${S.frames} recebidos`+
    (S.ovr?` · ${S.ovr} estouros de buffer (alguns quadros perdidos; não atrapalha a análise)`:'')+
    (S.drop?` · ${S.drop} quadros de IDs além do limite de 96`:'');
  const only=$('#onlyDyn').checked;
  let h='<tr><th>ID</th><th>Hz</th>'+[0,1,2,3,4,5,6,7].map(i=>`<th>${i}</th>`).join('')+'</tr>';
  for(const r of S.ids){
    if(only&&!r[7])continue;
    h+=`<tr><td class="id">${r[0]}</td><td class="hz">${r[1].toFixed(r[1]<10?1:0)}</td>`;
    for(let i=0;i<8;i++){
      if(i>=r[2]){h+='<td></td>';continue}
      const c=(r[6]>>i&1)?'ch':(r[7]>>i&1)?'':'st',s=sel&&sel.id===r[0]&&sel.b===i?' sel':'';
      h+=`<td class="b ${c}${s}" data-id="${r[0]}" data-b="${i}">${r[3].substr(i*2,2)}</td>`;
    }
    h+='</tr>';
  }
  $('#canT').innerHTML=h;
  renderSel();
}
$('#canT').onclick=e=>{const t=e.target.closest('td.b');if(!t)return;sel={id:t.dataset.id,b:+t.dataset.b};hist=[];if(S)renderNet()};
['#sLen','#sOrd'].forEach(s=>$(s).onchange=()=>{hist=[]});

function recStep(){
  const t=((Date.now()-rec.t0)/1000).toFixed(2);
  for(const r of S.ids){if(recLast[r[0]]===r[3])continue;recLast[r[0]]=r[3];rec.lines.push(`${t},${r[0]},${r[3]}`)}
  $('#recInfo').textContent=`Gravando: ${t} s, ${rec.lines.length} linhas`;
}
$('#bRec').onclick=()=>{
  const b=$('#bRec');
  if(rec){recDone=rec;rec=null;b.textContent='● Gravar';b.classList.remove('dng');
    $('#recInfo').textContent=`Gravação parada: ${recDone.lines.length} linhas. Toque em "Baixar gravação".`;return}
  const u=live();
  rec={t0:Date.now(),st:u,ms0:null,lines:[`# Fiesta Dash: gravação da rede CAN, ${new Date().toLocaleString('pt-BR')}`,
    `# ${D?D.bus:''}${u?' · transmissão contínua':''}`,u?'# tempo_s,ID,dados (todos os quadros, tempo medido no ESP32)':'# tempo_s,ID,dados (uma linha sempre que os dados de um ID mudam)']};
  recLast={};b.textContent='■ Parar';b.classList.add('dng');
};
$('#bMark').onclick=()=>{if(!rec)return alert('Comece a gravar primeiro');
  const tm=rec.st?(rec.ms0==null?0:(ST.lastMs-rec.ms0)/1000):(Date.now()-rec.t0)/1000;
  rec.lines.push(`# ${tm.toFixed(rec.st?3:2)} MARCA: ${$('#recLabel').value.trim()||'marca'}`);$('#recLabel').value=''};
$('#bSave').onclick=()=>{const r=rec||recDone;if(!r)return alert('Nada gravado ainda');
  const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([r.lines.join('\n')+'\n'],{type:'text/plain'}));
  a.download='fiesta-can-'+new Date().toISOString().slice(0,19).replace(/[:T]/g,'-')+'.txt';document.body.appendChild(a);a.click();a.remove()};
$('#bSniffOn').onclick=e=>busy(e.target,()=>post('/api/connect',{mode:1}));
$('#bSniffReset').onclick=e=>busy(e.target,async()=>{await post('/api/sniff/reset');ST.ids.clear();hist=[]});

async function saveSigs(){const r=await post('/api/sigs',{s:$('#sigs').value});
  $('#sigMsg').textContent=r.ok?'Salvo. Os valores já aparecem no Painel.':'Erro: '+r.err}
$('#bSigs').onclick=e=>busy(e.target,saveSigs);
$('#bAddSig').onclick=e=>{if(!sel)return alert('Toque num byte da tabela primeiro');
  const k=$('#sKey').value,line=[k,sel.id,sel.b,$('#sLen').value,$('#sOrd').value,$('#sMul').value||1,$('#sAdd').value||0].join(',');
  const lines=$('#sigs').value.split('\n').filter(l=>l.trim()&&!l.trim().startsWith(k+','));
  lines.push(line);$('#sigs').value=lines.join('\n');busy(e.target,saveSigs)};
api('/api/sigs').then(r=>{$('#sigs').value=r.text.trim()}).catch(()=>{});

async function pollNet(){
  if(view==='net'||rec){try{S=live()&&D&&D.mode==1?stSniff():await api('/api/sniff');
    if(view==='net')renderNet();if(rec&&!rec.st)recStep();else if(rec)recInfoSt()}catch(e){}}
  setTimeout(pollNet,300);
}
pollNet();

// ---------- Transmissão contínua (WebSocket na porta 81) ----------
// O ESP32 envia linhas "F <ms> <ID> <dlc> <dados>", "V <ms> <chave> <valor>" e "J <json>".
// Enquanto ela estiver ativa, painel e aba Rede usam esses dados; se cair, a página volta às
// consultas http e tenta reconectar.

function stConnect(){
  let s;
  try{s=new WebSocket(`ws://${location.hostname||'192.168.4.1'}:81/`)}catch(e){setTimeout(stConnect,3000);return}
  ST.sock=s;
  s.onmessage=ev=>{ST.msgN++;const t=ev.data;let i=0,j;
    while((j=t.indexOf('\n',i))>=0){stLine(t.slice(i,j));i=j+1}
    if(i<t.length)stLine(t.slice(i))};
  s.onclose=()=>{if(ST.sock===s){ST.sock=null;setTimeout(stConnect,3000)}};
  s.onerror=()=>{try{s.close()}catch(e){}};
}
function stLine(l){
  const k=l[0];if(l[1]!==' ')return;
  ST.lastData=Date.now();
  if(k==='F'){const p=l.split(' ');stFrame(+p[1],p[2],+p[3],p[4]||'')}
  else if(k==='V'){const p=l.split(' ');ST.v[p[2]]=parseFloat(p[3]);ST.vt[p[2]]=ST.lastData}
  else if(k==='J'){try{ST.J=JSON.parse(l.slice(2))}catch(e){}}
}
function stFrame(ms,id,dlc,hex){
  let e=ST.ids.get(id);const now=Date.now();ST.lastMs=ms;
  if(!e){e={id,dlc,d:hex,mn:[],mx:[],chg:[],ever:0,n:0,np:0,hz:0};
    for(let i=0;i<dlc;i++)e.mn[i]=e.mx[i]=hb(hex,i);ST.ids.set(id,e)}
  else for(let i=0;i<dlc;i++){const b=hb(hex,i);if(b!==hb(e.d,i)){e.ever|=1<<i;e.chg[i]=now}
    if(e.mn[i]==null||b<e.mn[i])e.mn[i]=b;if(e.mx[i]==null||b>e.mx[i])e.mx[i]=b}
  e.d=hex;e.dlc=dlc;e.n++;ST.frames++;
  if(rec&&rec.st){if(rec.ms0==null)rec.ms0=ms;rec.lines.push(`${((ms-rec.ms0)/1000).toFixed(3)},${id},${hex}`)}
}
setInterval(()=>{
  for(const e of ST.ids.values()){e.hz=e.n-e.np;e.np=e.n}
  ST.fps=ST.frames-ST.fpsN;ST.fpsN=ST.frames;ST.msgs=ST.msgN;ST.msgN=0;
},1000);
function stSniff(){
  const now=Date.now(),ids=[...ST.ids.values()].sort((a,b)=>a.id.length-b.id.length||(a.id<b.id?-1:1));
  return {fps:ST.fps,frames:ST.frames,ovr:0,drop:0,ids:ids.map(e=>{let r=0;
    for(let i=0;i<e.dlc;i++)if((e.ever>>i&1)&&now-(e.chg[i]||0)<1500)r|=1<<i;
    return [e.id,e.hz,e.dlc,e.d,hex2(e.mn.slice(0,e.dlc)),hex2(e.mx.slice(0,e.dlc)),r,e.ever]})};
}
// Estado: último J (2x/s) + valores V mais recentes (chegam a cada quadro)
function stData(){
  const base=ST.J||D||{v:{},sup:[],cfg:{}};
  const d={...base,v:{...(base.v||{})}},now=Date.now();
  for(const k in ST.v)if(now-ST.vt[k]<5000)d.v[k]=ST.v[k];
  if(d.mode==1&&!d.demo)d.hz=ST.fps;
  return d;
}
function recInfoSt(){$('#recInfo').textContent=`Gravando: ${rec.ms0==null?0:((ST.lastMs-rec.ms0)/1000).toFixed(1)} s, ${rec.lines.length} linhas (todos os quadros)`}
stConnect();
</script>
</body>
</html>
)HTML";
