param(
    # The bigworld-client-win32 package: bwclient_h.exe, offline-res, tools.
    [Parameter(Mandatory = $true)]
    [string]$NgClient
)

$ErrorActionPreference = "Stop"

$LauncherRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $LauncherRoot
$DistRoot = Join-Path $LauncherRoot "dist"
$BuildRoot = Join-Path $LauncherRoot "build"
$WorkRoot = Join-Path $BuildRoot "pyinstaller"
$SpecRoot = Join-Path $BuildRoot "spec"
# PyInstaller owns its work directory, so the payload is staged beside it.
$PayloadRoot = Join-Path $BuildRoot "payload"
$AppName = "wot-0.9.22-offline-battles-ng"
$PackedXmlModule = Join-Path $RepoRoot "tools\packed_xml.py"

if (-not (Test-Path -LiteralPath $PackedXmlModule)) {
    throw "Launcher build dependency is missing: $PackedXmlModule"
}

foreach ($directory in @($DistRoot, $BuildRoot)) {
    if (Test-Path -LiteralPath $directory) {
        Remove-Item -LiteralPath $directory -Recurse -Force
    }
}
New-Item -ItemType Directory -Force -Path $DistRoot | Out-Null
New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null
New-Item -ItemType Directory -Force -Path $SpecRoot | Out-Null

python (Join-Path $LauncherRoot "stage_payload.py") `
    --output $PayloadRoot `
    --source $RepoRoot `
    --ng-client $NgClient
if ($LASTEXITCODE -ne 0) {
    throw "Payload staging failed with exit code $LASTEXITCODE"
}

foreach ($entry in @("ng-client\bwclient_h.exe",
                     "ng-client\offline-res.json",
                     "ng-client\native-room-data.json",
                     "ng-client\mods\configs\offline_lan_0922\build_identity.json")) {
    if (-not (Test-Path -LiteralPath (Join-Path $PayloadRoot $entry))) {
        throw "Payload is incomplete: $entry"
    }
}

# A one-folder build keeps the payload on disk instead of extracting it on
# every launch and on every server start.
python -m PyInstaller `
    --noconfirm `
    --clean `
    --onedir `
    --windowed `
    --noupx `
    --version-file (Join-Path $LauncherRoot "version_info.txt") `
    --name $AppName `
    --distpath $DistRoot `
    --workpath $WorkRoot `
    --specpath $SpecRoot `
    --paths (Join-Path $RepoRoot "tools") `
    --paths (Join-Path $RepoRoot "src/res/scripts/client") `
    --hidden-import replay_launch `
    --hidden-import bot_tactics_ui `
    --hidden-import bot_tactics_smoke `
    --exclude-module gui `
    --copy-metadata numpy `
    --collect-all moderngl `
    --collect-all glcontext `
    --hidden-import PIL.ImageTk `
    --exclude-module PIL._avif `
    --hidden-import PIL.DdsImagePlugin `
    --hidden-import packed_xml `
    --hidden-import vehicle_prices `
    --hidden-import battle_wire_schema `
    --hidden-import battle_wire_codec `
    --add-data "$PayloadRoot\ng-client;ng-client" `
    --add-data "$PayloadRoot\launcher-contracts;launcher-contracts" `
    (Join-Path $LauncherRoot "wot_launcher.py")

if ($LASTEXITCODE -ne 0) {
    throw "PyInstaller failed with exit code $LASTEXITCODE"
}

$AppRoot = Join-Path $DistRoot $AppName

if (Test-Path -LiteralPath (Join-Path $AppRoot "_internal\servers")) {
    throw "The native worker-host must not carry a Python battle server payload"
}
if (Test-Path -LiteralPath (Join-Path $AppRoot "_internal\client")) {
    throw "The NG launcher must not carry the original-mod client payload"
}
if (-not (Test-Path -LiteralPath (Join-Path $AppRoot "_internal\ng-client\bwclient_h.exe"))) {
    throw "The NG launcher is missing its NG client package"
}
if (-not (Test-Path -LiteralPath (Join-Path $AppRoot "_internal\ng-client\native-room-data.json"))) {
    throw "The NG launcher is missing its native room data"
}
foreach ($entry in @("bot_tactics.py", "crew_recruits.py", "internal_layout_console.py")) {
    $Contract = Join-Path $AppRoot "_internal\launcher-contracts\gui\mods\offline_lan_0922\$entry"
    if (-not (Test-Path -LiteralPath $Contract)) {
        throw "The NG launcher is missing its data helper: $entry"
    }
}
$ForbiddenEntries = @(
    Get-ChildItem -LiteralPath $AppRoot -Recurse -Force |
        Where-Object {
            $RelativePath = $_.FullName.Substring($AppRoot.Length)
            $PathSegments = $RelativePath -split '[\\/]'
            ($PathSegments -contains "0.8.2") -or
                (($RelativePath -replace
                    '[^A-Za-z0-9]', '').ToLowerInvariant()).Contains(
                        "mapstudio")
        })
if ($ForbiddenEntries.Count -ne 0) {
    throw "0.8.2 and Map Studio must remain outside the launcher distribution"
}

Copy-Item -Force `
    (Join-Path $LauncherRoot "LAUNCHER_README.txt") `
    (Join-Path $DistRoot "$AppName\README.txt")
Copy-Item -Force `
    (Join-Path $RepoRoot "LICENSE") `
    (Join-Path $DistRoot "$AppName\LICENSE")
Copy-Item -Force `
    (Join-Path $RepoRoot "THIRD_PARTY_NOTICES.md") `
    (Join-Path $DistRoot "$AppName\THIRD_PARTY_NOTICES.md")
$LicenseRoot = Join-Path $DistRoot "$AppName\licenses"
New-Item -ItemType Directory -Force -Path $LicenseRoot | Out-Null
Copy-Item -Force `
    (Join-Path $RepoRoot "licenses\Boost-1.0.txt") `
    (Join-Path $LicenseRoot "Boost-1.0.txt")

# Shared data tools are staged explicitly outside the frozen game GUI package.
python (Join-Path $RepoRoot "tools/stage_editor_licenses.py") $LicenseRoot
if ($LASTEXITCODE -ne 0) { throw "Pillow license staging failed" }

foreach ($entry in @("$AppName.exe", "README.txt",
                     "LICENSE",
                     "THIRD_PARTY_NOTICES.md", "licenses\Boost-1.0.txt")) {
    if (-not (Test-Path -LiteralPath (Join-Path $DistRoot "$AppName\$entry"))) {
        throw "Launcher distribution is incomplete: $entry"
    }
}

Compress-Archive -Force `
    -Path (Join-Path $DistRoot $AppName) `
    -DestinationPath (Join-Path $DistRoot "$AppName-Windows.zip")

Write-Host "Built $DistRoot\$AppName\$AppName.exe"
