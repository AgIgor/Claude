bool lista_pids(uint32_t value, uint8_t offset)
{
  const uint8_t* v = reinterpret_cast<const uint8_t*>(&value);
  bool is_more = false;
  for (uint8_t i = 0; i < 4; i++)
  {
    const uint8_t b = v[3 - i];
    // Serial.print("Now handling b: "); Serial.println(b, HEX);
    for (int8_t j = 7; j >= 0; j--)
    {
      uint8_t PID = offset + (i + 1) * 8 - j;
      bool supported = b & (1 << j);

        if (supported){
          pidList[pidCount++] = PID;
        }
      
        is_more = supported;  // last read bit is always whether the next block is supported.
    }
  }
  return is_more;
}

// ------------------------------------------
// Desenha lista de PIDs disponíveis
// Exibe até 10 PIDs por página (5 por linha)
// Pagina automaticamente a cada 3 segundos
// Exemplo: "20 0C 11 0B 05  "
// ------------------------------------------
void drawPids()
{
    static uint8_t page = 0;
    static unsigned long lastFlip = 0;
    const uint8_t pidsPerPage = 10;
    const uint8_t pidsPerRow  = 5;
    char buf[4];

    if (pidCount > pidsPerPage && millis() - lastFlip > 3000UL) {
        uint8_t pages = (pidCount + pidsPerPage - 1) / pidsPerPage;
        page = (page + 1) % pages;
        lastFlip = millis();
    }

    uint8_t startIdx = page * pidsPerPage;

    lcd.setCursor(0, 0);
    for (uint8_t i = 0; i < pidsPerRow; i++) {
        uint8_t idx = startIdx + i;
        if (idx < pidCount) sprintf(buf, "%02X ", pidList[idx]);
        else                memcpy(buf, "   ", 4);
        lcd.print(buf);
    }
    lcd.print(" ");  // preenche posição 15

    lcd.setCursor(0, 1);
    for (uint8_t i = 0; i < pidsPerRow; i++) {
        uint8_t idx = startIdx + pidsPerRow + i;
        if (idx < pidCount) sprintf(buf, "%02X ", pidList[idx]);
        else                memcpy(buf, "   ", 4);
        lcd.print(buf);
    }
    lcd.print(" ");  // preenche posição 15
}
