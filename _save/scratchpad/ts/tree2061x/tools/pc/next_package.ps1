# The PC worker's queue (docs/PC_WORKER.md), for PowerShell (Git for Windows' sh cannot fork inside the PC session):
# prints the id of the oldest package on origin/claude/pc-queue that has not been started (no results/<id>/started on
# origin/pc/results) and is not cancelled (no queue/<id>.cancelled), and exits 0. Without one it exits 1, or with
# -Wait fetches again every 60 s until there is one. The same as tools/pc/next_package.sh.
# usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\pc\next_package.ps1 [-Wait]
param([switch]$Wait)
while ($true) {
    git fetch -q origin claude/pc-queue 2>$null
    git fetch -q origin pc/results 2>$null   # (there is no pc/results until the first result)
    $files = @(git ls-tree --name-only origin/claude/pc-queue queue/ 2>$null) |
        Where-Object { $_ -like '*.md' -and $_ -ne 'queue/README.md' } | Sort-Object
    foreach ($f in $files) {
        $id = [System.IO.Path]::GetFileNameWithoutExtension($f)
        git cat-file -e "origin/claude/pc-queue:queue/$id.cancelled" 2>$null
        if ($LASTEXITCODE -eq 0) { continue }
        git cat-file -e "origin/pc/results:results/$id/started" 2>$null
        if ($LASTEXITCODE -eq 0) { continue }
        Write-Output $id
        exit 0
    }
    if (-not $Wait) { exit 1 }
    Start-Sleep -Seconds 60
}
