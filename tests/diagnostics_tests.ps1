$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$global:GvDiagnosticFixtures=@()
function Get-WinEvent {
    [CmdletBinding()]param([hashtable]$FilterHashtable)
    $global:GvDiagnosticFixtures|Where-Object LogName -eq $FilterHashtable.LogName
}
function New-Fixture([int]$Id,[string]$Time,[hashtable]$Fields,[string]$Log='Microsoft-Windows-Windows Defender/Operational') {
    $xml='<Event><EventData>'
    foreach($key in $Fields.Keys){$xml+='<Data Name="'+[Security.SecurityElement]::Escape($key)+'">'+[Security.SecurityElement]::Escape([string]$Fields[$key])+'</Data>'}
    $xml+='</EventData></Event>'
    $event=[pscustomobject]@{Id=$Id;TimeCreated=[datetime]$Time;Message='GenshinVideo';LogName=$Log;Xml=$xml}
    $event|Add-Member -MemberType ScriptMethod -Name ToXml -Value {$this.Xml}
    return $event
}
function Check([bool]$Value,[string]$Message){if(!$Value){throw $Message}}
$exe=Join-Path $root 'build\GenshinVideo.exe'
$targetPath='file:_'+$exe+'; process:_pid:31111,ProcessStart:123'
$fileOnly=New-Fixture 1117 '2026-10-31T10:03:00Z' @{Path=('file:_'+$exe);'Action ID'='2';'Error Code'='0x00000000'}
$failed=New-Fixture 1117 '2026-10-31T10:04:00Z' @{Path=$targetPath;'Action ID'='2';'Error Code'='0x80508023'}
$other=New-Fixture 1117 '2026-10-31T10:04:00Z' @{Path=($targetPath.Replace('31111','31112'));'Action ID'='2';'Error Code'='0x00000000'}
$old=New-Fixture 1117 '2026-10-31T09:59:00Z' @{Path=$targetPath;'Action ID'='2';'Error Code'='0x00000000'}
$late=New-Fixture 1117 '2026-10-31T10:11:00Z' @{Path=$targetPath;'Action ID'='2';'Error Code'='0x00000000'}
$differentExe=New-Fixture 1117 '2026-10-31T10:04:00Z' @{Path=($targetPath.Replace('\build\','\dist\GenshinVideo\'));'Action ID'='2';'Error Code'='0x00000000'}
function Collect-Fixture {
    & (Join-Path $root 'scripts/collect-diagnostics.ps1') -Since ([datetime]'2026-10-31T10:00:00Z') -Until ([datetime]'2026-10-31T10:10:00Z') -TargetProcessId 31111 -Executable $exe|Out-Null
    Get-Content -LiteralPath (Join-Path $root 'build/diagnostics/exit-31111.json') -Raw|ConvertFrom-Json
}
$global:GvDiagnosticFixtures=@($fileOnly,$failed,$other,$old,$late,$differentExe)
$report=Collect-Fixture
Check ($report.Finding -eq 'no_exit_cause_recorded') 'a file-only event or failed action must not prove this process was quarantined'
Check ($report.Events.Count -eq 2) 'exclude wrong PID, wrong executable and events outside the exact UTC interval'
$confirmed=New-Fixture 1117 '2026-10-31T10:05:00Z' @{Path=$targetPath;'Action ID'='2';'Error Code'='0x00000000'}
$global:GvDiagnosticFixtures+=@($confirmed)
$report=Collect-Fixture
Check ($report.Finding -eq 'target_process_quarantined') 'a successful quarantine linked to this process must be identified'
$fault=New-Fixture 1000 '2026-10-31T10:05:00Z' @{AppName='GenshinVideo.exe';ProcessId='0x7987';ExceptionCode='c0000005'} 'Application'
$global:GvDiagnosticFixtures=@($fault)
$report=Collect-Fixture
Check ($report.Finding -eq 'target_application_fault_recorded') 'identify a Windows application fault using its hexadecimal process ID'
Write-Output 'PASS diagnostic UTC interval, executable/PID association, failed action and application fault fixtures'
