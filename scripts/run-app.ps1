param([switch]$Development,[switch]$ResetTestProfile,[switch]$StopOnly,[switch]$Build,[switch]$WaitSession,[switch]$Elevated,[string]$Url='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$allowed=@((Join-Path $root 'build/GenshinVideo.exe'),(Join-Path $root 'dist/GenshinVideo/GenshinVideo.exe'))
if($Elevated -and !$StopOnly -and @(Get-Process -Name GenshinVideo -ErrorAction SilentlyContinue|Where-Object {$allowed -contains $_.Path}).Count){throw 'Close the current overlay normally before requesting an elevated launch.'}
foreach($process in @(Get-Process -Name GenshinVideo -ErrorAction SilentlyContinue)) {
    if($allowed -contains $process.Path){Stop-Process -Id $process.Id}
}
if($StopOnly){return}
if($Build){& (Join-Path $PSScriptRoot 'build.ps1')}
if($ResetTestProfile){
    if(!$Development){throw 'ResetTestProfile is limited to build/data'}
    $state=Join-Path $root 'build/data/state.json'
    if(Test-Path -LiteralPath $state){Copy-Item -LiteralPath $state -Destination (Join-Path $root 'build/data/controller-state.json') -Force;Remove-Item -LiteralPath $state}
}
$executable=if($Development){$allowed[0]}else{$allowed[1]}
if($Url -match '[\s"\r\n]'){throw 'Pass one video URL without spaces or quotes'}
$started=Get-Date
$launch=@{FilePath=$executable;WindowStyle='Normal';PassThru=$true}
if($Url){$launch.ArgumentList=$Url}
if($Elevated){$launch.Verb='RunAs'}
$appProcess=Start-Process @launch
Write-Output ('APP_PROCESS_ID='+$appProcess.Id)
if($WaitSession){$appProcess.WaitForExit();$ended=Get-Date;$result=[pscustomobject]@{ProcessId=$appProcess.Id;ExitCode=$appProcess.ExitCode;Utc=$ended.ToUniversalTime().ToString('o')};$result|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root 'build/app-exit.json') -Encoding UTF8;$result|Format-List;Start-Sleep -Seconds 3;& (Join-Path $PSScriptRoot 'collect-diagnostics.ps1') -Since $started -Until (Get-Date) -TargetProcessId $appProcess.Id -Executable $executable -ExitCode $appProcess.ExitCode}
