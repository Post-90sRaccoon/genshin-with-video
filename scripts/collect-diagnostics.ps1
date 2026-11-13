param([datetime]$Since=(Get-Date).AddMinutes(-10),[datetime]$Until=(Get-Date),[int]$TargetProcessId=0,[string]$Executable='',[Nullable[int]]$ExitCode=$null,[string]$PlaybackSessionId='',[string]$DiagnosticRoot='')
$ErrorActionPreference='Stop'
$root=if($DiagnosticRoot){[IO.Path]::GetFullPath($DiagnosticRoot)}else{Split-Path $PSScriptRoot -Parent}
$sinceUtc=$Since.ToUniversalTime()
$untilUtc=$Until.ToUniversalTime()
if($untilUtc -lt $sinceUtc){throw 'Until must be greater than or equal to Since'}
$output=Join-Path $root 'build/diagnostics'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$warnings=[Collections.Generic.List[string]]::new()
$events=[Collections.Generic.List[object]]::new()
function Protect-Text([string]$Value) {
    $Value=$Value.Replace($root,'[PROJECT]')
    if($env:USERPROFILE){$Value=$Value.Replace($env:USERPROFILE,'[PROFILE]')}
    if($env:USERNAME){$Value=$Value.Replace($env:USERNAME,'[USER]')}
    $Value=[regex]::Replace($Value,'(?i)https?://[^\s;"<>]+','[URL]')
    $Value=[regex]::Replace($Value,'(?i)(?<![\w])(?:[a-z]:\\|\\\\)[^;,\r\n]*','[PATH]')
    $Value=[regex]::Replace($Value,'(?i)\b(cookie|authorization|sessdata|bili_jct|password|token)\s*[:=]\s*[^\r\n;]+','$1=[REDACTED]')
    return $Value
}
function Test-DiagnosticInteger($Value) {
    return ($Value -is [long] -or $Value -is [int] -or $Value -is [System.Int16] -or $Value -is [byte])
}
function Test-DiagnosticString([string]$Value) {
    return ([Text.Encoding]::UTF8.GetByteCount($Value) -le 512 -and $Value -notmatch '(?i)[/\\?&=%\p{C}]|(?:https?|ftp|file):|cookie|authorization|bearer|sessdata|bili_jct|dedeuserid|csrf|password|credential|secret')
}
function Protect-DiagnosticValue($Value,[int]$Depth=0) {
    if($null -eq $Value){return $null}
    if($Depth -gt 10){return '[REDACTED]'}
    if($Value -is [bool] -or $Value -is [ValueType]){return $Value}
    if($Value -is [string]) {
        if(!(Test-DiagnosticString $Value)){return '[REDACTED]'}
        return $Value
    }
    if($Value -is [Collections.IDictionary] -or $Value -is [pscustomobject]) {
        $clean=[ordered]@{}
        $properties=if($Value -is [Collections.IDictionary]){@($Value.Keys|ForEach-Object {[pscustomobject]@{Name=[string]$_;Value=$Value[$_]}})}else{@($Value.PSObject.Properties)}
        foreach($property in @($properties|Select-Object -First 128)) {
            $name=[string]$property.Name
            # These exact libmpv property names carry safe diagnostic values.
            $mpvProperty=$name -ceq 'video-params/h' -or $name -ceq 'raw-input-rate'
            if(!$mpvProperty -and ($name -notmatch '^[A-Za-z0-9_.-]{1,80}$' -or $name -match '(?i)cookie|auth|header|url|uri|path|stdout|stderr|token|raw|message|secret|credential|password|extractor|^state(?:[_.-]|$)')){continue}
            $clean[$name]=Protect-DiagnosticValue $property.Value ($Depth+1)
        }
        return [pscustomobject]$clean
    }
    if($Value -is [Collections.IEnumerable]) {
        $clean=[Collections.Generic.List[object]]::new()
        foreach($item in @($Value|Select-Object -First 128)){$clean.Add((Protect-DiagnosticValue $item ($Depth+1)))}
        return ,($clean.ToArray())
    }
    return '[REDACTED]'
}
function Read-DiagnosticJsonl([string]$Relative,[int]$MaxBytes) {
    $path=Join-Path $root $Relative
    $metadata=[ordered]@{File=$Relative;Exists=$false;Read=$false;BytesRead=0;InvalidLines=0;InvalidEntries=0;OversizedLines=0;Status='missing'}
    $entries=[Collections.Generic.List[object]]::new()
    $stream=$null
    try {
        if(!(Test-Path -LiteralPath $path)){return [pscustomobject]@{Entries=@();Availability=[pscustomobject]$metadata}}
        $metadata.Exists=$true
        $stream=[IO.FileStream]::new($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
        if($stream.Length -gt $MaxBytes) {
            $metadata.Status='oversized'
            $warnings.Add($Relative+': file exceeds bounded byte limit; not read')
        } else {
            # A live writer may append after Length is checked. Never read past the budget.
            $bytes=[byte[]]::new($MaxBytes)
            $count=0
            while($count -lt $MaxBytes) {
                $received=$stream.Read($bytes,$count,($MaxBytes-$count))
                if($received -eq 0){break}
                $count+=$received
            }
            $metadata.BytesRead=$count
            if($stream.Length -gt $MaxBytes) {
                $metadata.Status='oversized'
                $warnings.Add($Relative+': file grew beyond bounded byte limit; partial input ignored')
            } else {
                $metadata.Read=$true
                $metadata.Status='read'
                foreach($line in ([Text.Encoding]::UTF8.GetString($bytes,0,$count) -split '\r?\n')) {
                    if([string]::IsNullOrWhiteSpace($line)){continue}
                    if([Text.Encoding]::UTF8.GetByteCount($line) -gt 4096){$metadata.OversizedLines++;continue}
                    try{$entries.Add(($line|ConvertFrom-Json -ErrorAction Stop))}catch{$metadata.InvalidLines++}
                }
                if($metadata.InvalidLines){$warnings.Add($Relative+': invalid, malformed or truncated JSONL entries ignored ('+$metadata.InvalidLines+')')}
                if($metadata.OversizedLines){$warnings.Add($Relative+': records exceed bounded line limit ('+$metadata.OversizedLines+')')}
            }
        }
    } catch {
        $metadata.Status='read_failed'
        $warnings.Add($Relative+': read failed ('+$_.Exception.GetType().Name+')')
    } finally {if($stream){$stream.Dispose()}}
    return [pscustomobject]@{Entries=$entries.ToArray();Availability=[pscustomobject]$metadata}
}
function Test-PlaybackEnvelope($Entry) {
    return ($Entry -is [pscustomobject] -and (Test-DiagnosticInteger $Entry.schema) -and $Entry.schema -eq 1 -and
        $Entry.session_id -is [string] -and $Entry.session_id -cmatch '^[A-Za-z0-9_.-]{1,100}$' -and (Test-DiagnosticString $Entry.session_id) -and
        (Test-DiagnosticInteger $Entry.pid) -and $Entry.pid -gt 0 -and $Entry.pid -le [int]::MaxValue -and
        (Test-DiagnosticInteger $Entry.utc_epoch) -and $Entry.utc_epoch -ge 0 -and $Entry.utc_epoch -le 253402300799 -and
        (Test-DiagnosticInteger $Entry.tick_ms) -and $Entry.tick_ms -ge 0 -and
        $Entry.event -is [string] -and $Entry.event -cmatch '^[A-Za-z0-9_.-]{1,80}$' -and (Test-DiagnosticString $Entry.event) -and $Entry.fields -is [pscustomobject])
}
function Test-DiagnosticInterval($Entry) {
    if(!(Test-DiagnosticInteger $Entry.utc_epoch) -or $Entry.utc_epoch -lt 0 -or $Entry.utc_epoch -gt 253402300799){return $false}
    $utc=[DateTimeOffset]::FromUnixTimeSeconds($Entry.utc_epoch).UtcDateTime
    return ($utc -ge $sinceUtc -and $utc -le $untilUtc)
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
$lifecycleAvailability=@()
foreach($relative in @('build/data/session.jsonl','dist/GenshinVideo/data/session.jsonl')) {
    # The lifecycle writer rolls over at 1 MiB before its next small record.
    $read=Read-DiagnosticJsonl $relative (1024*1024+4096)
    $lifecycleAvailability+=@($read.Availability)
    foreach($entry in $read.Entries) {
        if((Test-DiagnosticInteger $entry.pid) -and ($TargetProcessId -eq 0 -or $entry.pid -eq $TargetProcessId) -and (Test-DiagnosticInterval $entry) -and $entry.event -is [string] -and $entry.event -cmatch '^[A-Za-z0-9_.-]{1,80}$' -and (Test-DiagnosticString $entry.event)) {
            $tick=if(Test-DiagnosticInteger $entry.tick_ms){$entry.tick_ms}else{$null}
            $lifecycle+=@([pscustomobject]@{utc_epoch=$entry.utc_epoch;tick_ms=$tick;pid=$entry.pid;event=$entry.event;value=(Protect-DiagnosticValue $entry.value)})
        }
    }
}
$playbackAvailability=@()
$allPlayback=[Collections.Generic.List[object]]::new()
foreach($directory in @('build/data/diagnostics','dist/GenshinVideo/data/diagnostics')) {
    foreach($name in @('playback.2.jsonl','playback.1.jsonl','playback.jsonl')) {
        $relative=$directory+'/'+$name
        $read=Read-DiagnosticJsonl $relative (2*1024*1024)
        $playbackAvailability+=@($read.Availability)
        $invalid=0
        foreach($entry in $read.Entries) {
            if(!(Test-PlaybackEnvelope $entry)){$invalid++;continue}
            if(($TargetProcessId -eq 0 -or $entry.pid -eq $TargetProcessId) -and (!$PlaybackSessionId -or $entry.session_id -ceq $PlaybackSessionId)) {
                $allPlayback.Add([pscustomobject]@{schema=1;session_id=$entry.session_id;pid=$entry.pid;utc_epoch=$entry.utc_epoch;tick_ms=$entry.tick_ms;event=$entry.event;fields=(Protect-DiagnosticValue $entry.fields)})
            }
        }
        $read.Availability.InvalidEntries=$invalid
        if($invalid){$warnings.Add($relative+': invalid playback envelope entries ignored ('+$invalid+')')}
    }
}
$playback=@($allPlayback|Where-Object {Test-DiagnosticInterval $_}|Sort-Object utc_epoch,tick_ms)
$matchedSessions=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
foreach($entry in $playback){$matchedSessions[[string]$entry.pid+'|'+$entry.session_id]=$true}
$latestContexts=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
$sourceContexts=@()
foreach($entry in @($allPlayback|Where-Object event -eq 'source-context'|Sort-Object utc_epoch,tick_ms)) {
    $key=[string]$entry.pid+'|'+$entry.session_id
    if(!$matchedSessions.ContainsKey($key)){continue}
    $utc=[DateTimeOffset]::FromUnixTimeSeconds($entry.utc_epoch).UtcDateTime
    if($utc -lt $sinceUtc){$latestContexts[$key]=$entry}elseif($utc -le $untilUtc){$sourceContexts+=@($entry)}
}
$sourceContexts=@(@($latestContexts.Values)+$sourceContexts|Sort-Object utc_epoch,tick_ms)
$readablePlayback=@($playbackAvailability|Where-Object Read).Count
$playbackStatus=if(!$readablePlayback){'unavailable'}elseif(@($playbackAvailability|Where-Object {($_.Exists -and !$_.Read) -or $_.InvalidLines -or $_.InvalidEntries -or $_.OversizedLines}).Count){'partial'}else{'available'}
if(!$readablePlayback){$warnings.Add('playback: no readable log files; unavailable evidence does not establish playback health')}
$quarantines=@($events|Where-Object {$_.Scope -eq 'target_process' -and $_.Id -eq 1117 -and $_.Fields['Action ID'] -eq '2' -and $_.Fields['Error Code'] -eq '0x00000000'})
$finding=if($quarantines.Count){'target_process_quarantined'}elseif(@($events|Where-Object {$_.Scope -eq 'target_process' -and $_.Id -eq 1000}).Count){'target_application_fault_recorded'}elseif($TargetProcessId -ne 0 -and @($lifecycle|Where-Object event -eq 'message-loop-exit').Count){'message_loop_exit_recorded'}else{'no_exit_cause_recorded'}
$report=[ordered]@{SinceUtc=$sinceUtc.ToString('o');UntilUtc=$untilUtc.ToString('o');CollectedUtc=(Get-Date).ToUniversalTime().ToString('o');ProcessId=$TargetProcessId;Executable=(Protect-Text $Executable);ExitCode=$ExitCode;Finding=$finding;Events=@($events.ToArray());Lifecycle=$lifecycle;LifecycleAvailability=$lifecycleAvailability;PlaybackSessionId=(Protect-DiagnosticValue $PlaybackSessionId);PlaybackStatus=$playbackStatus;Playback=$playback;SourceContexts=$sourceContexts;PlaybackAvailability=$playbackAvailability;ReadWarnings=@($warnings.ToArray())}
$file=Join-Path $output ('exit-'+$TargetProcessId+'.json')
$report|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $file -Encoding UTF8
[pscustomobject]@{Finding=$finding;EventCount=$events.Count;LifecycleCount=$lifecycle.Count;PlaybackCount=$playback.Count;SourceContextCount=$sourceContexts.Count;PlaybackStatus=$playbackStatus;ReadWarnings=$warnings.Count;Report=$file}|Format-List
