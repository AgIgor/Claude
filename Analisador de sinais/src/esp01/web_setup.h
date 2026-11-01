#pragma once
// Ajustes do ESP-01: Wi-Fi e firmware, que no ESP32 ficam no monitor.
#include <pgmspace.h>

static const char SETUP_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#0b0e14">
<title>ACRemote: ajustes</title>
<style>
*{box-sizing:border-box}
body{margin:0;background:#0b0e14;color:#eef1f6;font:16px/1.4 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
main{max-width:460px;margin:0 auto;padding:20px 16px 40px}
a{color:#7dd3fc;text-decoration:none}
h1{font-size:24px;margin:10px 0 2px}
h2{font-size:17px;margin:0 0 10px}
p{color:#98a2b3;margin:6px 0 0;font-size:14px;overflow-wrap:anywhere}
section{background:#151a23;border:1px solid #262d3a;border-radius:20px;padding:16px;margin-top:14px}
input,button{font:inherit;width:100%;padding:12px;border-radius:12px;border:1px solid #303848;background:#0f131b;color:inherit;margin-top:8px}
button{background:#232b39;border:0;cursor:pointer;font-weight:600}
button.pri{background:#7dd3fc;color:#07111c}
.two{display:grid;grid-template-columns:1fr 1fr;gap:8px}
</style>
</head>
<body>
<main>
  <a href="/">Voltar ao controle</a>
  <h1>Ajustes</h1>
  <p id="info">Carregando...</p>
  <section>
    <h2>Rede Wi-Fi</h2>
    <input id="ssid" list="nets" placeholder="Nome da rede" autocomplete="off">
    <datalist id="nets"></datalist>
    <input id="pass" type="password" placeholder="Senha" autocomplete="off">
    <div class="two"><button id="scan">Procurar redes</button><button id="save" class="pri">Conectar</button></div>
    <button id="forget">Esquecer rede</button>
    <p id="wmsg">Sem o roteador por 30 s, o ESP-01 abre a rede ACRemote (http://192.168.4.1). Ela fecha 30 s depois de reconectar.</p>
  </section>
  <section>
    <h2>Atualizar firmware</h2>
    <input id="fw" type="file" accept=".bin">
    <input id="fwpass" type="password" placeholder="Senha OTA" autocomplete="off">
    <button id="send" class="pri">Enviar</button>
    <p id="fmsg"></p>
  </section>
</main>
<script>
const $=id=>document.getElementById(id);
const post=(p,d)=>fetch(p,{method:'POST',body:new URLSearchParams(d)});
async function info(){
  try{
    const n=await (await fetch('/api/net')).json();
    $('info').textContent=`Firmware ${n.fw}. `+(n.sta?`Conectado a ${n.ssid}, IP ${n.ip}, sinal ${n.rssi} dBm.`:`Sem roteador. Rede própria em ${n.ip}.`)+` Ligado há ${n.up} s. Último reinício: ${n.reset}.`;
  }catch(e){$('info').textContent='Sem conexão com o ESP-01.'}
}
$('scan').onclick=async()=>{
  $('wmsg').textContent='Procurando...';
  for(let i=0;i<20;i++){
    const r=await (await fetch('/api/wifi/scan')).json();
    if(r.nets){
      r.nets.sort((a,b)=>b[1]-a[1]);
      const box=$('nets');
      box.textContent='';
      for(const n of r.nets){const o=document.createElement('option');o.value=n[0];box.append(o)}
      $('wmsg').textContent=`${r.nets.length} redes encontradas: toque no campo do nome.`;
      return;
    }
    await new Promise(k=>setTimeout(k,500));
  }
  $('wmsg').textContent='A busca não terminou.';
};
$('save').onclick=async()=>{await post('/api/wifi',{ssid:$('ssid').value,pass:$('pass').value});$('wmsg').textContent='Conectando...'};
$('forget').onclick=async()=>{await post('/api/wifi',{ssid:'',pass:''});$('wmsg').textContent='Rede esquecida.'};
$('send').onclick=()=>{
  const f=$('fw').files[0],msg=$('fmsg');
  if(!f){msg.textContent='Escolha o arquivo primeiro.';return}
  const x=new XMLHttpRequest(),body=new FormData();
  body.append('firmware',f);
  x.open('POST','/update?pass='+encodeURIComponent($('fwpass').value));
  x.upload.onprogress=e=>{msg.textContent=`Enviando ${Math.round(e.loaded/e.total*100)}%`};
  x.onload=()=>{
    let err='';
    try{err=JSON.parse(x.responseText).err||''}catch(e){}
    msg.textContent=x.status===200?'Gravado. Reiniciando...':x.status===403?'Senha OTA incorreta.':`Falha ao gravar, o firmware atual continua. ${err}`;
  };
  x.onerror=()=>{msg.textContent='A conexão caiu durante o envio.'};
  x.send(body);
};
info();
setInterval(info,5000);
</script>
</body>
</html>)HTML";
