# log_probe.ps1 -- get a compact overview of a log file WITHOUT loading it fully.
# Usage: powershell -File log_probe.ps1 -Path <path-to-log-file> [-TopN 10]

param(
    [Parameter(Mandatory=$true)][string]$Path,
    [int]$TopN = 10
)

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    Write-Error "File not found: $Path"
    exit 1
}

Write-Host "== File =="
Write-Host "Path: $Path"
$item = Get-Item -LiteralPath $Path
"{0:N1} KB" -f ($item.Length / 1KB) | ForEach-Object { Write-Host "Size: $_" }

# Line count without loading the whole file into memory as one blob.
$lineCount = 0
$reader = [System.IO.File]::OpenText($Path)
try {
    while ($null -ne $reader.ReadLine()) { $lineCount++ }
} finally {
    $reader.Close()
}
Write-Host "Lines: $lineCount"
Write-Host ""

Write-Host "== First & last lines (format check) =="
Write-Host "--- head ---"
Get-Content -LiteralPath $Path -TotalCount 3
Write-Host "--- tail ---"
Get-Content -LiteralPath $Path -Tail 3
Write-Host ""

Write-Host "== Approximate time range =="
$firstLine = Get-Content -LiteralPath $Path -TotalCount 1
$lastLine = Get-Content -LiteralPath $Path -Tail 1
$tsPattern = '\d{4}-\d{2}-\d{2}[ T][\d:.,]+'
$m1 = [regex]::Match($firstLine, $tsPattern)
$m2 = [regex]::Match($lastLine, $tsPattern)
if ($m1.Success) { Write-Host $m1.Value } else { Write-Host "(no ISO-style timestamp detected on first line)" }
if ($m2.Success) { Write-Host $m2.Value } else { Write-Host "(no ISO-style timestamp detected on last line)" }
Write-Host ""

Write-Host "== Log level counts (best-effort, case-insensitive) =="
$levels = "FATAL","CRITICAL","ERROR","EXCEPTION","WARN","WARNING","INFO","DEBUG","TRACE"
foreach ($level in $levels) {
    $count = (Select-String -LiteralPath $Path -Pattern "\b$level\b" -CaseSensitive:$false | Measure-Object).Count
    if ($count -gt 0) {
        "{0,-10} {1}" -f $level, $count | Write-Host
    }
}
Write-Host ""

Write-Host "== Top $TopN recurring error/exception lines (normalized, most frequent first) =="
$errorLines = Select-String -LiteralPath $Path -Pattern 'error|exception|fatal|panic|traceback' -CaseSensitive:$false
if ($errorLines.Count -eq 0) {
    Write-Host "(no error-like lines found)"
} else {
    $errorLines |
        ForEach-Object {
            $line = $_.Line
            $line = [regex]::Replace($line, $tsPattern, '')
            $line = [regex]::Replace($line, '\d+', 'N')
            $line.Trim()
        } |
        Group-Object |
        Sort-Object Count -Descending |
        Select-Object -First $TopN |
        ForEach-Object { "{0,6}  {1}" -f $_.Count, $_.Name | Write-Host }
}
Write-Host ""

Write-Host "== Next steps =="
Write-Host "Use Select-String / Get-Content -Tail / -TotalCount to drill into specific lines -- avoid viewing the full file."