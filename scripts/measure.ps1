param([Parameter(Mandatory=$true)][int]$TargetProcessId,[int]$Seconds=600,[string]$Scenario='playing')
$ErrorActionPreference = 'Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/metrics-'+$Scenario+'.csv')
$rows=[Collections.Generic.List[object]]::new()
$previous=Get-Process -Id $TargetProcessId
$cpu=$previous.CPU
$time=Get-Date
$end=$time.AddSeconds($Seconds)
while($true) {
    $p=Get-Process -Id $TargetProcessId
    $now=Get-Date
    $elapsed=($now-$time).TotalSeconds
    $usage=if($elapsed -gt 0){100*($p.CPU-$cpu)/$elapsed/[Environment]::ProcessorCount}else{0}
    $privateWs=$null;$gpuDedicated=$null;$gpuShared=$null
    try {$perf=Get-CimInstance Win32_PerfFormattedData_PerfProc_Process -Filter "IDProcess=$TargetProcessId"; $privateWs=$perf.WorkingSetPrivate} catch {}
    try {$g=Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory | Where-Object Name -match "pid_$($TargetProcessId)_";if($g){$gpuDedicated=($g|Measure-Object DedicatedUsage -Sum).Sum;$gpuShared=($g|Measure-Object SharedUsage -Sum).Sum}}catch{}
    $children=@(Get-CimInstance Win32_Process -Filter "ParentProcessId=$TargetProcessId")
    $childBytes=0;foreach($child in $children){try{$childBytes+=(Get-Process -Id $child.ProcessId).PrivateMemorySize64}catch{}}
    $rows.Add([pscustomobject]@{Utc=$now.ToUniversalTime().ToString('o');Scenario=$Scenario;PrivateBytes=$p.PrivateMemorySize64;WorkingSet=$p.WorkingSet64;PrivateWorkingSet=$privateWs;CpuPercent=[Math]::Round($usage,3);GpuDedicated=$gpuDedicated;GpuShared=$gpuShared;ChildCount=$children.Count;ChildPrivateBytes=$childBytes})
    $rows | Export-Csv -LiteralPath $out -NoTypeInformation -Encoding UTF8
    $cpu=$p.CPU;$time=$now
    if($now -ge $end){break}
    Start-Sleep -Seconds 5
}
$summary=[pscustomobject]@{Scenario=$Scenario;Samples=$rows.Count;FirstUtc=$rows[0].Utc;LastUtc=$rows[-1].Utc;PrivateMiBMin=[Math]::Round(($rows|Measure-Object PrivateBytes -Minimum).Minimum/1MB,2);PrivateMiBMax=[Math]::Round(($rows|Measure-Object PrivateBytes -Maximum).Maximum/1MB,2);PrivateMiBLast=[Math]::Round($rows[-1].PrivateBytes/1MB,2);WorkingSetMiBLast=[Math]::Round($rows[-1].WorkingSet/1MB,2);PrivateWorkingSetMiBLast=if($null -ne $rows[-1].PrivateWorkingSet){[Math]::Round($rows[-1].PrivateWorkingSet/1MB,2)}else{$null};GpuDedicatedMiBLast=if($null -ne $rows[-1].GpuDedicated){[Math]::Round($rows[-1].GpuDedicated/1MB,2)}else{$null};GpuSharedMiBLast=if($null -ne $rows[-1].GpuShared){[Math]::Round($rows[-1].GpuShared/1MB,2)}else{$null};CpuPercentMean=[Math]::Round(($rows|Measure-Object CpuPercent -Average).Average,3);MaxImmediateChildren=($rows|Measure-Object ChildCount -Maximum).Maximum;Csv=$out}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root ('build/metrics-'+$Scenario+'-summary.json')) -Encoding UTF8
$summary | Format-List
