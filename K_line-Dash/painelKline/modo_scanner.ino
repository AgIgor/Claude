// ------------------------------------------
// Desenha a tela Scanner e imprime valores
// Alterna entre 2 páginas a cada 2,5 segundos:
//   Página 0 - Linha 0: RPM:9999TPS: 99%
//              Linha 1: MAP:255 ECT: 99C
//   Página 1 - Linha 0: IAT: 99CADV:  12
//              Linha 1: O2:1275  LD: 99%
// ------------------------------------------
void drawScanner()
{
    static uint8_t page = 0;
    static unsigned long lastFlip = 0;
    char buf[17];

    if (millis() - lastFlip > 2500UL) {
        page ^= 1;
        lastFlip = millis();
        lcd.clear();
    }

    if (page == 0) {
        lcd.setCursor(0, 0);
        sprintf(buf, "RPM:%4uTPS:%3u%%", rpm, tps);
        lcd.print(buf);
        lcd.setCursor(0, 1);
        sprintf(buf, "MAP:%3u ECT:%3uC", mapkpa, temp);
        lcd.print(buf);
    } else {
        lcd.setCursor(0, 0);
        sprintf(buf, "IAT:%3uCADV:%4d", temp_ar, (int)(adv + 0.5f));
        lcd.print(buf);
        lcd.setCursor(0, 1);
        sprintf(buf, "O2:%4u  LD:%3u%%", tensao_sonda_1, carga);
        lcd.print(buf);
    }
}
