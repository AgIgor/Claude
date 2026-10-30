// ------------------------------------------
// Desenha a tela principal e imprime valores
// Layout 16x2:
//   Linha 0: RPM:9999TPS: 99%
//   Linha 1: MAP:255 ECT: 99C
// ------------------------------------------
void drawTela()
{
    char buf[17];
    lcd.setCursor(0, 0);
    sprintf(buf, "RPM:%4uTPS:%3u%%", rpm, tps);
    lcd.print(buf);
    lcd.setCursor(0, 1);
    sprintf(buf, "MAP:%3u ECT:%3uC", mapkpa, temp);
    lcd.print(buf);
}
