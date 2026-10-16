@echo off
rem Opens the folder with your PaRappa saves: memcard\bu00 next to parappa.exe, or the user folder
rem when security software keeps the game from saving here (see the notice the game shows).
if exist "%~dp0memcard\bu00\BASCUS-94183*" (
    start "" explorer "%~dp0memcard\bu00"
) else (
    if not exist "%APPDATA%\PaRappaPort\memcard\bu00" mkdir "%APPDATA%\PaRappaPort\memcard\bu00"
    start "" explorer "%APPDATA%\PaRappaPort\memcard\bu00"
)
