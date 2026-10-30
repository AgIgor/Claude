@echo off
REM Gera dist\GravadorSerial.exe (um unico arquivo, nao precisa de Python instalado)
python -m pip install --user esptool pyserial pyinstaller
python -m PyInstaller --noconfirm --onefile --windowed --name GravadorSerial ^
  --add-data "tools;tools" ^
  --collect-all esptool ^
  gravador.py
echo.
echo Pronto: dist\GravadorSerial.exe
pause
