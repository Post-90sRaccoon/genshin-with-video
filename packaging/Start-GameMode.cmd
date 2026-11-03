@echo off
setlocal
set "GenshinVideo_LaunchDir=%~dp0"
powershell.exe -NoProfile -WindowStyle Hidden -Command "if (@(Get-Process -Name GenshinVideo -ErrorAction SilentlyContinue).Count) { Add-Type -AssemblyName System.Windows.Forms; [void][System.Windows.Forms.MessageBox]::Show('Exit the running overlay before starting Game Mode.','Genshin Video'); exit }; Start-Process -FilePath (Join-Path $env:GenshinVideo_LaunchDir 'GenshinVideo.exe') -Verb RunAs -WindowStyle Normal"
endlocal
