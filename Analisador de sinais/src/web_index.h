#pragma once
#include <pgmspace.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0b0d10">
<title>Monitor serial</title>
<style>
:root{--bg:#0b0d10;--card:#14181d;--line:#232a32;--txt:#e8edf2;--mut:#8a96a3;--acc:#38bdf8;--a:#34d399;--b:#fbbf24;--ok:#22c55e;--warn:#f59e0b;--bad:#ef4444}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--txt);font:14px/1.45 system-ui,Segoe UI,Roboto,sans-serif}
header{position:sticky;top:0;z-index:2;display:flex;flex-wrap:wrap;gap:8px 14px;align-items:center;padding:10px 16px;background:var(--card);border-bottom:1px solid var(--line)}
h1{font-size:16px;margin:0}
h2{font-size:13px;margin:0 0 8px;color:var(--mut);font-weight:600;text-transform:uppercase;letter-spacing:.04em}
main{max-width:1100px;margin:0 auto;padding:12px 16px 40px;display:grid;gap:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px;min-width:0}
.two{display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(280px,1fr))}
.pill{padding:2px 9px;border-radius:99px;border:1px solid var(--line);font-size:12px;color:var(--mut)}
.pill.ok{color:var(--ok);border-color:var(--ok)}
.pill.bad{color:var(--bad);border-color:var(--bad)}
.pill.warn{color:var(--warn);border-color:var(--warn)}
.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(88px,1fr));gap:6px}
.stats div{background:var(--bg);border-radius:6px;padding:5px 8px}
.stats b{display:block;font:600 15px ui-monospace,Consolas,monospace}
.stats span{font-size:11px;color:var(--mut)}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
button,select,input{font:inherit;color:var(--txt);background:var(--bg);border:1px solid var(--line);border-radius:6px;padding:6px 10px;min-height:34px}
button{cursor:pointer}
button:hover{border-color:var(--acc)}
button.pri{background:var(--acc);border-color:var(--acc);color:#04121b;font-weight:600}
label{display:inline-flex;gap:5px;align-items:center;color:var(--mut)}
table{border-collapse:collapse;width:100%;font-size:13px}
th{text-align:left;color:var(--mut);font-weight:500}
th,td{padding:3px 8px 3px 0;border-bottom:1px solid var(--line)}
.scroll{overflow-x:auto}
#log{height:52vh;min-height:280px;overflow:auto;background:var(--bg);border-radius:6px;padding:6px;font:12px/1.6 ui-monospace,Consolas,monospace}
.fr{display:flex;gap:10px;white-space:nowrap}
.fr .tm{color:var(--mut);min-width:84px;text-align:right}
.fr .dl{color:var(--mut);min-width:64px;text-align:right}
.fr .ch{font-weight:700;min-width:16px}
.fr .n{color:var(--mut);min-width:34px;text-align:right}
.c0 .ch{color:var(--a)}.c1 .ch{color:var(--b)}
.chg{background:#7c2d12;color:#fed7aa;border-radius:2px}
.err{color:var(--bad)}
.rep{color:var(--acc)}
.mk{color:#04121b;background:var(--acc);border-radius:4px;padding:0 8px;margin:3px 0;font-weight:600;white-space:normal}
canvas{width:100%;height:150px;display:block;background:var(--bg);border-radius:6px}
.mut{color:var(--mut);font-size:12px}
.hist{margin-top:8px;display:grid;grid-template-columns:62px 1fr 56px;gap:1px 6px;font:11px ui-monospace,Consolas,monospace;color:var(--mut);align-items:center}
.hist i{display:block;height:7px;background:var(--acc);border-radius:2px;min-width:1px}
</style>
</head>
<body>
<header>
  <h1>Monitor serial</h1>
  <a href="/" class="pill">Controle</a>
  <span id="ws" class="pill bad">desconectado</span>
  <span id="lock" class="pill">sem trava</span>
  <span id="test" class="pill warn" hidden>teste ativo</span>
  <span id="sta" class="pill">sem roteador</span>
  <span id="fwv" class="pill"></span>
</header>
<main>
  <div class="two">
    <section class="card"><h2>Linha A <span style="color:var(--a)">verde, GPIO34</span></h2><div class="stats" id="s0"></div><div class="hist" id="h0"></div></section>
    <section class="card"><h2>Linha B <span style="color:var(--b)">branco, GPIO35</span></h2><div class="stats" id="s1"></div><div class="hist" id="h1"></div></section>
  </div>

  <section class="card">
    <h2>Detecção</h2>
    <div class="scroll"><table><thead><tr><th>Linha</th><th>Baud</th><th>Formato</th><th>Nota</th><th>Bytes bons</th><th>Com erro</th></tr></thead><tbody id="cand"></tbody></table></div>
    <p class="mut" id="hint"></p>
    <div class="row">
      <label>Baud <input id="baud" type="number" min="0" style="width:100px" value="9600"></label>
      <label>Formato <select id="fmt"><option value="0">8N1</option><option value="1">8E1</option><option value="2">8O1</option><option value="3">8N2</option><option value="4">8E2</option><option value="5">8O2</option></select></label>
      <button id="apply">Travar</button>
      <button id="auto">Detectar de novo</button>
      <label>Pausa entre quadros <input id="gap" type="number" min="1" max="2000" style="width:76px"> ms</label>
      <button id="gapset">Aplicar</button>
    </div>
  </section>

  <section class="card">
    <h2>Sinal da última rajada</h2>
    <canvas id="scope"></canvas>
    <div class="row" style="margin-top:8px">
      <button id="scopeget">Atualizar</button>
      <label>Janela <input id="span" type="range" min="1" max="100" value="100"></label>
      <span class="mut" id="spanv"></span>
    </div>
  </section>

  <section class="card">
    <h2>Quadros</h2>
    <div class="row" style="margin-bottom:8px">
      <input id="mark" placeholder="O que você acabou de fazer (ex.: 24 para 25 °C)" style="flex:1;min-width:220px">
      <button id="markb" class="pri">Marcar</button>
    </div>
    <div class="row" style="margin-bottom:8px">
      <label><input type="checkbox" id="only" checked>Esconder repetidos</label>
      <label><input type="checkbox" id="fa" checked>A</label>
      <label><input type="checkbox" id="fb" checked>B</label>
      <label><input type="checkbox" id="pause">Pausar tela</label>
      <button id="csv">Exportar CSV</button>
      <button id="clear">Limpar</button>
      <span class="mut" id="count"></span>
    </div>
    <div id="log"></div>
  </section>

  <section class="card">
    <h2>Ar-condicionado</h2>
    <div class="stats" id="acs"></div>
    <div class="row" style="margin-top:8px">
      <label><input type="checkbox" id="acen">Controle ativo</label>
      <span class="mut" id="acmsg">Ative só com o módulo original desconectado e o fio TX ligado.</span>
    </div>
    <div id="acbtn" hidden>
      <div class="row" style="margin-top:8px">
        <button data-c="power" data-v="1" class="pri">Ligar</button>
        <button data-c="power" data-v="0">Desligar</button>
        <button data-c="mode" data-v="6">Automático</button>
        <button data-c="mode" data-v="1">Resfriar</button>
        <button data-c="mode" data-v="2">Aquecer</button>
        <button data-c="mode" data-v="3">Ventilador</button>
      </div>
      <div class="row" style="margin-top:8px">
        <button data-c="fan" data-v="1">Baixa</button>
        <button data-c="fan" data-v="2">Média</button>
        <button data-c="fan" data-v="4">Alta</button>
        <button data-c="fan" data-v="8">Turbo</button>
        <label>Temperatura <input id="actemp" type="number" min="16" max="30" value="23" style="width:64px"></label>
        <button id="acset">Definir</button>
      </div>
      <div class="row" style="margin-top:8px">
        <span class="mut">Fixa</span>
        <button data-c="fixed" data-v="1">1</button><button data-c="fixed" data-v="2">2</button><button data-c="fixed" data-v="3">3</button>
        <button data-c="fixed" data-v="4">4</button><button data-c="fixed" data-v="5">5</button><button data-c="fixed" data-v="6">6</button>
        <span class="mut">Oscilar</span>
        <button data-c="swing" data-v="0">Amplo</button><button data-c="swing" data-v="1">Em cima</button>
        <button data-c="swing" data-v="2">No meio</button><button data-c="swing" data-v="3">Em baixo</button>
      </div>
      <div class="row" style="margin-top:8px">
        <button data-c="sleep" data-v="1">Dormir</button><button data-c="sleep" data-v="0">Sem dormir</button>
        <button data-c="display" data-v="1">Display ligado</button><button data-c="display" data-v="0">Display desligado</button>
        <button data-c="mute" data-v="1">Silenciar</button><button data-c="mute" data-v="0">Com som</button>
        <button data-c="indirect" data-v="1">Ar indireto</button>
        <button data-c="sense" data-v="1">Auto sense</button>
        <label>Timer <input id="actimer" type="number" min="1" max="12" value="1" style="width:56px"> h</label>
        <button id="actset">Iniciar</button>
      </div>
    </div>
  </section>

  <div class="two">
    <section class="card">
      <h2>Rede Wi-Fi</h2>
      <p class="mut">Sem o roteador por 30 s, o ESP32 abre a rede ACRemote (http://192.168.4.1). Ela fecha 30 s depois de reconectar.</p>
      <div class="row">
        <input id="ssid" list="nets" placeholder="Nome da rede" style="flex:1;min-width:140px">
        <datalist id="nets"></datalist>
        <input id="wpass" type="password" placeholder="Senha" style="flex:1;min-width:120px">
      </div>
      <div class="row" style="margin-top:8px">
        <button id="wscan">Procurar redes</button>
        <button id="wsave" class="pri">Conectar</button>
        <button id="wforget">Esquecer</button>
        <span class="mut" id="wmsg"></span>
      </div>
    </section>
    <section class="card">
      <h2>Atualizar firmware</h2>
      <p class="mut">Envie o arquivo .pio/build/esp32dev/firmware.bin. A página recarrega ao terminar.</p>
      <div class="row">
        <input id="fwfile" type="file" accept=".bin" style="flex:1;min-width:160px">
        <input id="fwpass" type="password" placeholder="Senha OTA" style="width:120px">
        <button id="fwsend" class="pri">Enviar</button>
      </div>
      <p class="mut" id="fwmsg"></p>
    </section>
  </div>

  <section class="card">
    <h2>Teste de bancada</h2>
    <p class="mut">Só com o ar desconectado: ligue GPIO25 no GPIO34 e GPIO26 no GPIO35.</p>
    <div class="row">
      <label>Baud <input id="tbaud" type="number" style="width:100px" value="9600"></label>
      <label>Formato <select id="tfmt"><option value="0">8N1</option><option value="1" selected>8E1</option><option value="2">8O1</option><option value="3">8N2</option><option value="4">8E2</option><option value="5">8O2</option></select></label>
      <button id="ton">Ligar gerador</button>
      <button id="toff">Desligar</button>
    </div>
  </section>
</main>
<script>
const $=id=>document.getElementById(id);
const MAX_ENTRIES=50000,MAX_ROWS=400;
const HIST=['<5','<10','<20','<40','<80','<160','<320','<640','<1,3k','<2,6k','<5,1k','maior'];
let entries=[],lastU=0,lastAt=0,gapFilled=false,dirty=true,scopeData=null,cfgText='';
let bootId=0,clockOffset=0,lastTime=0;

function post(path,data){return fetch(path,{method:'POST',body:new URLSearchParams(data)})}
function esc(s){return s.replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function deviceNow(){return clockOffset+lastU+(performance.now()-lastAt)*1000}

// O relógio do ESP32 zera ao reiniciar: soma o tempo já visto para o log continuar em ordem.
function syncBoot(id){
  if(bootId&&id!==bootId){clockOffset=lastTime+1e6;entries.push({m:'ESP32 reiniciou',u:clockOffset});dirty=true}
  bootId=id;
}

function connect(){
  const s=new WebSocket(`ws://${location.hostname}:81`);
  s.onopen=()=>{$('ws').textContent='conectado';$('ws').className='pill ok'};
  s.onclose=()=>{$('ws').textContent='desconectado';$('ws').className='pill bad';setTimeout(connect,1500)};
  s.onmessage=e=>{
    const m=JSON.parse(e.data);
    syncBoot(m.boot);
    if(m.t==='f')addFrames(m.l);
    else if(m.t==='st')showStatus(m);
  };
}

function addFrames(list){
  for(const [c,u,h,e] of list){
    const t=u+clockOffset;
    entries.push({c,u:t,h,e});
    lastTime=Math.max(lastTime,t);
  }
  entries.sort((a,b)=>a.u-b.u);
  if(entries.length>MAX_ENTRIES)entries.splice(0,entries.length-MAX_ENTRIES);
  dirty=true;
}

function showStatus(m){
  lastU=m.u;lastAt=performance.now();
  lastTime=Math.max(lastTime,m.u+clockOffset);
  cfgText=`${m.baud?m.baud+' '+m.fmt:'sem trava'}, pausa ${m.gap} ms`;
  $('test').hidden=!m.test;
  showAc(m.ac);
  $('fwv').textContent=`firmware ${m.fw}, início: ${m.reset}`;
  const st=$('sta');
  if(m.sta){st.textContent=`${m.ssid}: ${m.ip} (${m.rssi} dBm)`;st.className='pill ok'}
  else if(m.ssid){st.textContent=`conectando em ${m.ssid}`;st.className='pill warn'}
  else{st.textContent='sem roteador';st.className='pill'}
  const l=$('lock');
  if(m.baud){l.textContent=`${m.baud} ${m.fmt}`;l.className='pill ok'}
  else{l.textContent='detectando';l.className='pill warn'}
  if(!gapFilled){$('gap').value=m.gap;if(m.baud)$('baud').value=m.baud;gapFilled=true}
  m.ch.forEach((c,i)=>{
    const rows=[['Nível',c.lvl?'alto':'baixo'],['Bordas',c.edges],['Rajadas',c.bursts],['Quadros',c.frames],['Bytes',c.bytes],
      ['Erro stop',c.ferr],['Erro par.',c.perr],['Pulso µs',c.pulse||'-'],['Baud est.',c.est||'-'],['Ruído',c.ovf],['Perdidos',c.drop]];
    $('s'+i).innerHTML=rows.map(r=>`<div><b>${r[1]}</b><span>${r[0]}</span></div>`).join('');
    const top=Math.max(1,...c.hist);
    $('h'+i).innerHTML=c.hist.map((v,k)=>`<span>${HIST[k]} µs</span><span><i style="width:${v/top*100}%"></i></span><span>${v}</span>`).join('');
  });
  let rows='';
  m.ch.forEach((c,i)=>c.cand.forEach(a=>{rows+=`<tr><td>${'AB'[i]}</td><td>${a[0]}</td><td>${a[1]}</td><td>${a[2]}%</td><td>${a[3]}</td><td>${a[4]}</td></tr>`}));
  $('cand').innerHTML=rows||'<tr><td colspan="6" class="mut">Sem candidatos ainda</td></tr>';
  const edges=m.ch[0].edges+m.ch[1].edges,hasCand=m.ch.some(c=>c.cand.length);
  $('hint').textContent=!edges?'Nenhuma borda capturada. Use o app para gerar tráfego e confira a ligação.':
    hasCand?'':'Há bordas, mas nada encaixou como UART. Veja o sinal abaixo: pode ser outro protocolo.';
}

function keyOf(e){return e.c+':'+e.h.length+':'+e.h.slice(0,4)}

function render(){
  if(!dirty||$('pause').checked)return;
  dirty=false;
  const showA=$('fa').checked,showB=$('fb').checked,only=$('only').checked;
  const prev={},rows=[];
  let prevTime=null,frames=0;
  for(const e of entries){
    if(e.m!==undefined){rows.push({mark:e});continue}
    frames++;
    if(e.c===0?!showA:!showB)continue;
    const k=keyOf(e),p=prev[k];
    if(only&&p&&p.h===e.h){p.row.rep++;continue}
    const row={e,rep:1,dt:prevTime===null?null:(e.u-prevTime)/1000,prev:p?p.h:null};
    prev[k]={h:e.h,row};
    prevTime=e.u;
    rows.push(row);
  }
  const box=$('log'),atEnd=box.scrollTop+box.clientHeight>=box.scrollHeight-30;
  box.innerHTML=rows.slice(-MAX_ROWS).map(rowHtml).join('');
  if(atEnd)box.scrollTop=box.scrollHeight;
  $('count').textContent=`${frames} quadros, ${rows.length} linhas`;
}

function rowHtml(r){
  if(r.mark)return `<div class="mk">${(r.mark.u/1e6).toFixed(3)} s: ${esc(r.mark.m)}</div>`;
  const e=r.e;
  let hex='';
  for(let i=0;i<e.h.length;i+=2){
    const b=e.h.substr(i,2),changed=r.prev&&r.prev.substr(i,2)!==b;
    hex+=(changed?`<span class="chg">${b}</span>`:b)+' ';
  }
  return `<div class="fr c${e.c}"><span class="tm">${(e.u/1e6).toFixed(3)}</span><span class="dl">${r.dt===null?'':'+'+r.dt.toFixed(1)}</span><span class="ch">${'AB'[e.c]}</span><span class="n">${e.h.length/2}</span><span>${hex}</span>${r.rep>1?`<span class="rep">×${r.rep}</span>`:''}${e.e?`<span class="err">${e.e} erro(s)</span>`:''}</div>`;
}

function addMark(){
  const t=$('mark').value.trim();
  if(!t)return;
  entries.push({m:t,u:deviceNow()});
  $('mark').value='';
  dirty=true;
}

function exportCsv(){
  const lines=['tempo_us,tipo,linha,bytes,hex,erros,marcador',`0,config,,,,,"${cfgText}"`];
  for(const e of entries){
    if(e.m!==undefined)lines.push(`${Math.round(e.u)},marcador,,,,,"${e.m.replace(/"/g,'""')}"`);
    else lines.push(`${e.u},quadro,${'AB'[e.c]},${e.h.length/2},${e.h},${e.e},`);
  }
  const a=document.createElement('a');
  a.href=URL.createObjectURL(new Blob([lines.join('\n')],{type:'text/csv'}));
  a.download=`captura_${new Date().toISOString().slice(0,19).replace(/[:T]/g,'-')}.csv`;
  a.click();
  URL.revokeObjectURL(a.href);
}

async function scanWifi(){
  $('wmsg').textContent='procurando...';
  for(let i=0;i<20;i++){
    const r=await (await fetch('/api/wifi/scan')).json();
    if(r.nets){
      r.nets.sort((a,b)=>b[1]-a[1]);
      const box=$('nets');
      box.textContent='';
      for(const n of r.nets){const o=document.createElement('option');o.value=n[0];box.append(o)}
      $('wmsg').textContent=`${r.nets.length} redes: clique no campo do nome`;
      return;
    }
    await new Promise(ok=>setTimeout(ok,700));
  }
  $('wmsg').textContent='a busca não terminou';
}

function sendFirmware(){
  const f=$('fwfile').files[0],msg=$('fwmsg');
  if(!f){msg.textContent='Escolha o arquivo primeiro.';return}
  const x=new XMLHttpRequest(),body=new FormData();
  body.append('firmware',f);
  x.open('POST','/update?pass='+encodeURIComponent($('fwpass').value));
  x.upload.onprogress=e=>{msg.textContent=`Enviando ${Math.round(e.loaded/e.total*100)}%`};
  x.onload=()=>{
    if(x.status===200){msg.textContent='Gravado. Reiniciando...';setTimeout(()=>location.reload(),7000)}
    else msg.textContent=x.status===403?'Senha OTA incorreta.':'Falha ao gravar: o firmware atual continua.';
  };
  x.onerror=()=>{msg.textContent='A conexão caiu durante o envio.'};
  x.send(body);
}

function showAc(a){
  const q=v=>v<0?'?':v,deg=v=>v<0?'?':v+' °C';
  const modes={1:'Resfriar',2:'Aquecer',3:'Ventilador',6:'Automático'},fans={1:'Baixa',2:'Média',4:'Alta',8:'Turbo'};
  const yes=v=>v<0?'?':v?'sim':'não';
  const rows=[['Estado',a.power<0?'?':a.power?'ligado':'desligado'],['Modo',a.mode<0?'?':modes[a.mode]||'valor '+a.mode],
    ['Ventilador',a.fan<0?'?':fans[a.fan]||'valor '+a.fan],['Alvo',deg(a.set)],['Oscilar',yes(a.swing)],['Aleta',q(a.vane)],
    ['Dormir',yes(a.sleep)],['Display',q(a.disp)],['Silencioso',yes(a.mute)],['Ar indireto',yes(a.ind)],['Auto sense',yes(a.sense)],
    ['Timer',a.timer<0?'?':a.timer?(a.timer/3600).toFixed(1)+' h':'não'],
    ['CA1 04.30',deg(a.ca30)],['CA1 04.33',deg(a.ca33)],['AE1 04.30',deg(a.ae30)],['AE1 04.35',deg(a.ae35)],['AE1 04.36',deg(a.ae36)]];
  if(a.on)rows.push(['Heartbeat',`${a.hbok}/${a.hb}`],['Comandos ok',a.ok],['Comandos falhos',a.fail],['Confirmações',a.acks]);
  rows.push(['Último quadro',a.rx<0?'nunca':a.rx+' s']);
  $('acs').innerHTML=rows.map(r=>`<div><b>${r[1]}</b><span>${r[0]}</span></div>`).join('');
  $('acen').checked=a.on;
  $('acbtn').hidden=!a.on;
}

function acCmd(cmd,v){
  post('/api/ac',{cmd,v}).then(r=>{$('acmsg').textContent=r.ok?'Comando na fila.':'Comando recusado.'});
}

function drawScope(){
  const cv=$('scope'),w=cv.clientWidth,h=cv.clientHeight,r=devicePixelRatio||1;
  cv.width=w*r;cv.height=h*r;
  const g=cv.getContext('2d');
  g.scale(r,r);g.clearRect(0,0,w,h);
  if(!scopeData)return;
  const longest=Math.max(1,...scopeData.c.map(c=>c.p.reduce((a,b)=>a+b,0)));
  const span=longest*$('span').value/100;
  $('spanv').textContent=`${(span/10000).toFixed(2)} ms`;
  ['#34d399','#fbbf24'].forEach((color,i)=>{
    const c=scopeData.c[i],top=10+i*(h/2),bot=top+h/2-30;
    g.strokeStyle=color;g.lineWidth=1.5;g.beginPath();
    let t=0,lvl=c.first;
    const y=l=>l?top:bot;
    g.moveTo(0,y(lvl));
    for(const p of c.p){
      t+=p;
      const x=t/span*w;
      g.lineTo(Math.min(x,w),y(lvl));
      if(x>w)break;
      lvl=lvl?0:1;
      g.lineTo(x,y(lvl));
    }
    g.stroke();
  });
}

async function getScope(){
  scopeData=await (await fetch('/api/scope')).json();
  drawScope();
}

$('apply').onclick=()=>post('/api/config',{baud:$('baud').value,fmt:$('fmt').value});
$('auto').onclick=()=>post('/api/config',{baud:0,fmt:0});
$('gapset').onclick=()=>post('/api/config',{gap:$('gap').value});
$('markb').onclick=addMark;
$('mark').onkeydown=e=>{if(e.key==='Enter')addMark()};
$('csv').onclick=exportCsv;
$('clear').onclick=()=>{entries=[];dirty=true};
$('scopeget').onclick=getScope;
$('span').oninput=drawScope;
$('ton').onclick=()=>post('/api/test',{on:1,baud:$('tbaud').value,fmt:$('tfmt').value});
$('toff').onclick=()=>post('/api/test',{on:0});
$('acen').onchange=e=>post('/api/ac/enable',{on:e.target.checked?1:0});
document.querySelectorAll('#acbtn button[data-c]').forEach(b=>b.onclick=()=>acCmd(b.dataset.c,b.dataset.v));
$('acset').onclick=()=>acCmd('temp',$('actemp').value);
$('actset').onclick=()=>acCmd('timer',$('actimer').value);
$('wscan').onclick=scanWifi;
$('wsave').onclick=async()=>{await post('/api/wifi',{ssid:$('ssid').value,pass:$('wpass').value});$('wmsg').textContent='conectando...'};
$('wforget').onclick=async()=>{await post('/api/wifi',{ssid:'',pass:''});$('wmsg').textContent='rede esquecida'};
$('fwsend').onclick=sendFirmware;
['only','fa','fb','pause'].forEach(id=>$(id).onchange=()=>{dirty=true});
addEventListener('resize',drawScope);
setInterval(render,250);
connect();
</script>
</body>
</html>)HTML";
