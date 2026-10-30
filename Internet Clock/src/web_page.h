#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="pt-br"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Relógio</title>
<style>
:root{--bg:#0e1014;--card:#171a21;--in:#0e1014;--line:#272b35;--tx:#e7e9ee;--mu:#8a909c;--ac:#3d9cff;--ok:#3ecf8e;--bad:#ff5d5d;--warn:#ffb020}
@media (prefers-color-scheme:light){:root{--bg:#f3f4f7;--card:#fff;--in:#f3f4f7;--line:#dfe2e8;--tx:#16181d;--mu:#677080}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{position:sticky;top:0;z-index:5;background:var(--bg);border-bottom:1px solid var(--line);padding:12px 16px;display:flex;align-items:center;gap:12px}
header h1{font-size:17px;margin:0;flex:1}
#dot{width:10px;height:10px;border-radius:50%;background:var(--mu)}
#dot.ok{background:var(--ok)}#dot.bad{background:var(--bad)}
nav{display:flex;gap:6px;overflow-x:auto;padding:10px 16px;border-bottom:1px solid var(--line);scrollbar-width:none}
nav a{color:var(--mu);text-decoration:none;padding:6px 12px;border-radius:999px;border:1px solid var(--line);white-space:nowrap;font-size:13px}
nav a:hover{color:var(--tx)}
main{max-width:720px;margin:auto;padding:4px 16px 96px}
section{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px 16px;margin:14px 0;scroll-margin-top:110px}
h2{font-size:13px;margin:0 0 6px;color:var(--mu);text-transform:uppercase;letter-spacing:.07em}
.row{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:10px 0;border-top:1px solid var(--line)}
h2+.row,.hint+.row{border-top:0}
.row>span{flex:1}
.row small{display:block;color:var(--mu);font-size:12px}
input,select,button{font:inherit;color:var(--tx)}
input[type=text],input[type=password],input[type=number],select{background:var(--in);border:1px solid var(--line);border-radius:8px;padding:7px 10px;min-width:0}
input[type=number]{width:96px;text-align:right}
.row input[type=text],.row input[type=password],.row select{width:55%;max-width:280px}
input[type=range]{width:min(55%,260px);accent-color:var(--ac)}
input[type=checkbox]{width:20px;height:20px;accent-color:var(--ac)}
input[type=color]{width:56px;height:32px;border:1px solid var(--line);border-radius:8px;background:none;padding:2px}
output{min-width:34px;text-align:right;color:var(--mu);font-variant-numeric:tabular-nums}
button{background:var(--in);border:1px solid var(--line);border-radius:8px;padding:8px 14px;cursor:pointer}
button:hover{border-color:var(--ac)}
button.pri{background:var(--ac);border-color:var(--ac);color:#fff}
button.dan{color:var(--bad)}
.btns{display:flex;gap:8px;flex-wrap:wrap;padding-top:10px}
.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:8px;margin-top:8px}
.st{background:var(--in);border-radius:10px;padding:10px 12px}
.st small{color:var(--mu);font-size:12px}
.st b{display:block;font-size:22px;font-weight:600;font-variant-numeric:tabular-nums}
.st i{font-style:normal;color:var(--mu);font-size:12px}
.hint{color:var(--mu);font-size:13px;margin:4px 0 6px}
#nets button{display:flex;justify-content:space-between;width:100%;margin-top:6px;text-align:left}
progress{width:100%;height:10px;accent-color:var(--ac)}
#bar{position:fixed;left:0;right:0;bottom:0;background:var(--card);border-top:1px solid var(--line);padding:12px 16px;display:none;justify-content:center;align-items:center;gap:10px;z-index:6}
#bar.on{display:flex}
#bar span{color:var(--warn);font-size:13px}
#toast{position:fixed;left:50%;top:70px;transform:translateX(-50%);background:var(--tx);color:var(--bg);padding:8px 16px;border-radius:999px;font-size:14px;opacity:0;transition:opacity .3s;pointer-events:none;z-index:9}
#toast.on{opacity:1}
#ledN{min-width:64px;text-align:center}
[hidden]{display:none!important}
</style></head><body>
<header><div id="dot"></div><h1 id="title">Relógio</h1><b id="hclock"></b></header>
<nav><a href="#s-status">Status</a><a href="#s-leds">Matriz</a><a href="#s-telas">Telas</a><a href="#s-cor">Cores</a><a href="#s-brilho">Brilho</a><a href="#s-noite">Noturno</a><a href="#s-hora">Hora</a><a href="#s-sens">Sensores</a><a href="#s-wifi">Wi-Fi</a><a href="#s-mqtt">MQTT</a><a href="#s-sys">Sistema</a></nav>
<main>

<section id="s-status"><h2>Status</h2>
<div class="stats">
<div class="st"><small>Hora</small><b id="vTime">--:--</b><i id="vDate"></i></div>
<div class="st"><small>Temperatura</small><b id="vTemp">--</b><i id="vTempRaw"></i></div>
<div class="st"><small>Umidade</small><b id="vHumi">--</b><i id="vHumiRaw"></i></div>
<div class="st"><small>Luz</small><b id="vLux">--</b><i id="vNight"></i></div>
<div class="st"><small>Brilho</small><b id="vBright">--</b><i id="vScreen"></i></div>
<div class="st"><small>Wi-Fi</small><b id="vRssi">--</b><i id="vIp"></i></div>
</div>
<div class="btns"><button onclick="act('test')">Testar LEDs</button><button onclick="act('next')">Próxima tela</button></div>
</section>

<section id="s-leds" data-own><h2>Matriz de LEDs</h2>
<p class="hint">Fita de 17 × 5 LEDs. Padrão: olhando o relógio de frente, o primeiro LED fica embaixo à direita, de baixo para cima, em zigue-zague. Clique em <b>Teste de orientação</b>: o certo é aparecer <b>12:34</b> legível, com o ponto vermelho no canto superior esquerdo.</p>
<label class="row"><span>Primeiro LED à esquerda</span><input type="checkbox" id="lyStartLeft"></label>
<label class="row"><span>Primeiro LED em cima</span><input type="checkbox" id="lyStartTop"></label>
<label class="row"><span>Zigue-zague<small>Cada linha alterna o sentido</small></span><input type="checkbox" id="lyZigzag"></label>
<label class="row"><span>Fita em colunas<small>Percorre colunas (5 LEDs) em vez de linhas (17 LEDs)</small></span><input type="checkbox" id="lyColumns"></label>
<label class="row"><span>Pino de dados<small>GPIO0 é gerado pela CPU e pode ter piscadas aleatórias com o Wi-Fi. GPIO3 (RX) usa DMA e elimina isso: mova o fio de dados para o pino RX antes de trocar. Reinicia ao mudar.</small></span><select id="lyDma"><option value="0">GPIO0 (bit-bang)</option><option value="1">GPIO3 / RX (DMA)</option></select></label>
<div class="btns"><button onclick="diag('orient')">Teste de orientação</button><button onclick="diag('all')">Degradê</button><button onclick="diag('off')">Voltar ao relógio</button><button class="pri" onclick="lySave()">Salvar orientação</button></div>
<div class="row"><span>Acender um LED da fita<small>Índice na fita, começando em 0</small></span><button onclick="ledStep(-1)">‹</button><b id="ledN">—</b><button onclick="ledStep(1)">›</button></div>
</section>

<section id="s-telas"><h2>Telas</h2>
<p class="hint">Tempo de cada tela em segundos. 0 desativa a tela.</p>
<label class="row"><span>Relógio</span><input type="number" min="0" max="3600" data-k="tClock"></label>
<label class="row"><span>Temperatura</span><input type="number" min="0" max="3600" data-k="tTemp"></label>
<label class="row"><span>Umidade</span><input type="number" min="0" max="3600" data-k="tHumi"></label>
<label class="row"><span>Transição</span><select data-k="trans" data-num><option value="0">Nenhuma</option><option value="1">Fade</option><option value="2">Varredura</option></select></label>
<label class="row"><span>Formato da hora</span><select data-k="h12" data-num><option value="0">24 horas (14:30)</option><option value="1">12 horas (2:30)</option></select></label>
<label class="row"><span>Zero à esquerda na hora</span><input type="checkbox" data-k="lz"></label>
<label class="row"><span>Piscar dois-pontos</span><input type="checkbox" data-k="blink"></label>
<label class="row"><span>Mostrar casa decimal<small>23.5° e 65.4%</small></span><input type="checkbox" data-k="tDec"></label>
<label class="row"><span>Fahrenheit</span><input type="checkbox" data-k="tF"></label>
</section>

<section id="s-cor"><h2>Cores</h2>
<label class="row"><span>Modo</span><select data-k="cMode" data-num><option value="0">Cor fixa por tela</option><option value="1">Arco-íris</option><option value="2">Arco-íris em degradê</option></select></label>
<div data-show="cMode=0">
<label class="row"><span>Relógio</span><input type="color" data-k="cClock"></label>
<label class="row"><span>Temperatura</span><input type="color" data-k="cTemp"></label>
<label class="row"><span>Umidade</span><input type="color" data-k="cHumi"></label>
</div>
<label class="row" data-show="cMode=1,2"><span>Velocidade</span><input type="range" min="1" max="10" data-k="rbSpd"><output></output></label>
</section>

<section id="s-brilho"><h2>Brilho</h2>
<label class="row"><span>Automático pelo sensor de luz</span><input type="checkbox" data-k="bAuto"></label>
<div data-show="bAuto=1">
<label class="row"><span>Brilho mínimo</span><input type="range" min="1" max="255" data-k="bMin"><output></output></label>
<label class="row"><span>Brilho máximo</span><input type="range" min="1" max="255" data-k="bMax"><output></output></label>
<label class="row"><span>Lux para brilho mínimo<small>Abaixo disso usa o mínimo</small></span><input type="number" min="0" max="65000" data-k="luxLo"></label>
<label class="row"><span>Lux para brilho máximo<small>Acima disso usa o máximo · agora: <b class="luxNow">--</b> lx</small></span><input type="number" min="1" max="65000" data-k="luxHi"></label>
</div>
<label class="row" data-show="bAuto=0"><span>Brilho</span><input type="range" min="1" max="255" data-k="bMan"><output></output></label>
</section>

<section id="s-noite"><h2>Modo noturno</h2>
<label class="row"><span>Ativar</span><select data-k="nMode" data-num><option value="0">Desligado</option><option value="1">Pelo sensor de luz</option><option value="2">Por horário</option></select></label>
<label class="row" data-show="nMode=1"><span>Ativa abaixo de (lux)<small>Agora: <b class="luxNow">--</b> lx</small></span><input type="number" min="0" max="65000" data-k="nLux"></label>
<div data-show="nMode=2">
<label class="row"><span>Início (hora)</span><input type="number" min="0" max="23" data-k="nStart"></label>
<label class="row"><span>Fim (hora)</span><input type="number" min="0" max="23" data-k="nEnd"></label>
</div>
<div data-show="nMode=1,2">
<label class="row"><span>Cor</span><input type="color" data-k="nColor"></label>
<label class="row"><span>Brilho</span><input type="range" min="1" max="255" data-k="nBright"><output></output></label>
<label class="row"><span>Mostrar só o relógio</span><input type="checkbox" data-k="nClock"></label>
</div>
</section>

<section id="s-hora"><h2>Data e hora</h2>
<label class="row"><span>Fuso horário</span><select id="tzSel">
<option value="&lt;-02&gt;2">Fernando de Noronha (UTC−2)</option>
<option value="&lt;-03&gt;3">Brasília, São Paulo, Buenos Aires (UTC−3)</option>
<option value="&lt;-04&gt;4">Manaus, Cuiabá, Porto Velho (UTC−4)</option>
<option value="&lt;-05&gt;5">Rio Branco, Bogotá, Lima (UTC−5)</option>
<option value="&lt;-04&gt;4&lt;-03&gt;,M9.1.6/24,M4.1.6/24">Santiago (com horário de verão)</option>
<option value="CST6">Cidade do México (UTC−6)</option>
<option value="EST5EDT,M3.2.0,M11.1.0">Nova York (EUA Leste)</option>
<option value="CST6CDT,M3.2.0,M11.1.0">Chicago (EUA Central)</option>
<option value="MST7MDT,M3.2.0,M11.1.0">Denver (EUA Montanha)</option>
<option value="PST8PDT,M3.2.0,M11.1.0">Los Angeles (EUA Pacífico)</option>
<option value="WET0WEST,M3.5.0/1,M10.5.0">Lisboa</option>
<option value="GMT0BST,M3.5.0/1,M10.5.0">Londres</option>
<option value="CET-1CEST,M3.5.0,M10.5.0/3">Madri, Paris, Berlim, Roma</option>
<option value="WAT-1">Luanda (UTC+1)</option>
<option value="CAT-2">Maputo (UTC+2)</option>
<option value="JST-9">Tóquio (UTC+9)</option>
<option value="UTC0">UTC</option>
<option value="custom">Personalizado (POSIX)…</option>
</select></label>
<label class="row" id="tzRow" hidden><span>String POSIX<small>Ex.: &lt;-03&gt;3</small></span><input type="text" id="tzCustom" maxlength="63"></label>
<label class="row"><span>Servidor NTP</span><input type="text" data-k="ntp" maxlength="63"></label>
</section>

<section id="s-sens"><h2>Sensores</h2>
<p class="hint">O AHT10 costuma ler acima do ambiente porque recebe o calor do ESP-01 e dos LEDs. Meça a temperatura real com um termômetro de confiança, deixe o relógio ligado por pelo menos 30 minutos e calibre. A umidade é recalculada automaticamente para a temperatura corrigida.</p>
<div class="stats">
<div class="st"><small>Sensor (bruto)</small><b id="cRaw">--</b></div>
<div class="st"><small>Corrigido</small><b id="cCor">--</b></div>
</div>
<div class="row"><span>Temperatura real (°C)</span><input type="number" step="0.1" id="realT"><button onclick="calT()">Calibrar</button></div>
<div class="row"><span>Umidade real (%)<small>Calibre a temperatura antes</small></span><input type="number" step="0.1" id="realH"><button onclick="calH()">Calibrar</button></div>
<label class="row"><span>Ajuste de temperatura (°C)</span><input type="number" step="0.1" min="-20" max="20" data-k="tOff"></label>
<label class="row"><span>Ajuste de umidade (%)</span><input type="number" step="0.1" min="-30" max="30" data-k="hOff"></label>
<label class="row"><span>Intervalo de leitura (s)<small>Leituras menos frequentes aquecem menos o sensor</small></span><input type="number" min="2" max="600" data-k="sInt"></label>
</section>

<section id="s-wifi" data-own><h2>Wi-Fi</h2>
<p class="hint" id="wifiNow"></p>
<label class="row"><span>Rede</span><input type="text" id="wSsid" maxlength="32"></label>
<label class="row"><span>Senha</span><input type="password" id="wPass" maxlength="64" placeholder="(manter atual)"></label>
<div class="btns"><button onclick="scan()">Procurar redes</button><button class="pri" onclick="saveWifi()">Conectar</button></div>
<div id="nets"></div>
</section>

<section id="s-mqtt" data-nopv><h2>MQTT</h2>
<label class="row"><span>Ativar</span><input type="checkbox" data-k="mqEn"></label>
<div data-show="mqEn=1">
<label class="row"><span>Servidor</span><input type="text" data-k="mqHost" maxlength="63"></label>
<label class="row"><span>Porta</span><input type="number" min="1" max="65535" data-k="mqPort"></label>
<label class="row"><span>Usuário</span><input type="text" data-k="mqUser" maxlength="32"></label>
<label class="row"><span>Senha</span><input type="password" data-k="mqPass" data-secret maxlength="64"></label>
<label class="row"><span>Tópico base<small>Publica &lt;base&gt;/state, recebe &lt;base&gt;/set</small></span><input type="text" data-k="mqTopic" maxlength="47"></label>
<label class="row"><span>Intervalo de publicação (s)</span><input type="number" min="5" max="3600" data-k="mqInt"></label>
<p class="hint" id="mqStat"></p>
</div>
</section>

<section id="s-sys" data-nopv><h2>Sistema</h2>
<label class="row"><span>Nome na rede<small>Acesso por http://<b id="hn"></b>.local · exige reiniciar</small></span><input type="text" data-k="hostname" maxlength="31"></label>
<label class="row"><span>Senha de administrador<small>Usuário: admin · protege a página e o OTA</small></span><input type="password" data-k="adminPass" data-secret maxlength="32"></label>
<div class="row"><span>Atualizar firmware<small>Arquivo .pio/build/esp01_1m/firmware.bin</small></span><input type="file" id="fw" accept=".bin"></div>
<progress id="fwProg" max="100" value="0" hidden></progress>
<div class="btns"><button class="pri" onclick="ota()">Enviar firmware</button><button onclick="reboot()">Reiniciar</button><button class="dan" onclick="factory()">Restaurar padrão</button></div>
<p class="hint" id="sysInfo"></p>
</section>
</main>

<div id="bar"><span>Pré-visualizando alterações</span><button onclick="discard()">Descartar</button><button class="pri" onclick="save()">Salvar</button></div>
<div id="toast"></div>

<script>
const $=s=>document.querySelector(s),$$=s=>[...document.querySelectorAll(s)];
let cfg={},st={},pt;
const api=async(p,o)=>{const r=await fetch(p,o);if(!r.ok)throw Error(r.status);return r.json()};
const post=(p,b)=>api(p,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b||{})});
function toast(m){const t=$('#toast');t.textContent=m;t.classList.add('on');clearTimeout(t._t);t._t=setTimeout(()=>t.classList.remove('on'),2500)}
const fields=()=>$$('[data-k]').filter(e=>!e.closest('[data-own]'));
function outs(){$$('input[type=range]').forEach(e=>{const o=e.nextElementSibling;if(o&&o.tagName=='OUTPUT')o.value=e.value})}
function vis(){$$('[data-show]').forEach(e=>{const[k,v]=e.dataset.show.split('=');const f=$(`[data-k=${k}]`);const val=f.type=='checkbox'?(f.checked?'1':'0'):f.value;e.hidden=!v.split(',').includes(val)});$('#tzRow').hidden=$('#tzSel').value!='custom'}
function fill(c){
 fields().forEach(e=>{const v=c[e.dataset.k];if(e.dataset.secret!==undefined){e.value='';return}if(v===undefined)return;if(e.type=='checkbox')e.checked=v;else e.value=typeof v=='boolean'?(v?'1':'0'):v});
 const sel=$('#tzSel');sel.value=c.tz;if(sel.value!==c.tz){sel.value='custom';$('#tzCustom').value=c.tz}
 $('[data-k=mqPass]').placeholder=c.mqPassSet?'•••••• (definida)':'';
 $('[data-k=adminPass]').placeholder=c.adminSet?'•••••• (definida)':'sem senha';
 $('#hn').textContent=c.hostname;$('#title').textContent=c.hostname;
 $('#wSsid').value=c.ssid;outs();vis();
}
function collect(full){const o={};fields().forEach(e=>{const k=e.dataset.k;if(!full&&e.closest('[data-nopv]'))return;if(e.dataset.secret!==undefined){if(full&&e.value)o[k]=e.value;return}
 o[k]=e.type=='checkbox'?e.checked:(e.type=='number'||e.type=='range'||e.dataset.num!==undefined)?+e.value:e.value});
 o.tz=$('#tzSel').value=='custom'?$('#tzCustom').value:$('#tzSel').value;return o}
function dirty(on){$('#bar').classList.toggle('on',on)}
document.addEventListener('input',e=>{const t=e.target;if(t.closest('[data-own]')||t.type=='file')return;
 if(!t.dataset.k&&!t.id.startsWith('tz'))return;outs();vis();dirty(true);
 if(t.closest('[data-nopv]'))return;clearTimeout(pt);pt=setTimeout(()=>post('/api/config?save=0',collect(false)).catch(()=>{}),150)});
async function save(){try{const r=await post('/api/config?save=1',collect(true));toast(r.reboot?'Salvo. Reinicie para aplicar o novo nome.':'Salvo');dirty(false);await load()}catch(e){toast('Erro ao salvar')}}
async function discard(){await post('/api/config?save=0',cfg).catch(()=>{});fill(cfg);dirty(false)}
async function load(){cfg=await api('/api/config');fill(cfg)}
async function act(a){await post('/api/'+a).catch(()=>{})}
const fmtUp=s=>{const d=s/86400|0,h=s/3600%24|0,m=s/60%60|0;return(d?d+'d ':'')+h+'h '+m+'m'};
async function status(){
 try{st=await api('/api/status');$('#dot').className='ok';
  $('#vTime').textContent=st.time||'--:--';$('#hclock').textContent=st.time?st.time.slice(0,5):'';
  $('#vDate').textContent=st.date?(st.date+(st.synced?'':' · sem NTP')):'aguardando NTP';
  const t=st.temp!=null,f=cfg.tF;
  $('#vTemp').textContent=t?(f?(st.temp*1.8+32).toFixed(1)+'°F':st.temp.toFixed(1)+'°C'):'--';
  $('#vTempRaw').textContent=t?'sensor '+st.rawT.toFixed(1)+'°C':'sensor não encontrado';
  $('#vHumi').textContent=t?st.humi.toFixed(1)+'%':'--';
  $('#vHumiRaw').textContent=t?'sensor '+st.rawH.toFixed(1)+'%':'';
  $('#vLux').textContent=st.lux!=null?st.lux+' lx':'--';$$('.luxNow').forEach(e=>e.textContent=st.lux??'--');
  $('#vNight').textContent=st.night?'modo noturno':'';
  $('#vBright').textContent=st.bright;$('#vScreen').textContent=st.screen;
  $('#vRssi').textContent=st.connected?st.rssi+' dBm':(st.ap?'portal':'--');
  $('#vIp').textContent=st.connected?st.ip:(st.ap?st.apName:'desconectado');
  $('#cRaw').textContent=t?st.rawT.toFixed(1)+'°C · '+st.rawH.toFixed(1)+'%':'--';
  $('#cCor').textContent=t?st.temp.toFixed(1)+'°C · '+st.humi.toFixed(1)+'%':'--';
  $('#wifiNow').textContent=st.connected?`Conectado a "${st.ssid}" · ${st.ip} · ${st.rssi} dBm`:(st.ap?`Sem conexão. Portal aberto: ${st.apName} (192.168.4.1)`:'Conectando…');
  $('#mqStat').textContent=st.mqtt?'Conectado ao servidor':'Desconectado';
  $('#sysInfo').textContent=`Versão ${st.ver} · ligado há ${fmtUp(st.uptime)} · memória livre ${(st.heap/1024).toFixed(1)} KB (frag. ${st.frag}%) · firmware ${(st.sketch/1024)|0} KB, espaço p/ OTA ${(st.free/1024)|0} KB · último reset: ${st.reset}`;
 }catch(e){$('#dot').className='bad'}
 setTimeout(status,2000)}
function calT(){const r=parseFloat($('#realT').value);if(isNaN(r)||st.rawT==null)return toast('Informe a temperatura real');
 const e=$('[data-k=tOff]');e.value=(r-st.rawT).toFixed(1);e.dispatchEvent(new Event('input',{bubbles:true}));toast('Ajuste aplicado. Clique em Salvar.')}
function calH(){const r=parseFloat($('#realH').value);if(isNaN(r)||st.humi==null)return toast('Informe a umidade real');
 const e=$('[data-k=hOff]');const base=st.humi-(+e.value);e.value=(r-base).toFixed(1);e.dispatchEvent(new Event('input',{bubbles:true}));toast('Ajuste aplicado. Clique em Salvar.')}
async function scan(){const n=$('#nets');n.textContent='Procurando…';
 for(let i=0;i<20;i++){const r=await api('/api/scan').catch(()=>({}));
  if(r.list){n.innerHTML='';r.list.sort((a,b)=>b.rssi-a.rssi).forEach(w=>{const b=document.createElement('button');
   const s=document.createElement('span');s.textContent=w.ssid;const q=document.createElement('small');q.textContent=(w.lock?'🔒 ':'')+w.rssi+' dBm';
   b.append(s,q);b.onclick=()=>{$('#wSsid').value=w.ssid;$('#wPass').focus()};n.append(b)});
   if(!r.list.length)n.textContent='Nenhuma rede encontrada';return}
  await new Promise(r=>setTimeout(r,1000))}n.textContent='Falha na busca'}
async function saveWifi(){const o={ssid:$('#wSsid').value};if($('#wPass').value)o.pass=$('#wPass').value;
 await post('/api/config?save=1',o).catch(()=>{});$('#wPass').value='';toast('Conectando a '+o.ssid+'…')}
function reboot(){if(confirm('Reiniciar o relógio?'))act('reboot').then(()=>toast('Reiniciando…'))}
function factory(){if(confirm('Apagar todas as configurações, inclusive o Wi-Fi?'))act('factory').then(()=>toast('Restaurado. Reiniciando…'))}
function ota(){const f=$('#fw').files[0];if(!f)return toast('Escolha o arquivo .bin');
 const fd=new FormData();fd.append('firmware',f,f.name);const x=new XMLHttpRequest(),p=$('#fwProg');p.hidden=false;p.value=0;
 x.upload.onprogress=e=>p.value=e.loaded/e.total*100;
 x.onload=()=>{let r={};try{r=JSON.parse(x.responseText)}catch(e){}if(r.ok){toast('Atualizado! Reiniciando…');setTimeout(()=>location.reload(),15000)}else{p.hidden=true;toast('Falha na atualização');alert('Falha: '+(r.error||x.status))}};
 x.onerror=()=>toast('Erro de conexão');x.open('POST','/update');x.send(fd)}
let ledCur=-1;
const diag=(mode,n)=>post('/api/diag',{mode,n:n||0}).catch(()=>{});
const LY=['startLeft','startTop','zigzag','columns'],lyId=k=>'#ly'+k[0].toUpperCase()+k.slice(1);
const lyVals=()=>Object.fromEntries(LY.map(k=>[k,$(lyId(k)).checked]));
async function lyLoad(){const r=await api('/api/layout');LY.forEach(k=>$(lyId(k)).checked=r[k]);$('#lyDma').value=r.dma?'1':'0';
 $('#lyDma').onchange=async e=>{const v=e.target.value=='1';if(!confirm(v?'Mover os dados para o GPIO3/RX? O fio de dados da fita precisa estar ligado no pino RX. O relógio vai reiniciar.':'Voltar os dados para o GPIO0? O relógio vai reiniciar.')){e.target.value=v?'0':'1';return}
  await post('/api/layout?save=1',{dma:v}).catch(()=>{});toast('Reiniciando…');setTimeout(()=>location.reload(),8000)};
 LY.forEach(k=>$(lyId(k)).onchange=()=>post('/api/layout?save=0',lyVals()).then(()=>diag('orient')))}
async function lySave(){await post('/api/layout?save=1',lyVals()).then(()=>toast('Orientação salva')).catch(()=>toast('Erro ao salvar'));diag('off')}
function ledShow(i){ledCur=i;$('#ledN').textContent='LED '+i;diag('led',i)}
function ledStep(d){ledShow(ledCur<0?0:(ledCur+d+85)%85)}
load().catch(()=>{});lyLoad().catch(()=>{});status();
</script></body></html>)HTML";
