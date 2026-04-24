# cleanup-broken-install.ps1
# One-shot cleanup of the broken Plan 4 install. Run elevated (UAC).
# Reverses: vrpathreg adddriver, bindings patch, file copy, uninstall regkey.
# Does NOT remove %APPDATA%\MicMap\ (user data is preserved per D-13 default).

$ErrorActionPreference = 'Continue'

# --- Resolve SteamVR from HKCU (fail-closed) ---
$SteamPath = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -Name SteamPath -EA 0).SteamPath
if (-not $SteamPath) { Write-Error 'Steam not found in HKCU'; exit 1 }
$SteamPath = $SteamPath -replace '/','\'
$SteamVR   = Join-Path $SteamPath 'steamapps\common\SteamVR'
$MicMapDir = Join-Path $SteamVR 'drivers\micmap'
$Vrpathreg = Join-Path $SteamVR 'bin\win64\vrpathreg.exe'
$DevDriver = 'C:\Users\decid\Documents\projects\mic-map\build\driver\micmap'

Write-Host "=== MicMap cleanup ==="
Write-Host "SteamVR   : $SteamVR"
Write-Host "MicMapDir : $MicMapDir"
Write-Host ""

# --- 1. Refuse to continue if SteamVR is running ---
$svrRunning = Get-Process -Name vrserver,vrmonitor,vrcompositor,vrdashboard,vrwebhelper -EA 0
if ($svrRunning) {
    Write-Error ("SteamVR is running: " + ($svrRunning.Name -join ', ') + ". Close it and re-run.")
    exit 1
}

# --- 2. vrpathreg removedriver (both the installer path and the dev path) ---
if (Test-Path $Vrpathreg) {
    foreach ($p in @($MicMapDir, $DevDriver)) {
        Write-Host "vrpathreg removedriver `"$p`""
        & $Vrpathreg removedriver "$p" 2>&1 | Write-Host
    }
} else {
    Write-Warning "vrpathreg.exe not found at $Vrpathreg -- skipping driver unregister"
}

# --- 3. Unpatch bindings: copy .micmap_backup over target, then delete backup ---
$cfgRoot = Join-Path $SteamVR 'resources\config'
if (Test-Path $cfgRoot) {
    Get-ChildItem $cfgRoot -Recurse -Filter '*.micmap_backup' | ForEach-Object {
        $backup = $_.FullName
        $target = $backup -replace '\.micmap_backup$',''
        Write-Host "Restore backup: $backup -> $target"
        try {
            Copy-Item -LiteralPath $backup -Destination $target -Force
            Remove-Item -LiteralPath $backup -Force
        } catch {
            Write-Warning ("Restore failed for " + $backup + ": " + $_.Exception.Message)
        }
    }
    # Controller-type files are entirely MicMap-owned (per Plan 04-01 D-11) -- delete.
    $ownedFiles = @(
        'vrcompositor_bindings_lighthouse_hmd.json',
        'lighthouse_hmd_profile.json'
    )
    foreach ($fname in $ownedFiles) {
        $fp = Get-ChildItem $cfgRoot -Recurse -Filter $fname -EA 0 | Select-Object -First 1
        if ($fp) {
            try {
                $raw = Get-Content -LiteralPath $fp.FullName -Raw -EA 0
                if ($raw -and ($raw -match 'micmap_marker|_micmap_')) {
                    Write-Host "Delete MicMap-owned: $($fp.FullName)"
                    Remove-Item -LiteralPath $fp.FullName -Force
                }
            } catch {}
        }
    }
}

# --- 4. Delete SteamVR\drivers\micmap\ tree ---
if (Test-Path $MicMapDir) {
    Write-Host "Remove tree: $MicMapDir"
    Remove-Item -LiteralPath $MicMapDir -Recurse -Force -EA Continue
}

# --- 5. Delete Add/Remove Programs entry ---
$RegKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{BC6D91A7-A852-4562-8CBF-58FC4662FEDC}_is1'
if (Test-Path $RegKey) {
    Write-Host "Remove regkey: $RegKey"
    Remove-Item -LiteralPath $RegKey -Recurse -Force
}

# --- 6. Verify clean state ---
Write-Host ""
Write-Host "=== Post-cleanup verification ==="
Write-Host "drivers\micmap present: $(Test-Path $MicMapDir)"
Write-Host "uninstall regkey      : $(Test-Path $RegKey)"
if (Test-Path $Vrpathreg) {
    $show = & $Vrpathreg show 2>&1 | Out-String
    $micmapLines = $show -split "`n" | Where-Object { $_ -match 'micmap' }
    if ($micmapLines) {
        Write-Host "vrpathreg still lists micmap:"
        $micmapLines | ForEach-Object { Write-Host "  $_" }
    } else {
        Write-Host "vrpathreg micmap entries: NONE"
    }
}
if (Test-Path $cfgRoot) {
    $residual = Get-ChildItem $cfgRoot -Recurse -Filter '*.micmap_backup' -EA 0
    Write-Host "Residual .micmap_backup files: $($residual.Count)"
}

Write-Host ""
Write-Host "Done. You may now re-run MicMap-Setup-v0.1.0.exe."
