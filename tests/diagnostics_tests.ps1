$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$fixtureRoot=Join-Path ([IO.Path]::GetTempPath()) ('gv-diagnostics-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'scripts') -Force|Out-Null
Copy-Item -LiteralPath (Join-Path $root 'scripts/collect-diagnostics.ps1') -Destination (Join-Path $fixtureRoot 'scripts/collect-diagnostics.ps1')
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
$exe=Join-Path $fixtureRoot 'build\GenshinVideo.exe'
$targetPath='file:_'+$exe+'; process:_pid:31111,ProcessStart:123'
$fileOnly=New-Fixture 1117 '2026-10-31T10:03:00Z' @{Path=('file:_'+$exe);'Action ID'='2';'Error Code'='0x00000000'}
$failed=New-Fixture 1117 '2026-10-31T10:04:00Z' @{Path=$targetPath;'Action ID'='2';'Error Code'='0x80508023'}
$other=New-Fixture 1117 '2026-10-31T10:04:00Z' @{Path=($targetPath.Replace('31111','31112'));'Action ID'='2';'Error Code'='0x00000000'}
$old=New-Fixture 1117 '2026-10-31T09:59:00Z' @{Path=$targetPath;'Action ID'='2';'Error Code'='0x00000000'}
$late=New-Fixture 1117 '2026-10-31T10:11:00Z' @{Path=$targetPath;'Action ID'='2';'Error Code'='0x00000000'}
$differentExe=New-Fixture 1117 '2026-10-31T10:04:00Z' @{Path=($targetPath.Replace('\build\','\dist\GenshinVideo\'));'Action ID'='2';'Error Code'='0x00000000'}
function Collect-Fixture {
    param([string]$SessionId='',[datetime]$From=([datetime]'2026-10-31T10:00:00Z'),[datetime]$To=([datetime]'2026-10-31T10:10:00Z'))
    & (Join-Path $fixtureRoot 'scripts/collect-diagnostics.ps1') -DiagnosticRoot $fixtureRoot -Since $From -Until $To -TargetProcessId 31111 -Executable $exe -PlaybackSessionId $SessionId|Out-Null
    Get-Content -LiteralPath (Join-Path $fixtureRoot 'build/diagnostics/exit-31111.json') -Raw|ConvertFrom-Json
}
function Write-FixtureLines([string]$Relative,[object[]]$Entries) {
    $path=Join-Path $fixtureRoot $Relative
    New-Item -ItemType Directory -Path (Split-Path $path -Parent) -Force|Out-Null
    $lines=@($Entries|ForEach-Object {if($_ -is [string]){$_}else{$_|ConvertTo-Json -Depth 20 -Compress}})
    [IO.File]::WriteAllLines($path,[string[]]$lines,[Text.UTF8Encoding]::new($false))
}
function New-Playback([string]$Event,[long]$Epoch,[string]$Session='session-a',[int]$Process=31111,[hashtable]$Fields=@{},[long]$Tick=0) {
    [ordered]@{schema=1;session_id=$Session;pid=$Process;utc_epoch=$Epoch;tick_ms=$Tick;event=$Event;fields=$Fields}
}
try {
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

# Removing the full bounded lifecycle read must lose the early exit marker here.
$global:GvDiagnosticFixtures=@()
$lifecycle=@([ordered]@{utc_epoch=1793440800;pid=31111;tick_ms=1;event='message-loop-exit';value=0})
$lifecycle+=@(1..225|ForEach-Object {[ordered]@{utc_epoch=1793440860;pid=31111;tick_ms=($_+1);event='started';value=0}})
$lifecycle+=@([ordered]@{utc_epoch=1793441401;pid=31111;event='late';value=0})
Write-FixtureLines 'build/data/session.jsonl' $lifecycle
$report=Collect-Fixture
Check ($report.Finding -eq 'message_loop_exit_recorded') 'an exit marker before the last 200 lifecycle lines must remain available'
Check ($report.Lifecycle.Count -eq 226) 'read the entire bounded lifecycle file and keep exact UTC filtering'
Write-Output 'PASS full lifecycle interval beyond the last 200 lines'

# Epochs are literal UTC boundaries: 1793440800=2026-10-31T10:00:00Z.
# Removing rotation, PID, session or UTC association must change this hand-counted set.
Write-FixtureLines 'build/data/diagnostics/playback.2.jsonl' @(
    (New-Playback 'source-context' 1793440200 'session-a' 31111 @{bvid='BV1PyxQ6sEbH';page=2;cid=11} 1),
    (New-Playback 'source-context' 1793440790 'session-a' 31111 @{bvid='BV1PyxQ6sEbH';page=3;cid=22} 2),
    (New-Playback 'source-context' 1793440798 'SESSION-A' 31111 @{bvid='BV1PyxQ6sEbH';page=8;cid=88} 3),
    (New-Playback 'source-context' 1793440799 'session-a' 31112 @{bvid='BV1PyxQ6sEbH';page=9;cid=99} 3),
    (New-Playback 'sample' 1793440799 'session-a' 31111 @{} 4)
)
Write-FixtureLines 'build/data/diagnostics/playback.1.jsonl' @(
    (New-Playback 'sample' 1793440800 'session-a' 31111 @{snapshot=@{values=@{'time-pos'=10.5;'pause'=$false;'video-params/h'=1080;'demuxer-cache-state'=@{'raw-input-rate'=4096}};status=@{'time-pos'=0}}} 10),
    '{bad-json',
    (New-Playback 'sample' 1793440900 'session-b' 31111 @{} 11),
    (New-Playback 'sample' 1793440900 'session-a' 31112 @{} 12),
    @{schema=1;session_id='session-a';pid=31111;utc_epoch=1793440900.25;tick_ms=12;event='sample';fields=@{}},
    (New-Playback 'source-context' 1793441000 'session-a' 31111 @{page=4;cid=33} 13)
)
Write-FixtureLines 'build/data/diagnostics/playback.jsonl' @(
    (New-Playback 'sample' 1793441400 'session-a' 31111 @{speed=1.75} 20),
    (New-Playback 'sample' 1793441401 'session-a' 31111 @{} 21),
    '{"schema":1,"event":"truncated'
)
Write-FixtureLines 'dist/GenshinVideo/data/diagnostics/playback.jsonl' @(
    (New-Playback 'sample' 1793441100 'session-a' 31111 @{quality='1080p'} 30)
)
$report=Collect-Fixture 'session-a'
Check ($report.Playback.Count -eq 4) 'read both build/dist rotations, filter exact UTC/PID/session and survive malformed lines'
Check ($report.SourceContexts.Count -eq 2) 'retain only latest pre-interval source context plus applicable in-interval changes'
Check ($report.SourceContexts[0].fields.cid -eq 22 -and $report.SourceContexts[1].fields.cid -eq 33) 'associate source contexts with the matched PID/session across rotation'
Check ($report.Playback[0].fields.snapshot.values.'time-pos' -eq 10.5 -and $report.Playback[0].fields.snapshot.values.'pause' -eq $false) 'preserve typed telemetry numbers and false values'
Check ($report.Playback[0].fields.snapshot.values.'video-params/h' -eq 1080) 'preserve the allowlisted mpv property with a slash in its key'
Check ($report.Playback[0].fields.snapshot.values.'demuxer-cache-state'.'raw-input-rate' -eq 4096) 'preserve allowlisted numeric cache input-rate telemetry despite the raw prefix'
Check (@($report.PlaybackAvailability|Where-Object Exists).Count -eq 4) 'record missing and present fixed playback files separately'
Check (@($report.ReadWarnings|Where-Object {$_ -match 'invalid|malformed|truncated'}).Count -ge 2) 'record per-file malformed/truncated line warnings without raw contents'
Check ($report.PlaybackStatus -eq 'partial') 'ignored corrupt records must leave an honestly partial availability status'
$report=Collect-Fixture
Check ($report.Playback.Count -eq 5) 'an omitted session filter includes other matched sessions'
Check (@($report.SourceContexts|Where-Object {$_.fields.cid -eq 88}).Count -eq 0) 'source context association must keep differently cased session identities separate'
$report=Collect-Fixture 'session-a' ([datetime]'2026-10-31T10:00:00.500Z') ([datetime]'2026-10-31T10:23:20Z')
Check (@($report.Playback|Where-Object utc_epoch -eq 1793440800).Count -eq 0) 'fractional Since boundaries must not be rounded down to include an earlier event'
Write-Output 'PASS playback rotation, exact UTC/PID/session filtering, source context and malformed/truncated lines'

# Removing recursive export sanitization must disclose at least one unique sentinel.
Write-FixtureLines 'dist/GenshinVideo/data/session.jsonl' @(
    @{utc_epoch=1793441001;pid=31111;tick_ms='https://example.invalid/LIFECYCLE_SENTINEL';event='started';value=@{cookie='LIFECYCLE_VALUE_SENTINEL'}}
)
Write-FixtureLines 'build/data/diagnostics/playback.jsonl' @(
    (New-Playback 'sample' 1793441001 'session-a' 31111 @{
        cookie='COOKIE_SENTINEL';Authorization='Bearer AUTH_SENTINEL';url='https://example.invalid/path?token=URL_SENTINEL';
        state=@{nested=@{password='PASSWORD_SENTINEL'}};snapshot=@{values=@{'time-pos'=0;'audio-codec-name'='aac';unavailable=$null};status=@{'time-pos'=0}};
        note='C:\private\PATH_SENTINEL';detail='https://example.invalid/?secret=DETAIL_SENTINEL';encoded='https%3A%2F%2Fexample.invalid%3Fkey%3DENCODED_SENTINEL';
        items=@(@{token='ARRAY_SENTINEL';safe=17},'Cookie: COOKIE_TEXT_SENTINEL','SESSDATA=SESSION_SENTINEL');
        source_context=@{bvid='BV1PyxQ6sEbH';source='public';cdn_host='upos.example.invalid';generation=5}
    } 40),
    (@{schema=1;session_id='https://example.invalid/SESSION_ID_SENTINEL';pid=31111;utc_epoch=1793441001;tick_ms=42;event='sample';fields=@{}}),
    (New-Playback 'https://example.invalid/EVENT_SENTINEL' 1793441001 'session-a' 31111 @{} 43),
    (New-Playback 'COOKIE_EVENT_SENTINEL' 1793441001 'session-a' 31111 @{} 44),
    (New-Playback 'sample' 1793441001 'COOKIE_ID_SENTINEL' 31111 @{} 45)
)
$report=Collect-Fixture
$serialized=$report|ConvertTo-Json -Depth 30
Check ($serialized -notmatch 'SENTINEL') 'exported structured fields and malformed envelopes must not expose credentials, URLs, paths or raw state'
$privacySample=@($report.Playback|Where-Object tick_ms -eq 40)[0]
Check ($privacySample.fields.snapshot.values.'time-pos' -eq 0 -and $privacySample.fields.snapshot.values.'audio-codec-name' -eq 'aac') 'privacy filtering preserves useful diagnostic primitives and safe codec values'
Check ($privacySample.fields.snapshot.values.PSObject.Properties.Name -contains 'unavailable') 'missing property null remains distinguishable from absence'
Check ($privacySample.fields.source_context.cdn_host -eq 'upos.example.invalid') 'retain host-only source evidence'
Write-Output 'PASS recursive privacy filtering for legacy and malformed records'

# Oversized files and lines must be bounded without blocking other rotated evidence.
$huge=Join-Path $fixtureRoot 'build/data/diagnostics/playback.jsonl'
[IO.File]::WriteAllText($huge,('x'*(2*1024*1024+1)))
Write-FixtureLines 'dist/GenshinVideo/data/diagnostics/playback.1.jsonl' @(('x'*8192),(New-Playback 'sample' 1793441101 'session-a' 31111 @{speed=2} 50))
$report=Collect-Fixture 'session-a'
Check (@($report.Playback|Where-Object tick_ms -eq 50).Count -eq 1) 'an oversized line/file must not hide safe events in other bounded inputs'
Check (@($report.ReadWarnings|Where-Object {$_ -match 'limit|oversiz|bounded'}).Count -ge 2) 'report oversized file and record limitations'
Check (@($report.PlaybackAvailability|Where-Object {$_.BytesRead -gt 2097152}).Count -eq 0) 'never read more than the fixed playback byte budget per file'
[IO.File]::WriteAllText((Join-Path $fixtureRoot 'build/data/session.jsonl'),('x'*(1024*1024+8192)))
$report=Collect-Fixture 'session-a'
Check ($report.Finding -eq 'no_exit_cause_recorded') 'unavailable lifecycle evidence cannot manufacture an exit finding'
Check (@($report.ReadWarnings|Where-Object {$_ -match 'session.jsonl.*limit|session.jsonl.*oversiz'}).Count -ge 1) 'report oversized lifecycle inputs rather than reading arbitrary growing files'
Write-Output 'PASS bounded files and records with honest availability warnings'

Remove-Item -LiteralPath (Join-Path $fixtureRoot 'build/data/diagnostics/playback.2.jsonl')
New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'build/data/diagnostics/playback.2.jsonl')|Out-Null
$report=Collect-Fixture 'session-a'
Check (@($report.PlaybackAvailability|Where-Object Status -eq 'read_failed').Count -eq 1) 'record individual file read failures and continue other logs'
Check ($report.PlaybackStatus -eq 'partial' -and @($report.Playback|Where-Object tick_ms -eq 50).Count -eq 1) 'a failed file read leaves an honestly partial report with usable independent evidence'
Check (($report.ReadWarnings -join ' ') -notlike ('*'+$fixtureRoot+'*')) 'read failures must not disclose local paths or raw exception text'
Write-Output 'PASS read failure isolation and privacy-safe availability metadata'
} finally {
    $resolvedFixture=[IO.Path]::GetFullPath($fixtureRoot)
    $resolvedTemp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    if($resolvedFixture.StartsWith($resolvedTemp,[StringComparison]::OrdinalIgnoreCase) -and (Split-Path $resolvedFixture -Leaf) -match '^gv-diagnostics-[a-f0-9]{32}$') {
        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
    }
}
