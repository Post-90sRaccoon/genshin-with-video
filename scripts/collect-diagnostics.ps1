param([datetime]$Since=(Get-Date).AddMinutes(-10),[datetime]$Until=(Get-Date),[int]$TargetProcessId=0,[string]$Executable='',[Nullable[int]]$ExitCode=$null)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root 'build/diagnostics'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$warnings=[Collections.Generic.List[string]]::new()
$events=[Collections.Generic.List[object]]::new()
function Protect-Text([string]$Value) {
    $Value=$Value.Replace($root,'[PROJECT]')
    if($env:USERPROFILE){$Value=$Value.Replace($env:USERPROFILE,'[PROFILE]')}
    if($env:USERNAME){$Value=$Value.Replace($env:USERNAME,'[USER]')}
    return $Value
}
foreach($query in @(
    @{LogName='Microsoft-Windows-Windows Defender/Operational';Id=1116,1117;StartTime=$Since;EndTime=$Until},
    @{LogName='Application';Id=1000,1001;StartTime=$Since;EndTime=$Until}
)) {
    $readErrors=@()
    $records=@(Get-WinEvent -FilterHashtable $query -ErrorAction SilentlyContinue -ErrorVariable readErrors)
    foreach($readError in $readErrors){if($readError.FullyQualifiedErrorId -notmatch 'NoMatchingEventsFound'){$warnings.Add($query.LogName+': '+$readError.FullyQualifiedErrorId)}}
    foreach($record in $records) {
        if($record.TimeCreated.ToUniversalTime() -lt $Since.ToUniversalTime() -or $record.TimeCreated.ToUniversalTime() -gt $Until.ToUniversalTime()){continue}
        if($record.Message -notmatch 'GenshinVideo'){continue}
        $xml=[xml]$record.ToXml()
        $fields=[ordered]@{}
        $rawPath='';$eventProcess=0
        foreach($item in $xml.Event.EventData.Data) {
            if($item.Name -eq 'Path'){$rawPath=[string]$item.'#text';$match=[regex]::Match($rawPath,'process:_pid:(\d+)\b');if($match.Success){$eventProcess=[int]$match.Groups[1].Value}}
            if($item.Name -eq 'ProcessId') {try{$rawProcess=[string]$item.'#text';$eventProcess=if($rawProcess.StartsWith('0x')){[Convert]::ToInt32($rawProcess.Substring(2),16)}else{[int]$rawProcess}}catch{}}
            if($item.Name -match '^(Threat Name|Detection ID|Detection Time|Action Name|Action ID|Error Code|Error Description|Path|Execution ID|AppName|AppVersion|ModuleName|ModuleVersion|ExceptionCode|FaultingOffset|ProcessId|AppTimeStamp|ModuleTimeStamp)$') {
                $fields[[string]$item.Name]=Protect-Text ([string]$item.'#text')
            }
        }
        if($TargetProcessId -ne 0 -and $eventProcess -ne 0 -and $eventProcess -ne $TargetProcessId){continue}
        $pathMatches=$Executable -and $rawPath.IndexOf($Executable,[StringComparison]::OrdinalIgnoreCase) -ge 0
        if($Executable -and $rawPath -and !$pathMatches){continue}
        $scope=if($TargetProcessId -ne 0 -and $eventProcess -eq $TargetProcessId){'target_process'}elseif($pathMatches){'executable_file_only'}else{'related_application'}
        $events.Add([pscustomobject]@{Utc=$record.TimeCreated.ToUniversalTime().ToString('o');Log=$query.LogName;Id=$record.Id;Scope=$scope;Fields=$fields})
    }
}
$lifecycle=@()
foreach($relative in @('build/data/session.jsonl','dist/GenshinVideo/data/session.jsonl')) {
    $path=Join-Path $root $relative
    if(Test-Path -LiteralPath $path) {
        foreach($line in @(Get-Content -LiteralPath $path -Tail 200)) {
            try {$entry=$line|ConvertFrom-Json;if(($TargetProcessId -eq 0 -or $entry.pid -eq $TargetProcessId) -and $entry.utc_epoch -ge ([DateTimeOffset]$Since).ToUnixTimeSeconds() -and $entry.utc_epoch -le ([DateTimeOffset]$Until).ToUnixTimeSeconds()){$lifecycle+=$entry}}catch{}
        }
    }
}
$quarantines=@($events|Where-Object {$_.Scope -eq 'target_process' -and $_.Id -eq 1117 -and $_.Fields['Action ID'] -eq '2' -and $_.Fields['Error Code'] -eq '0x00000000'})
$finding=if($quarantines.Count){'target_process_quarantined'}elseif(@($events|Where-Object {$_.Scope -eq 'target_process' -and $_.Id -eq 1000}).Count){'target_application_fault_recorded'}elseif($TargetProcessId -ne 0 -and @($lifecycle|Where-Object event -eq 'message-loop-exit').Count){'message_loop_exit_recorded'}else{'no_exit_cause_recorded'}
$report=[ordered]@{SinceUtc=$Since.ToUniversalTime().ToString('o');UntilUtc=$Until.ToUniversalTime().ToString('o');CollectedUtc=(Get-Date).ToUniversalTime().ToString('o');ProcessId=$TargetProcessId;Executable=(Protect-Text $Executable);ExitCode=$ExitCode;Finding=$finding;Events=@($events.ToArray());Lifecycle=$lifecycle;ReadWarnings=@($warnings.ToArray())}
$file=Join-Path $output ('exit-'+$TargetProcessId+'.json')
$report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $file -Encoding UTF8
[pscustomobject]@{Finding=$finding;EventCount=$events.Count;LifecycleCount=$lifecycle.Count;ReadWarnings=$warnings.Count;Report=$file}|Format-List
