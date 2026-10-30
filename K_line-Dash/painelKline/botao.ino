void botao(){
  bool leitura = digitalRead(pino_botao);

  if (leitura != estadoAnterior) {
    tempoDebounce = millis();
    estadoAnterior = leitura;
  }

  if (millis() - tempoDebounce > debounce) {

    if (leitura != estadoEstavel) {
      estadoEstavel = leitura;

      // Aciona quando SOLTA
      if (estadoEstavel == HIGH) {
        modo++;
        if (modo > total_modos)
          modo = 1;
      }
    }
  }
}  
