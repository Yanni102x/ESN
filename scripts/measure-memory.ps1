param(
  [int]$Notes = 6,
  [int]$Items = 50,
  [string]$Root = (Join-Path $env:TEMP "ESN-mem"),
  [string]$Exe = (Join-Path $PSScriptRoot "..\build\dist\ESN.exe"),
  [int]$SettleSeconds = 5,
  [int]$SampleSeconds = 15,
  [switch]$KeepRunning
)

$ErrorActionPreference = "Stop"
$dataDir = Join-Path $Root "ESN"
New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
Get-Process esn -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400
Remove-Item (Join-Path $dataDir "data.json*") -Force -ErrorAction SilentlyContinue

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class Dpi { [DllImport("user32.dll")] public static extern uint GetDpiForSystem(); }
"@
$dpi = [Dpi]::GetDpiForSystem()
$noteW = [int](340 * $dpi / 96); $noteH = [int](440 * $dpi / 96)

$notesJson = @()
for ($i = 0; $i -lt $Notes; $i++) {
  $itemsJson = @()
  for ($j = 0; $j -lt $Items; $j++) {
    $itemsJson += [ordered]@{
      id = "i_$i`_$j"; text = "第 $($i+1) 张便签的第 $($j+1) 条待办事项"; done = ($j % 3 -eq 0); indent = 0
    }
  }
  $x = 120 + ($i % 4) * 60
  $y = 100 + [math]::Floor($i / 4) * 60
  $notesJson += [ordered]@{
    id = "n_$i"; name = "便签 $($i+1)"; pinned = $false
    rect = @($x, $y, $noteW, $noteH)
    items = $itemsJson
  }
}
$fixture = [ordered]@{
  schema = 1
  settings = [ordered]@{ skin = "paper"; checked = "strike"; autostart = $false }
  notes = $notesJson
} | ConvertTo-Json -Depth 10
[IO.File]::WriteAllText((Join-Path $dataDir "data.json"), $fixture, [Text.UTF8Encoding]::new($false))

$env:ESN_DATA_DIR = $dataDir
$p = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds $SettleSeconds
if ($p.HasExited) { throw "process exited with code $($p.ExitCode)" }

$samples = @()
$end = (Get-Date).AddSeconds($SampleSeconds)
while ((Get-Date) -lt $end) {
  $p.Refresh()
  $samples += $p.PrivateMemorySize64
  Start-Sleep -Milliseconds 300
}
$peak = ($samples | Measure-Object -Maximum).Maximum
$avg = ($samples | Measure-Object -Average).Average
$ws = $p.WorkingSet64
$threads = $p.Threads.Count

[pscustomobject]@{
  Notes = $Notes
  ItemsPerNote = $Items
  PrivatePeakMiB = [math]::Round($peak / 1MB, 2)
  PrivateAvgMiB = [math]::Round($avg / 1MB, 2)
  WorkingSetMiB = [math]::Round($ws / 1MB, 2)
  Threads = $threads
  Budget = "<= 10.00"
  Pass = ($peak / 1MB) -le 10.0
} | Format-List

if (-not $KeepRunning) { Stop-Process -Id $p.Id -Force }
