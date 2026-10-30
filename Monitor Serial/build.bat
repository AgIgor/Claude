@echo off
rem Compila o Monitor Serial usando o compilador C# que ja vem com o .NET Framework 4.x do Windows.
setlocal
cd /d "%~dp0"
set CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe
if not exist "%CSC%" set CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe
if not exist "%CSC%" (
    echo Nao encontrei o csc.exe do .NET Framework 4.x.
    exit /b 1
)

"%CSC%" /nologo /target:winexe /optimize+ /codepage:65001 ^
  /out:MonitorSerial.exe ^
  /win32manifest:src\app.manifest ^
  /win32icon:src\icon.ico ^
  /r:System.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll /r:System.Management.dll ^
  src\*.cs
if errorlevel 1 (
    echo.
    echo *** Falha na compilacao ***
    exit /b 1
)
echo Pronto: MonitorSerial.exe
