#pragma once
// Página de controle, comum às duas placas. Consulta o estado por HTTP: sem WebSocket, um celular em
// espera não segura o laço principal. Autônoma: precisa abrir também na rede própria, sem internet.
#include <pgmspace.h>

static const char CONTROL_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0b0e14">
<title>ACRemote</title>
<style>
:root{--bg:#0b0e14;--glass:rgba(255,255,255,.055);--edge:rgba(255,255,255,.09);--txt:#eef1f6;--mut:#98a2b3;--a1:#64748b;--a2:#475569;--fan:2.4s}
body[data-m=cool]{--a1:#38bdf8;--a2:#6366f1}
body[data-m=heat]{--a1:#fb923c;--a2:#f43f5e}
body[data-m=fan]{--a1:#2dd4bf;--a2:#22c55e}
body[data-m=auto]{--a1:#a78bfa;--a2:#38bdf8}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
[hidden]{display:none!important}
html{background:var(--bg)}
body{margin:0;min-height:100vh;background:var(--bg);color:var(--txt);font:15px/1.3 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;font-variant-numeric:tabular-nums;overflow-x:hidden;position:relative;isolation:isolate}
body::before{content:"";position:fixed;inset:-20%;z-index:-1;pointer-events:none;opacity:.32;transition:opacity .8s;background:radial-gradient(38% 34% at 28% 16%,var(--a1),transparent 70%),radial-gradient(40% 36% at 74% 30%,var(--a2),transparent 72%);animation:drift 16s ease-in-out infinite alternate}
body.off::before{opacity:.1;animation:none}
button{font:inherit;color:inherit;background:none;border:0;padding:0;cursor:pointer}
button:disabled{opacity:.38;cursor:default}
button:focus-visible,a:focus-visible{outline:2px solid var(--a1);outline-offset:2px}
.i{width:22px;height:22px;display:block;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round;flex:none}
.app{max-width:440px;margin:0 auto;padding:14px 16px calc(28px + env(safe-area-inset-bottom))}
.top{display:flex;justify-content:space-between;align-items:center;margin-bottom:6px}
.brand{font-weight:800;font-size:19px;letter-spacing:-.02em}
.brand span{background:linear-gradient(90deg,var(--a1),var(--a2));-webkit-background-clip:text;background-clip:text;color:transparent}
.tools{display:flex;align-items:center;gap:10px;color:var(--mut)}
.tools a{color:inherit;display:grid;place-items:center;width:40px;height:40px;border-radius:50%;background:var(--glass)}
.dot{width:8px;height:8px;border-radius:50%;background:#f87171}
.dot.ok{background:#4ade80;box-shadow:0 0 0 0 rgba(74,222,128,.6);animation:ping 2.6s infinite}
.dial{position:relative;width:min(78vw,290px);aspect-ratio:1;margin:6px auto 0}
.dial svg{width:100%;height:100%;display:block;overflow:visible}
.arc{fill:none;stroke-width:13;stroke-linecap:round}
.track{stroke:rgba(255,255,255,.08)}
.val{stroke:url(#g);transition:stroke-dasharray .5s cubic-bezier(.3,.8,.3,1)}
.thumb{fill:#fff;transition:transform .5s cubic-bezier(.3,.8,.3,1);transform-origin:120px 120px}
.halo{fill:none;stroke:var(--a1);stroke-width:1;opacity:0;transform-origin:120px 120px}
body:not(.off) .halo{animation:halo 3.6s ease-out infinite}
body:not(.off) .halo.b{animation-delay:1.8s}
.mid{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center;pointer-events:none}
.mid small{color:var(--mut);font-size:13px}
.mid b{font-size:clamp(58px,18vw,76px);font-weight:300;letter-spacing:-.04em;line-height:1}
.room{display:flex;gap:6px;align-items:center;color:var(--mut);margin-top:6px;font-size:14px}
.room .i{width:16px;height:16px}
.room strong{color:var(--txt);font-weight:600}
.state{display:flex;justify-content:center;align-items:center;gap:7px;color:var(--mut);margin:-4px 0 12px;min-height:22px;font-size:14px}
.ctrl{display:flex;align-items:center;justify-content:center;gap:18px}
.step{width:54px;height:54px;border-radius:50%;background:var(--glass);border:1px solid var(--edge);display:grid;place-items:center;transition:transform .15s}
.step:active{transform:scale(.9)}
.pw{display:flex;gap:8px}
.pw button{display:flex;align-items:center;gap:8px;height:54px;padding:0 22px;border-radius:27px;font-weight:700;background:linear-gradient(120deg,var(--a1),var(--a2));color:#07111c;transition:transform .15s}
.pw button.ghost{background:var(--glass);color:var(--txt);box-shadow:inset 0 0 0 1px var(--edge)}
.pw button:active{transform:scale(.95)}
body.off .pw button:not(.ghost){background:#eef1f6}
.notice{background:rgba(248,113,113,.14);color:#fecaca;border-radius:14px;padding:11px 14px;margin:14px 0 0;font-size:14px}
.box{background:var(--glass);border:1px solid var(--edge);border-radius:20px;padding:14px;margin-top:12px;-webkit-backdrop-filter:blur(14px);backdrop-filter:blur(14px)}
.head{display:flex;justify-content:space-between;align-items:center;gap:8px;margin-bottom:10px;color:var(--mut);font-size:13px;font-weight:600}
.head .side{display:flex;align-items:center;gap:6px}
.head .i{width:18px;height:18px}
.segs{display:grid;grid-template-columns:repeat(4,1fr);gap:6px}
.segs.six{grid-template-columns:repeat(6,1fr)}
.seg{display:flex;flex-direction:column;align-items:center;justify-content:center;gap:6px;min-height:46px;padding:9px 2px;border-radius:14px;font-size:12px;color:var(--mut);transition:background .25s,color .25s,transform .15s}
.seg:active{transform:scale(.94)}
.seg[aria-pressed=true]{background:linear-gradient(135deg,var(--a1),var(--a2));color:#07111c;font-weight:700}
.bars{display:flex;align-items:flex-end;gap:2px;height:16px}
.bars i{width:4px;border-radius:2px;background:currentColor;opacity:.25}
.bars i:nth-child(1){height:6px}.bars i:nth-child(2){height:9px}.bars i:nth-child(3){height:12px}.bars i:nth-child(4){height:16px}
.bars i.on{opacity:1}
.tabs{display:inline-flex;background:rgba(255,255,255,.07);border-radius:10px;padding:2px}
.tabs button{padding:4px 11px;border-radius:8px;font-size:12px;color:var(--mut)}
.tabs button.on{background:rgba(255,255,255,.16);color:var(--txt)}
.tiles{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:12px}
.tile{display:flex;flex-direction:column;gap:12px;text-align:left;background:var(--glass);border:1px solid var(--edge);border-radius:20px;padding:14px;transition:border-color .25s,transform .15s}
.tile:active{transform:scale(.97)}
.tile .row{display:flex;justify-content:space-between;align-items:center;width:100%}
.tile em{font-style:normal;display:block;color:var(--mut);font-size:13px;margin-top:2px}
.tile.act{border-color:var(--a1)}
.tile.act .i{color:var(--a1)}
.tile:not(.act) .slash{display:none}
.sw{width:34px;height:20px;border-radius:10px;background:rgba(255,255,255,.14);position:relative;transition:background .25s;flex:none}
.sw::after{content:"";position:absolute;top:2px;left:2px;width:16px;height:16px;border-radius:50%;background:#fff;transition:transform .25s}
.act .sw{background:var(--a1)}
.act .sw::after{transform:translateX(14px)}
.sheet{position:fixed;inset:0;background:rgba(3,5,9,.6);display:flex;align-items:flex-end;justify-content:center;z-index:5;animation:fade .2s}
.panel{background:#151a23;border:1px solid var(--edge);width:100%;max-width:440px;max-height:92vh;overflow:auto;border-radius:24px 24px 0 0;padding:18px 16px calc(20px + env(safe-area-inset-bottom));animation:rise .25s cubic-bezier(.3,.8,.3,1)}
.panel .head{color:var(--txt);font-size:17px}
.hours{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
.hours .seg{background:var(--glass);font-size:15px;color:var(--txt)}
#toast{position:fixed;left:50%;bottom:22px;transform:translateX(-50%);background:#eef1f6;color:#0b0e14;padding:9px 16px;border-radius:20px;font-size:14px;z-index:9;max-width:90vw}
.list{display:flex;flex-direction:column;gap:8px}
.item{display:flex;align-items:center;gap:10px;padding:10px;border-radius:14px;background:rgba(255,255,255,.04)}
.item.mutedr{opacity:.55}
.item .txt{flex:1;min-width:0;text-align:left}
.item b{display:block;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.item em{display:block;font-style:normal;color:var(--mut);font-size:13px}
.mini{height:36px;padding:0 14px;border-radius:18px;background:var(--glass);box-shadow:inset 0 0 0 1px var(--edge);font-size:13px;font-weight:700;flex:none}
.empty{color:var(--mut);font-size:14px;margin:2px}
.clk{color:var(--mut);font-size:13px;margin:0 0 10px}
.clk.bad{color:#fecaca}
.f{display:block;margin-bottom:14px;font-size:13px;color:var(--mut)}
.f>span{display:block;margin-bottom:6px}
.f input,.f select{width:100%;height:46px;padding:0 12px;border-radius:12px;border:1px solid var(--edge);background:#0b0e14;color:var(--txt);color-scheme:dark;font:inherit;font-size:16px}
.f .ctrl b{font-size:30px;font-weight:300;color:var(--txt);min-width:70px;text-align:center}
.segs.three{grid-template-columns:repeat(3,1fr)}
.segs.seven{grid-template-columns:repeat(7,1fr)}
.f .seg:not([aria-pressed=true]){background:rgba(255,255,255,.04)}
.f .tabs{margin-bottom:8px}
.acts{display:flex;gap:10px}
.acts .save{flex:1;height:48px;border-radius:24px;font-weight:700;background:linear-gradient(120deg,var(--a1),var(--a2));color:#07111c}
body.off .acts .save{background:#eef1f6}
.acts .mini{height:48px;border-radius:24px;color:#fecaca}
@media(min-width:800px){
 .app{max-width:860px;display:grid;grid-template-columns:1fr 1fr;gap:0 28px;align-items:start;padding-top:22px}
 .top{grid-column:1/-1}
 .left{position:sticky;top:22px}
 .dial{width:310px}
}
.spin{animation:spin var(--fan) linear infinite}
.slow{animation:spin 9s linear infinite}
.flick{animation:flick 1.3s ease-in-out infinite;transform-origin:50% 90%}
.pulse{animation:pulse 2.4s ease-in-out infinite}
.rock{animation:rock 3.4s ease-in-out infinite;transform-origin:50% 50%}
.flow path{stroke-dasharray:5 4;animation:flow 1.1s linear infinite}
.tick .hand{animation:spin 6s linear infinite;transform-origin:12px 12px}
@keyframes spin{to{transform:rotate(360deg)}}
@keyframes flick{0%,100%{transform:scale(1)}30%{transform:scale(1.08,.94)}60%{transform:scale(.94,1.1)}}
@keyframes pulse{50%{transform:scale(1.14);opacity:.75}}
@keyframes rock{0%,100%{transform:rotate(-9deg)}50%{transform:rotate(9deg)}}
@keyframes flow{to{stroke-dashoffset:-18}}
@keyframes halo{0%{transform:scale(.62);opacity:.42}100%{transform:scale(1);opacity:0}}
@keyframes ping{70%,100%{box-shadow:0 0 0 7px rgba(74,222,128,0)}}
@keyframes drift{to{transform:translate(3%,4%) scale(1.08)}}
@keyframes fade{from{opacity:0}}
@keyframes rise{from{transform:translateY(40px)}}
@media(prefers-reduced-motion:reduce){*,*::before{animation:none!important;transition:none!important}}
</style>
</head>
<body class="off">
<div class="app">
  <header class="top">
    <div class="brand">AC<span>Remote</span></div>
    <div class="tools"><span class="dot" id="net" title="Conexão com a placa"></span><a href="/ajustes" aria-label="Ajustes" data-i="gear"></a></div>
  </header>

  <div class="left">
    <section class="dial" aria-live="polite">
      <svg viewBox="0 0 240 240" aria-hidden="true">
        <defs><linearGradient id="g" x1="0" y1="1" x2="1" y2="0"><stop offset="0" style="stop-color:var(--a2)"/><stop offset="1" style="stop-color:var(--a1)"/></linearGradient></defs>
        <circle class="halo" cx="120" cy="120" r="118"/><circle class="halo b" cx="120" cy="120" r="118"/>
        <g transform="rotate(135 120 120)">
          <circle class="arc track" cx="120" cy="120" r="100" stroke-dasharray="471.2 628.4"/>
          <circle class="arc val" id="arc" cx="120" cy="120" r="100" stroke-dasharray="0 628.4"/>
          <circle class="thumb" id="thumb" cx="220" cy="120" r="5"/>
        </g>
      </svg>
      <div class="mid">
        <small id="lbl">Temperatura alvo</small>
        <b id="set">--</b>
        <div class="room" id="roomBox"><span data-i="drop"></span>Interna <strong id="room">--</strong></div>
      </div>
    </section>
    <p class="state"><span id="sIco"></span><span id="state">Aguardando o ar</span></p>
    <div class="ctrl">
      <button class="step" id="minus" aria-label="Diminuir temperatura" data-i="minus"></button>
      <div class="pw" id="pw"></div>
      <button class="step" id="plus" aria-label="Aumentar temperatura" data-i="plus"></button>
    </div>
    <div class="notice" id="notice" hidden></div>
  </div>

  <div>
    <section class="box"><div class="head">Modo</div><div class="segs" id="modes"></div></section>
    <section class="box">
      <div class="head">Ventilador<span class="side" id="fanIco" data-i="fan"></span></div>
      <div class="segs" id="fans"></div>
    </section>
    <section class="box">
      <div class="head"><span>Direção do ar</span><div class="tabs"><button id="tFix">Fixo</button><button id="tOsc">Oscilar</button></div></div>
      <div class="segs" id="dirs"></div>
    </section>
    <div class="tiles">
      <button class="tile" id="bSleep"><span class="row"><span data-i="moon"></span><span class="sw"></span></span><span>Dormir<em id="vSleep">--</em></span></button>
      <button class="tile" id="bDisp"><span class="row"><span data-i="disp"></span><span class="sw"></span></span><span>Display<em id="vDisp">--</em></span></button>
      <button class="tile" id="bMute"><span class="row"><span data-i="bell"></span><span class="sw"></span></span><span>Silencioso<em id="vMute">--</em></span></button>
      <button class="tile" id="bInd"><span class="row"><span data-i="wind"></span></span><span>Ar indireto<em id="vInd">--</em></span></button>
      <button class="tile" id="bSense"><span class="row"><span data-i="sense"></span></span><span>Auto sense<em id="vSense">--</em></span></button>
      <button class="tile" id="bTimer"><span class="row"><span data-i="clock"></span></span><span>Timer<em id="vTimer">--</em></span></button>
    </div>
    <section class="box">
      <div class="head"><span>Modos</span><button class="mini" id="pNew">Novo</button></div>
      <div class="list" id="presets"></div>
    </section>
    <section class="box">
      <div class="head"><span>Programação</span><button class="mini" id="rNew">Novo</button></div>
      <p class="clk" id="clk"></p>
      <div class="list" id="rules"></div>
    </section>
  </div>
</div>

<div class="sheet" id="sheet" hidden>
  <div class="panel" role="dialog" aria-modal="true" aria-label="Desligar em">
    <div class="head">Desligar em<button id="sClose">Fechar</button></div>
    <div class="hours" id="hours"></div>
  </div>
</div>
<div class="sheet" id="pSheet" hidden>
  <div class="panel" role="dialog" aria-modal="true" aria-labelledby="pTitle">
    <div class="head"><span id="pTitle"></span><button data-close>Fechar</button></div>
    <label class="f"><span>Nome</span><input id="pName" maxlength="16" autocomplete="off"></label>
    <div class="f"><span>Modo</span><div class="segs" id="pMode"></div></div>
    <div class="f" id="pTempBox"><span>Temperatura</span><div class="ctrl">
      <button class="step" id="pMinus" aria-label="Diminuir temperatura" data-i="minus"></button><b id="pTemp"></b>
      <button class="step" id="pPlus" aria-label="Aumentar temperatura" data-i="plus"></button></div></div>
    <div class="f"><span>Ventilador</span><div class="segs" id="pFan"></div></div>
    <div class="f"><span>Direção do ar</span><div class="tabs"><button id="pFix">Fixo</button><button id="pOsc">Oscilar</button></div><div class="segs" id="pDir"></div></div>
    <div class="f"><span>Opções</span><div class="segs three" id="pOpts"></div></div>
    <div class="acts"><button class="mini" id="pDel">Apagar</button><button class="save" id="pSave">Salvar</button></div>
  </div>
</div>
<div class="sheet" id="rSheet" hidden>
  <div class="panel" role="dialog" aria-modal="true" aria-labelledby="rTitle">
    <div class="head"><span id="rTitle"></span><button data-close>Fechar</button></div>
    <label class="f"><span>Horário</span><input id="rTime" type="time"></label>
    <div class="f"><span>Dias</span><div class="segs seven" id="rDays"></div></div>
    <label class="f"><span>Ação</span><select id="rAct"></select></label>
    <div class="acts"><button class="mini" id="rDel">Apagar</button><button class="save" id="rSave">Salvar</button></div>
  </div>
</div>
<div id="toast" hidden></div>

<script>
const $=id=>document.getElementById(id);
const P={
 gear:'<circle cx="12" cy="12" r="3"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M18.4 5.6l-2.1 2.1M7.7 16.3l-2.1 2.1"/>',
 minus:'<path d="M6 12h12"/>',plus:'<path d="M6 12h12M12 6v12"/>',
 power:'<path d="M12 3v8M6.6 6.8a8 8 0 1 0 10.8 0"/>',
 drop:'<path d="M12 3s6 6.5 6 11a6 6 0 0 1-12 0c0-4.5 6-11 6-11z"/>',
 snow:'<path d="M12 3v18M4.2 7.5l15.6 9M19.8 7.5l-15.6 9M12 7l-2-2M12 7l2-2M12 17l-2 2M12 17l2 2"/>',
 flame:'<path d="M12 3c1 4 5 5.5 5 11a5 5 0 0 1-10 0c0-2 1-3.5 2-4.5 0 2 1 3 2 3 .5-3-.5-6 1-9.5z"/>',
 fan:'<circle cx="12" cy="12" r="1.6"/><path d="M12 10.4C10 7 10.5 3 13 3c3 0 3.5 4.5-1 7.4zM13.6 12c3.4-2 7.4-1.5 7.4 1 0 3-4.5 3.5-7.4-1zM12 13.6c2 3.4 1.5 7.4-1 7.4-3 0-3.5-4.500 1-7.400zM10.4 12C7 14 3 13.500 3 11c0-3 4.500-3.500 7.400 1z"/>',
 auto:'<circle cx="12" cy="12" r="8.5"/><path d="M8.5 16l3.5-8 3.5 8M9.8 13.5h4.4"/>',
 moon:'<path d="M20 14.5A8.5 8.5 0 0 1 9.5 4a8.5 8.5 0 1 0 10.5 10.5z"/>',
 disp:'<rect x="3" y="6" width="18" height="12" rx="3"/><path d="M8 12h.01M12 12h.01M16 12h.01"/>',
 wind:'<path d="M3 9h11a3 3 0 1 0-3-3M3 14h15a3 3 0 1 1-3 3M3 19h7"/>',
 sense:'<circle cx="12" cy="12" r="3"/><path d="M5 9V6a1 1 0 0 1 1-1h3M15 5h3a1 1 0 0 1 1 1v3M19 15v3a1 1 0 0 1-1 1h-3M9 19H6a1 1 0 0 1-1-1v-3"/>',
 bell:'<path d="M6 16V11a6 6 0 0 1 12 0v5l1.5 2h-15zM10 21h4"/><path class="slash" d="M4 4l16 16"/>',
 clock:'<circle cx="12" cy="12" r="9"/><path class="hand" d="M12 12V6.5"/>'
};
const svg=(n,c='')=>`<svg class="i ${c}" viewBox="0 0 24 24" aria-hidden="true">${P[n]}</svg>`;
document.querySelectorAll('[data-i]').forEach(e=>{e.innerHTML=svg(e.dataset.i)});

const MODES=[[6,'Auto','auto','pulse','auto'],[1,'Resfriar','snow','slow','cool'],[2,'Aquecer','flame','flick','heat'],[3,'Ventilar','fan','spin','fan']];
const FANS=[[1,'Baixa',1,'2.4s'],[2,'Média',2,'1.4s'],[4,'Alta',3,'.8s'],[8,'Turbo',4,'.4s']];
const SWINGS=['Amplo','Em cima','No meio','Em baixo'];
const TEMP_MIN=16,TEMP_MAX=30,TEMP_START=23,STALE_S=30,ARC=471.2,SCHEDULE_POLL_MS=30000;
const TEMP_SEND_MS=900,TEMP_CONFIRM_MS=3000;  // espera o último toque; depois, a confirmação do ar
let a=null,lastFail=null,dirTab=null,toastTimer=0,tempWanted=null,tempTimer=0,tempSentAt=0;

function toast(msg){
  const t=$('toast');
  t.textContent=msg;t.hidden=false;
  clearTimeout(toastTimer);
  toastTimer=setTimeout(()=>{t.hidden=true},3000);
}

async function send(cmd,v){
  try{
    const r=await fetch('/api/ac',{method:'POST',body:new URLSearchParams({cmd,v})});
    if(!r.ok)toast('Comando recusado.');
  }catch(e){toast('Sem conexão com a placa.')}
  setTimeout(refresh,300);
  setTimeout(refresh,800);
}

// Só troca o que mudou: recriar um ícone reiniciaria a animação dele a cada consulta.
function setHtml(el,html){
  if(el.dataset.h===html)return;
  el.dataset.h=html;
  el.innerHTML=html;
}

function segs(box,items){
  setHtml(box,items.map(s=>`<button class="seg" data-c="${s.c}" data-v="${s.v}" aria-pressed="${!!s.on}">${s.html}</button>`).join(''));
  box.querySelectorAll('button').forEach(b=>{b.disabled=!a||!a.on;b.onclick=()=>send(b.dataset.c,b.dataset.v)});
}

function animate(el,cls,on){const s=el.querySelector('svg');if(s)s.classList.toggle(cls,on)}

function renderDir(){
  const osc=dirTab===1;
  $('tFix').className=osc?'':'on';$('tOsc').className=osc?'on':'';
  $('dirs').className=osc?'segs':'segs six';
  const swing=a?a.swing:-1,vane=a?a.vane:-1;
  segs($('dirs'),osc?SWINGS.map((t,i)=>({c:'swing',v:i,html:t,on:swing===1&&vane===i}))
    :[1,2,3,4,5,6].map(i=>({c:'fixed',v:i,html:String(i),on:swing===0&&vane===i})));
}

function renderPower(){
  const known=a&&a.power>=0,on=a&&a.power===1;
  const list=known?[[on?'Desligar':'Ligar',on?0:1,'']]:[['Ligar',1,''],['Desligar',0,'ghost']];
  setHtml($('pw'),list.map(w=>`<button data-v="${w[1]}" class="${w[2]}">${known?svg('power'):''}${w[0]}</button>`).join(''));
  $('pw').querySelectorAll('button').forEach(b=>{b.disabled=!a||!a.on;b.onclick=()=>send('power',b.dataset.v)});
}

function toggle(id,val,text){
  $(id).classList.toggle('act',val===1);
  $(id).setAttribute('aria-pressed',val===1);
  return text;
}

function render(){
  const on=a.power===1,mode=MODES.find(m=>m[0]===a.mode),fan=FANS.find(f=>f[0]===a.fan),isFan=a.mode===3;
  document.body.classList.toggle('off',!on);
  document.body.dataset.m=on&&mode?mode[4]:'';
  if(fan)document.documentElement.style.setProperty('--fan',fan[3]);

  if(tempWanted!==null&&tempSentAt&&(a.set===tempWanted||Date.now()-tempSentAt>TEMP_CONFIRM_MS))tempWanted=null;
  const shown=isFan?a.ca30:tempWanted??a.set;
  $('lbl').textContent=isFan?'Temperatura interna':'Temperatura alvo';
  $('set').textContent=shown<0?'--':shown+'°';
  $('roomBox').hidden=isFan;
  $('room').textContent=a.ca30<0?'--':a.ca30+'°';
  const f=shown<0?0:Math.min(1,Math.max(0,(shown-TEMP_MIN)/(TEMP_MAX-TEMP_MIN)));
  $('arc').setAttribute('stroke-dasharray',`${ARC*f} 628.4`);
  $('thumb').style.transform=`rotate(${270*f}deg)`;
  $('minus').style.visibility=$('plus').style.visibility=isFan?'hidden':'visible';

  $('state').textContent=a.power<0?'Estado aparece quando o ar avisar':on?(mode?'Ligado em '+mode[1].toLowerCase():'Ligado'):'Desligado';
  setHtml($('sIco'),on&&mode?svg(mode[2],mode[3]):'');

  segs($('modes'),MODES.map(m=>({c:'mode',v:m[0],on:a.mode===m[0],html:svg(m[2],on&&a.mode===m[0]?m[3]:'')+m[1]})));
  segs($('fans'),FANS.map(s=>({c:'fan',v:s[0],on:a.fan===s[0],html:`<span class="bars">${[1,2,3,4].map(n=>`<i class="${n<=s[2]?'on':''}"></i>`).join('')}</span>${s[1]}`})));
  animate($('fanIco'),'spin',on);

  if(dirTab===null&&a.swing>=0)dirTab=a.swing===1?1:0;
  renderDir();

  $('vSleep').textContent=toggle('bSleep',a.sleep,a.sleep<0?'--':a.sleep?'Ligado':'Desligado');
  $('vDisp').textContent=toggle('bDisp',a.disp,a.disp<0?'--':a.disp?'Ligado':'Desligado');
  $('vMute').textContent=toggle('bMute',a.mute,a.mute<0?'--':a.mute?'Ligado':'Desligado');
  const shared=val=>isFan?'Indisponível':val===1?'Ligado':'Ativar';
  $('vInd').textContent=toggle('bInd',isFan?0:a.ind,shared(a.ind));
  $('vSense').textContent=toggle('bSense',isFan?0:a.sense,shared(a.sense));
  const timerOn=a.timer>0;
  $('vTimer').textContent=a.timer<0?'--':timerOn?'Desliga em '+(a.timer/3600).toFixed(1).replace('.0','').replace('.',',')+' h':'Sem timer';
  $('bTimer').classList.toggle('act',timerOn);
  animate($('bSleep'),'rock',a.sleep===1);
  animate($('bMute'),'rock',a.mute===1);
  animate($('bInd'),'flow',a.ind===1&&on&&!isFan);
  animate($('bSense'),'pulse',a.sense===1&&on&&!isFan);
  animate($('bTimer'),'tick',timerOn);

  const n=$('notice');
  n.hidden=a.on&&a.rx>=0&&a.rx<STALE_S;
  n.textContent=!a.on?'Controle desativado. Ative-o em Ajustes, só com o módulo original desconectado.':'Sem resposta do ar. Confira os fios e a alimentação.';
  document.querySelectorAll('.app .step,.tile,.app .tabs button').forEach(b=>{b.disabled=!a.on});
  if(a.ind===1||isFan)$('bInd').disabled=true;
  if(a.sense===1||isFan)$('bSense').disabled=true;
  renderPower();
  if(lastFail!==null&&a.fail>lastFail)toast('O ar não confirmou o comando.');
  lastFail=a.fail;
}

function sendTemp(){
  if(tempWanted===a.set||a.mode===3){
    tempWanted=null;
    render();
    return;
  }
  tempSentAt=Date.now();
  send('temp',tempWanted);
}

function stepTemp(d){
  const v=(tempWanted??(a.set<0?TEMP_START:a.set))+d;
  if(v<TEMP_MIN||v>TEMP_MAX)return;
  tempWanted=v;
  tempSentAt=0;
  clearTimeout(tempTimer);
  tempTimer=setTimeout(sendTemp,TEMP_SEND_MS);
  render();
}

async function refresh(){
  try{
    const r=await fetch('/api/ac/state');
    a=await r.json();
    $('net').classList.add('ok');
    render();
  }catch(e){$('net').classList.remove('ok')}
}

const closeSheet=()=>document.querySelectorAll('.sheet').forEach(s=>{s.hidden=true});
$('hours').innerHTML=Array.from({length:12},(_,i)=>`<button class="seg" data-v="${i+1}">${i+1} h</button>`).join('');
$('hours').querySelectorAll('button').forEach(b=>b.onclick=()=>{send('timer',b.dataset.v);closeSheet()});
$('bTimer').onclick=()=>{$('sheet').hidden=false};
$('sClose').onclick=closeSheet;
document.querySelectorAll('[data-close]').forEach(b=>{b.onclick=closeSheet});
document.querySelectorAll('.sheet').forEach(s=>{s.onclick=e=>{if(e.target===s)closeSheet()}});
document.addEventListener('keydown',e=>{if(e.key==='Escape')closeSheet()});
$('minus').onclick=()=>stepTemp(-1);
$('plus').onclick=()=>stepTemp(1);
$('tFix').onclick=()=>{dirTab=0;renderDir()};
$('tOsc').onclick=()=>{dirTab=1;renderDir()};
$('bSleep').onclick=()=>send('sleep',a.sleep===1?0:1);
$('bDisp').onclick=()=>send('display',a.disp===1?0:1);
$('bMute').onclick=()=>send('mute',a.mute===1?0:1);
$('bInd').onclick=()=>send('indirect',1);
$('bSense').onclick=()=>send('sense',1);

// ---------- Modos e programação ----------
const DAY_KEYS=['D','S','T','Q','Q','S','S'],DAY_NAMES=['dom','seg','ter','qua','qui','sex','sáb'];
const ALL_DAYS=127,WEEK_DAYS=62,WEEKEND=65,RULE_OFF=-1,TEMP_DEFAULT=22;
let sch=null,pf=null,rf=null;
const esc=s=>String(s).replace(/[&<>"]/g,c=>`&#${c.charCodeAt(0)};`);
const pad=n=>String(n).padStart(2,'0');

function pick(box,items,cb){
  box.innerHTML=items.map((s,i)=>`<button type="button" class="seg" data-n="${i}" aria-pressed="${!!s.on}">${s.html}</button>`).join('');
  box.querySelectorAll('button').forEach(b=>{b.onclick=()=>cb(items[b.dataset.n].v)});
}

function presetText(p){
  const m=MODES.find(x=>x[0]===p.mode),f=FANS.find(x=>x[0]===p.fan);
  const parts=[m[1]+(p.mode===3?'':` ${p.temp}°`),f[1],p.swing?'Oscilar '+SWINGS[p.vane].toLowerCase():'Fixo '+p.vane];
  if(p.sleep)parts.push('Dormir');
  if(p.mute)parts.push('Silencioso');
  return parts.join(', ');
}

function daysText(d){
  if(d===ALL_DAYS)return 'Todos os dias';
  if(d===WEEK_DAYS)return 'Seg a sex';
  if(d===WEEKEND)return 'Fim de semana';
  return DAY_NAMES.filter((_,i)=>d&(1<<i)).join(', ');
}

function actionText(act){
  if(act===RULE_OFF)return 'Desligar';
  const p=sch.presets[act];
  return p&&p.n?p.n:'Modo apagado';
}

function renderSchedule(){
  const presets=sch.presets.map((p,i)=>({p,i})).filter(x=>x.p.n);
  setHtml($('presets'),presets.length?presets.map(({p,i})=>`<div class="item"><button class="txt" data-e="${i}"><b>${esc(p.n)}</b><em>${esc(presetText(p))}</em></button><button class="mini" data-a="${i}">Aplicar</button></div>`).join(''):'<p class="empty">Nenhum modo salvo.</p>');
  $('presets').querySelectorAll('[data-e]').forEach(b=>{b.onclick=()=>openPreset(+b.dataset.e)});
  $('presets').querySelectorAll('[data-a]').forEach(b=>{b.onclick=()=>{toast('Aplicando '+sch.presets[b.dataset.a].n);send('preset',b.dataset.a)}});
  $('pNew').disabled=presets.length===sch.presets.length;

  const rules=sch.rules.map((r,i)=>({r,i})).filter(x=>x.r.days).sort((x,y)=>x.r.h*60+x.r.m-y.r.h*60-y.r.m);
  setHtml($('rules'),rules.length?rules.map(({r,i})=>`<div class="item ${r.on?'':'mutedr'}"><button class="txt" data-e="${i}"><b>${pad(r.h)}:${pad(r.m)} · ${esc(actionText(r.act))}</b><em>${daysText(r.days)}</em></button><button class="${r.on?'act':''}" data-t="${i}" aria-pressed="${!!r.on}" aria-label="Ativa"><span class="sw" style="display:block"></span></button></div>`).join(''):'<p class="empty">Nenhum horário programado.</p>');
  $('rules').querySelectorAll('[data-e]').forEach(b=>{b.onclick=()=>openRule(+b.dataset.e)});
  $('rules').querySelectorAll('[data-t]').forEach(b=>{b.onclick=()=>{const r=sch.rules[b.dataset.t];saveRule(+b.dataset.t,{...r,on:r.on?0:1})}});
  $('rNew').disabled=rules.length===sch.rules.length;

  const c=$('clk');
  c.className=sch.sync?'clk':'clk bad';
  c.textContent=sch.sync?`Agora: ${DAY_NAMES[sch.day]} ${pad(sch.h)}:${pad(sch.m)}`+(sch.last?` · Última: ${sch.last}`:''):'Relógio sem horário: a programação precisa de internet no roteador.';
}

async function loadSchedule(){
  try{
    const r=await fetch('/api/schedule');
    sch=await r.json();
    renderSchedule();
  }catch(e){}
}

async function saveSchedule(body){
  try{
    const r=await fetch('/api/schedule',{method:'POST',body:new URLSearchParams(body)});
    if(!r.ok){toast('Valores recusados pela placa.');return false}
    await loadSchedule();
    return true;
  }catch(e){toast('Sem conexão com a placa.');return false}
}

const saveRule=(i,r)=>saveSchedule({kind:'rule',i,days:r.days,on:r.on,h:r.h,m:r.m,act:r.act});

function renderPresetForm(){
  pick($('pMode'),MODES.map(m=>({v:m[0],on:pf.mode===m[0],html:svg(m[2])+m[1]})),v=>{pf.mode=v;renderPresetForm()});
  $('pTempBox').hidden=pf.mode===3;
  $('pTemp').textContent=pf.temp+'°';
  pick($('pFan'),FANS.map(s=>({v:s[0],on:pf.fan===s[0],html:s[1]})),v=>{pf.fan=v;renderPresetForm()});
  $('pFix').className=pf.swing?'':'on';$('pOsc').className=pf.swing?'on':'';
  $('pDir').className=pf.swing?'segs':'segs six';
  pick($('pDir'),pf.swing?SWINGS.map((t,i)=>({v:i,on:pf.vane===i,html:t})):[1,2,3,4,5,6].map(i=>({v:i,on:pf.vane===i,html:String(i)})),v=>{pf.vane=v;renderPresetForm()});
  pick($('pOpts'),[['sleep','Dormir'],['disp','Display'],['mute','Silencioso']].map(o=>({v:o[0],on:pf[o[0]]===1,html:o[1]})),v=>{pf[v]=pf[v]?0:1;renderPresetForm()});
}

function openPreset(i){
  const old=i>=0;
  const idx=old?i:sch.presets.findIndex(p=>!p.n);
  pf=old?{...sch.presets[i]}:{n:'',mode:1,temp:TEMP_DEFAULT,fan:1,swing:0,vane:1,sleep:0,disp:1,mute:0};
  pf.i=idx;
  $('pTitle').textContent=old?'Editar modo':'Novo modo';
  $('pName').value=pf.n;
  $('pDel').hidden=!old;
  renderPresetForm();
  $('pSheet').hidden=false;
}

function renderRuleForm(){
  pick($('rDays'),DAY_KEYS.map((t,i)=>({v:i,on:!!(rf.days&(1<<i)),html:t})),v=>{rf.days^=1<<v;renderRuleForm()});
}

function openRule(i){
  const old=i>=0;
  const idx=old?i:sch.rules.findIndex(r=>!r.days);
  rf=old?{...sch.rules[i]}:{days:ALL_DAYS,on:1,h:22,m:0,act:RULE_OFF};
  rf.i=idx;
  $('rTitle').textContent=old?'Editar horário':'Novo horário';
  $('rTime').value=`${pad(rf.h)}:${pad(rf.m)}`;
  $('rAct').innerHTML=sch.presets.map((p,n)=>p.n?`<option value="${n}">${esc(p.n)}</option>`:'').join('')+`<option value="${RULE_OFF}">Desligar o ar</option>`;
  $('rAct').value=rf.act;
  if($('rAct').selectedIndex<0)$('rAct').value=RULE_OFF;
  $('rDel').hidden=!old;
  renderRuleForm();
  $('rSheet').hidden=false;
}

function stepPreset(d){
  const v=pf.temp+d;
  if(v>=TEMP_MIN&&v<=TEMP_MAX){pf.temp=v;renderPresetForm()}
}

function setPresetSwing(s){
  if(pf.swing===s)return;
  pf.swing=s;pf.vane=s?0:1;
  renderPresetForm();
}

$('pNew').onclick=()=>openPreset(-1);
$('rNew').onclick=()=>openRule(-1);
$('pMinus').onclick=()=>stepPreset(-1);
$('pPlus').onclick=()=>stepPreset(1);
$('pFix').onclick=()=>setPresetSwing(0);
$('pOsc').onclick=()=>setPresetSwing(1);
$('pSave').onclick=async()=>{
  const n=$('pName').value.trim();
  if(!n){toast('Dê um nome ao modo.');return}
  const {i,mode,temp,fan,swing,vane,sleep,disp,mute}=pf;
  if(await saveSchedule({kind:'preset',i,n,mode,temp,fan,swing,vane,sleep,disp,mute})){closeSheet();toast('Modo salvo.')}
};
$('pDel').onclick=async()=>{if(await saveSchedule({kind:'preset',i:pf.i,n:''})){closeSheet();toast('Modo apagado.')}};
$('rSave').onclick=async()=>{
  const [h,m]=$('rTime').value.split(':').map(Number);
  if(!rf.days){toast('Escolha ao menos um dia.');return}
  if(!(h>=0)){toast('Escolha o horário.');return}
  if(await saveRule(rf.i,{...rf,h,m,act:+$('rAct').value})){closeSheet();toast('Horário salvo.')}
};
$('rDel').onclick=async()=>{if(await saveRule(rf.i,{days:0,on:0,h:0,m:0,act:RULE_OFF})){closeSheet();toast('Horário apagado.')}};

renderPower();
refresh();
loadSchedule();
setInterval(()=>{if(!document.hidden)refresh()},2000);
setInterval(()=>{if(!document.hidden)loadSchedule()},SCHEDULE_POLL_MS);
document.addEventListener('visibilitychange',()=>{if(!document.hidden){refresh();loadSchedule()}});
</script>
</body>
</html>
)HTML";
