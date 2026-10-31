#Requires -Version 7.0
param(
    [Parameter(Mandatory = $true)]
    [string]$WorkDirectory
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false
Set-StrictMode -Version Latest

if (-not $IsWindows) {
    throw "The BigWorld client build requires Windows and Visual Studio 2022."
}

$Revision = "3d3cc16ae60bb1d71cb014e9b0539e54e6d8c4fb"
$D3dxVersion = "9.29.952.8"
$ScaleformRevision = "7d92be560bf1b2e4ecae9ac69e340d4cae9a557e"
$ScaleformVersion = "4.2.22"
$GlooxVersion = "1.0.28"
$OfflineForkRevision = "144619f6eb2925f8da4b13637bf65b8550fc898f"
$RepositoryRoot = Split-Path -Parent $PSScriptRoot
$StandaloneRoot = Join-Path $RepositoryRoot "native\standalone_client\bigworld"
$StandaloneCmake = Join-Path $StandaloneRoot "CMakeLists.txt"
$ScaleformCmake = Join-Path $StandaloneRoot "scaleform_sdk.cmake"
if (-not (Test-Path -LiteralPath $StandaloneCmake)) {
    throw "The standalone application CMake fragment is missing: $StandaloneCmake"
}
$WorkRoot = [IO.Path]::GetFullPath($WorkDirectory)
if (Test-Path -LiteralPath $WorkRoot) {
    throw "Use a new work directory; existing files will not be replaced: $WorkRoot"
}

$SourceRoot = Join-Path $WorkRoot "source"
$BuildRoot = Join-Path $WorkRoot "build"
$ArtifactRoot = Join-Path $WorkRoot "artifacts"
$LogRoot = Join-Path $ArtifactRoot "logs"
$ClientOutput = Join-Path $ArtifactRoot "client"
New-Item -ItemType Directory -Path $SourceRoot, $LogRoot | Out-Null

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments, [string]$LogName)
    Write-Host "$Program $($Arguments -join ' ')"
    & $Program @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $LogRoot $LogName)
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE; see $LogName"
    }
}

$PreviousDxSdk = $env:DXSDK_DIR
$PreviousPath = $env:PATH
$CacheExe = $null
Start-Transcript -Path (Join-Path $LogRoot "build-transcript.log") | Out-Null
try {
    Get-Command cmake, tar, python | Format-Table Name, Source
    Invoke-Checked "cmake" @("--version") "cmake-version.log"

    # A short-lived authenticated endpoint supplies the owner's licensed SDK.
    # Never put the URL/token in command arguments, transcripts, or artifacts.
    $WwiseSdk = $env:WOT_WWISE_SDK_ROOT
    if ($env:WOT_WWISE_SDK_URL -and $env:WOT_WWISE_SDK_TOKEN) {
        $WwiseArchive = Join-Path $WorkRoot "wwise-sdk.tar.xz"
        $WwiseInput = Join-Path $WorkRoot "wwise-sdk"
        New-Item -ItemType Directory -Path $WwiseInput | Out-Null
        Write-Host "Downloading the licensed Wwise SDK input"
        @'
import os, sys, time, urllib.request
# A GitHub release-asset API URL returns JSON metadata unless the binary is
# requested explicitly.
request = urllib.request.Request(os.environ['WOT_WWISE_SDK_URL'], headers={
    'Authorization': 'Bearer ' + os.environ['WOT_WWISE_SDK_TOKEN'],
    'Accept': 'application/octet-stream'})
try:
    started = time.monotonic()
    received = 0
    with urllib.request.urlopen(request, timeout=30) as response, open(sys.argv[1], 'wb') as output:
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            output.write(chunk)
            received += len(chunk)
            if time.monotonic() - started > 120:
                raise TimeoutError('SDK transfer deadline exceeded')
        expected = response.headers.get('Content-Length')
        if expected is not None and received != int(expected):
            raise IOError('Incomplete SDK input')
    print('Licensed SDK input received: %d bytes' % received)
except Exception as error:
    # Do not serialize request objects or URLs containing private input data.
    print('Licensed SDK download failed: ' + type(error).__name__, file=sys.stderr)
    sys.exit(1)
'@ | & python - $WwiseArchive
        if ($LASTEXITCODE -ne 0) {
            throw "The licensed Wwise SDK download failed."
        }
        Write-Host "Inspecting the licensed Wwise SDK archive"
        @'
from pathlib import PurePosixPath
import sys, tarfile
with tarfile.open(sys.argv[1], 'r:xz') as archive:
    members = archive.getmembers()
    if not members:
        raise ValueError('The licensed SDK archive is empty')
    for member in members:
        path = PurePosixPath(member.name)
        if (not member.name.startswith('SDK/') or '..' in path.parts or
                '\\' in member.name or not (member.isfile() or member.isdir())):
            raise ValueError('Unexpected licensed SDK archive member')
    archive.extractall(sys.argv[2], members=members, filter='data')
print('Licensed SDK input extracted: %d files' % len(members))
'@ | & python - $WwiseArchive $WwiseInput
        if ($LASTEXITCODE -ne 0) {
            throw "The licensed Wwise SDK input cannot be extracted."
        }
        $WwiseSdk = Join-Path $WwiseInput "SDK"
    }
    if (-not $WwiseSdk -or -not (Test-Path -LiteralPath (Join-Path $WwiseSdk "include\AK\AkWwiseSDKVersion.h"))) {
        throw "Supply the licensed Wwise 2017.1.9 SDK through WOT_WWISE_SDK_ROOT or the private build input."
    }
    if ($WwiseSdk) {
        $WwiseProbe = Join-Path $WorkRoot "wwise-probe"
        Invoke-Checked "cmake" @("-S", (Join-Path $RepositoryRoot "native\standalone_client"),
            "-B", $WwiseProbe, "-G", "Visual Studio 17 2022", "-A", "Win32", "-T", "v142",
            "-DBUILD_TESTING=OFF", "-DWOT_WWISE_SDK_ROOT=$WwiseSdk") "wwise-probe-configure.log"
        Invoke-Checked "cmake" @("--build", $WwiseProbe, "--config", "Release",
            "--target", "probe_wwise_sdk", "--parallel", "4") "wwise-probe-compile.log"
        Invoke-Checked (Join-Path $WwiseProbe "Release\probe_wwise_sdk.exe") @() "wwise-probe-run.log"
    }

    $Archive = Join-Path $WorkRoot "bigworld.tar.gz"
    $ArchivePrefix = "BigWorld-Engine-14.4.1-$Revision"
    Write-Host "Downloading BigWorld source at $Revision"
    Invoke-WebRequest -Uri "https://codeload.github.com/v2v3v4/BigWorld-Engine-14.4.1/tar.gz/$Revision" -OutFile $Archive
    $Members = @(& tar -tzf $Archive)
    if ($LASTEXITCODE -ne 0 -or $Members.Count -eq 0) {
        throw "Cannot inspect the BigWorld source archive."
    }
    foreach ($Member in $Members) {
        if (($Member -ne $ArchivePrefix -and
                -not $Member.StartsWith("$ArchivePrefix/")) -or
                (($Member -split '/') -contains '..') -or $Member.Contains('\')) {
            throw "Unexpected source archive member: $Member"
        }
    }
    Invoke-Checked "tar" @("-xzf", $Archive, "-C", $SourceRoot,
        "--strip-components", "1") "source-extract.log"

    $EngineRoot = Join-Path $SourceRoot "programming\bigworld"
    foreach ($RelativePath in @("CMakeLists.txt", "client\winmain.cpp",
            "client\CMakeLists.txt", "lib\moo\CMakeLists.txt")) {
        if (-not (Test-Path -LiteralPath (Join-Path $EngineRoot $RelativePath))) {
            throw "Pinned source is missing $RelativePath"
        }
    }

    # Build the available AS3/D3D9 source SDK, rather than reusing its old /MT
    # binaries or relabelling it as the original client's different 4.6.34.
    $ScaleformArchive = Join-Path $WorkRoot "scaleform-sdk.tar.gz"
    $ScaleformRoot = Join-Path $WorkRoot "scaleform-sdk"
    $ScaleformPrefix = "scaleform_sdk-$ScaleformRevision"
    New-Item -ItemType Directory -Path $ScaleformRoot | Out-Null
    Write-Host "Downloading Scaleform SDK $ScaleformVersion source at $ScaleformRevision"
    Invoke-WebRequest -Uri "https://codeload.github.com/RetiredSDK/scaleform_sdk/tar.gz/$ScaleformRevision" -OutFile $ScaleformArchive
    $ScaleformMembers = @(& tar -tzf $ScaleformArchive)
    if ($LASTEXITCODE -ne 0 -or $ScaleformMembers.Count -eq 0) {
        throw "Cannot inspect the pinned Scaleform source archive."
    }
    foreach ($Member in $ScaleformMembers) {
        if (($Member -ne $ScaleformPrefix -and
                -not $Member.StartsWith("$ScaleformPrefix/")) -or
                (($Member -split '/') -contains '..') -or $Member.Contains('\')) {
            throw "Unexpected Scaleform source archive member: $Member"
        }
    }
    Invoke-Checked "tar" @("-xzf", $ScaleformArchive, "-C", $ScaleformRoot,
        "--strip-components", "1") "scaleform-source-extract.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_scaleform_sdk.py"),
        $EngineRoot, $ScaleformRoot) "scaleform-source-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_flash_scheduler.py"),
        $ScaleformRoot) "stock-flash-scheduler-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_flash_bitmap_fill.py"),
        $ScaleformRoot) "stock-flash-bitmap-fill-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_flash_vertical_autosize.py"),
        $ScaleformRoot) "stock-flash-vertical-autosize-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_flash_drawable_images.py"),
        $EngineRoot) "stock-flash-drawable-images-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_flash_display_object.py"),
        $EngineRoot) "stock-flash-display-object-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_flash_collections.py"),
        $EngineRoot) "stock-flash-collections-patch.log"
    # Edits only lib/scaleform/util.cpp, which patch_scaleform_sdk.py does not touch.
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_lobby_input.py"),
        $EngineRoot) "stock-lobby-input-patch.log"
    # Both adaptations use Scaleform source seams separate from the SDK patch.
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_flash_input_mode.py"),
        $EngineRoot) "stock-flash-input-mode-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_glyph_cache.py"),
        $EngineRoot) "stock-glyph-cache-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_gui_profiler.py"),
        $EngineRoot) "stock-gui-profiler-patch.log"
    # Installs the LangBar input-language owner and guards the SDK IME focus
    # members it activates; both seams are separate from the SDK patch.
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_ime_lifecycle.py"),
        $EngineRoot, $ScaleformRoot) "stock-ime-lifecycle-patch.log"

    $GlooxArchive = Join-Path $WorkRoot "gloox.tar.bz2"
    $GlooxRoot = Join-Path $WorkRoot "gloox"
    $GlooxPrefix = "gloox-$GlooxVersion"
    New-Item -ItemType Directory -Path $GlooxRoot | Out-Null
    Invoke-WebRequest -Uri "https://camaya.net/download/gloox-$GlooxVersion.tar.bz2" -OutFile $GlooxArchive
    $GlooxMembers = @(& tar -tjf $GlooxArchive)
    if ($LASTEXITCODE -ne 0 -or $GlooxMembers.Count -eq 0) {
        throw "Cannot inspect the pinned gloox source archive."
    }
    foreach ($Member in $GlooxMembers) {
        if (($Member -ne $GlooxPrefix -and
                -not $Member.StartsWith("$GlooxPrefix/")) -or
                (($Member -split '/') -contains '..') -or $Member.Contains('\')) {
            throw "Unexpected gloox source archive member: $Member"
        }
    }
    Invoke-Checked "tar" @("-xjf", $GlooxArchive, "-C", $GlooxRoot,
        "--strip-components", "1") "gloox-source-extract.log"

    $OfflineForkArchive = Join-Path $WorkRoot "offline-fork.tar.gz"
    $OfflineForkRoot = Join-Path $WorkRoot "offline-fork"
    $OfflineForkPrefix = "wot-0.9.22-offline-battles-$OfflineForkRevision"
    New-Item -ItemType Directory -Path $OfflineForkRoot | Out-Null
    Invoke-WebRequest -Uri "https://codeload.github.com/colorfulmeans/wot-0.9.22-offline-battles/tar.gz/$OfflineForkRevision" -OutFile $OfflineForkArchive
    $OfflineForkMembers = @(& tar -tzf $OfflineForkArchive)
    if ($LASTEXITCODE -ne 0 -or $OfflineForkMembers.Count -eq 0) {
        throw "Cannot inspect the pinned offline gameplay source archive."
    }
    foreach ($Member in $OfflineForkMembers) {
        if (($Member -ne $OfflineForkPrefix -and
                -not $Member.StartsWith("$OfflineForkPrefix/")) -or
                $Member.Contains("..") -or $Member.Contains('\')) {
            throw "Unexpected offline gameplay source archive member: $Member"
        }
    }
    Invoke-Checked "tar" @("-xzf", $OfflineForkArchive, "-C", $OfflineForkRoot,
        "--strip-components", "1", "$OfflineForkPrefix/native",
        "$OfflineForkPrefix/src", "$OfflineForkPrefix/LICENSE",
        "$OfflineForkPrefix/destructibles", "$OfflineForkPrefix/foliage",
        "$OfflineForkPrefix/navgraphs") "offline-fork-source-extract.log"
    @{ revision = $OfflineForkRevision } | ConvertTo-Json | Set-Content -LiteralPath `
        (Join-Path $OfflineForkRoot "fork-source.json") -Encoding utf8NoBOM

    # The unchanged recorder opens its complete release archive from disk.
    # Resource mounts alone cannot supply the bundled child-process runtime.
    $OfflineForkReleaseArchive = Join-Path $WorkRoot "offline-fork-release.zip"
    Invoke-WebRequest -Uri "https://github.com/colorfulmeans/wot-0.9.22-offline-battles/releases/download/v0.10.1/wot-0.9.22-offline-battles-0.10.1-Windows-x64.zip" -OutFile $OfflineForkReleaseArchive
    @'
import io
from pathlib import Path
import sys, zipfile
source, release = Path(sys.argv[1]), Path(sys.argv[2])
package = 'org.colorfulmeans.offline_lan_0922_0.10.1.wotmod'
client_member = '_internal/client/0.9.22.zip'
mod_member = 'mods/0.9.22.0.1/' + package
with zipfile.ZipFile(release) as outer:
    if outer.namelist().count(client_member) != 1:
        raise ValueError('The pinned release must contain one client archive')
    with zipfile.ZipFile(io.BytesIO(outer.read(client_member))) as client:
        if client.namelist().count(mod_member) != 1:
            raise ValueError('The pinned client must contain one matching gameplay archive')
        payload = client.read(mod_member)
(source / 'dist').mkdir()
(source / 'dist' / package).write_bytes(payload)
print('Staged the pinned complete gameplay archive for recorder preparation')
'@ | & python - $OfflineForkRoot $OfflineForkReleaseArchive
    if ($LASTEXITCODE -ne 0) {
        throw "The pinned offline recorder archive could not be prepared."
    }

    # The pinned source predates the current SDK's API deprecations and the
    # standard library's nodiscard annotations. Keep /WX for other diagnostics.
    $CompilerOptions = Join-Path $EngineRoot "build\cmake\BWCompilerAndLinkerOptions.cmake"
    $CompilerOptionsText = Get-Content -LiteralPath $CompilerOptions -Raw
    if ([regex]::Matches($CompilerOptionsText, '/WX').Count -ne 1) {
        throw "The pinned compiler warning policy changed."
    }
    $CompilerOptionsText.Replace('/WX', '/WX /wd4996 /wd4834') |
        Set-Content -LiteralPath $CompilerOptions -Encoding utf8
    # This is our application build, not a vendor diagnostic build. Keep
    # native crash reporting local instead of invoking BigWorld's uploader.
    $EngineConfig = Join-Path $EngineRoot "lib\cstdmf\config.hpp"
    $EngineConfigText = Get-Content -LiteralPath $EngineConfig -Raw
    $EngineConfigText -replace '(?m)^#define BUILT_BY_BIGWORLD[\t ]+1[\t ]*$', `
        '#define BUILT_BY_BIGWORLD 0' |
        Set-Content -LiteralPath $EngineConfig -Encoding utf8
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_python_numeric_parsing.py"),
        $EngineRoot) "stock-python-numeric-parsing-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_python_math_specials.py"),
        $EngineRoot) "stock-python-math-specials-patch.log"
    # BigWorld.*Watcher Python functions keep #1513's consumer-build contract.
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_python_watchers.py"),
        $EngineRoot) "stock-python-watchers-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_python_profile_hook.py"),
        $EngineRoot) "stock-python-profile-hook-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_memory_debug.py"),
        $EngineRoot) "stock-memory-debug-patch.log"
    # BW_GUARD, PROFILER_SCOPED and memory debug compile out as in #1513's consumer build.
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_consumer_instrumentation.py"),
        $EngineRoot) "stock-consumer-instrumentation-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_heap_allocator.py"),
        $EngineRoot) "stock-heap-allocator-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_shader_compiler.py"),
        $EngineRoot) "shader-compiler-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_resource_write_overlay.py"),
        $EngineRoot) "resource-write-overlay-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_resource_packages.py"),
        $EngineRoot) "resource-packages-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_zip_case_lookup.py"),
        $EngineRoot) "stock-zip-case-lookup-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_processed_resources.py"),
        $EngineRoot) "processed-resources-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_vertex_format_cache.py"),
        $EngineRoot) "stock-vertex-format-cache-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_vertices_registration.py"),
        $EngineRoot) "stock-vertices-registration-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_static_collision.py"),
        $EngineRoot) "static-collision-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_vehicle_fashions.py"),
        $EngineRoot) "vehicle-fashions-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_vehicle_resources.py"),
        $EngineRoot) "vehicle-resources-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_client_session.py"),
        $EngineRoot) "client-session-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_vehicle_filter.py"),
        $EngineRoot) "stock-vehicle-filter-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_entity_filter.py"),
        $EngineRoot) "stock-entity-filter-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_avatar_filter_vectors.py"),
        $EngineRoot) "stock-avatar-filter-vectors-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_entity_picker.py"),
        $EngineRoot) "stock-entity-picker-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_vehicle_reflections.py"),
        $EngineRoot) "vehicle-reflections-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_vehicle_compound_rendering.py"),
        $EngineRoot) "vehicle-compound-rendering-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_audio.py"),
        $EngineRoot) "stock-audio-patch.log"
    # Keeps the simulation worker hidden; replaces its draw phase after audio.
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_worker_runtime.py"),
        $EngineRoot) "stock-worker-runtime-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_vehicle_visuals.py"),
        $EngineRoot) "stock-vehicle-visuals-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_py_splodge.py"),
        $EngineRoot) "stock-py-splodge-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_texture_streaming.py"),
        $EngineRoot) "stock-texture-streaming-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_script_callback_metadata.py"),
        $EngineRoot) "stock-script-callback-metadata-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_inactive_profiler.py"),
        $EngineRoot) "stock-inactive-profiler-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_frame_behind_guard.py"),
        $EngineRoot) "stock-frame-behind-guard-patch.log"
    # Ticks BigWorld.PyBallisticsSimulator tracer owners once per world frame.
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_ballistics.py"),
        $EngineRoot) "stock-ballistics-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_effect_owners.py"),
        $EngineRoot) "stock-effect-owners-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_startup_services.py"),
        $EngineRoot) "stock-startup-services-patch.log"
    # App frame pacing for BigWorld.wg_setSpecialFPSMode/wg_setReducedFpsMode.
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_runtime_services.py"),
        $EngineRoot) "stock-runtime-services-patch.log"
    # DebugApp frame sampling for BigWorld.getFPS.
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_debug_queries.py"),
        $EngineRoot) "stock-debug-queries-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_console_lifecycle.py"),
        $EngineRoot) "stock-console-lifecycle-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_windows_notifier.py"),
        $EngineRoot) "stock-windows-notifier-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_control_mode_rendering.py"),
        $EngineRoot) "stock-control-mode-rendering-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_resource_content.py"),
        $EngineRoot) "stock-resource-content-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_scaleform_resources.py"),
        $EngineRoot) "stock-scaleform-resources-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_scaleform_localization.py"),
        $EngineRoot) "stock-scaleform-localization-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_script_paths.py"),
        $EngineRoot) "stock-script-paths-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_xmpp.py"),
        $EngineRoot) "stock-xmpp-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_pinger.py"),
        $EngineRoot) "stock-pinger-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_replay_controller.py"),
        $EngineRoot) "stock-replay-controller-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_space_data.py"),
        $EngineRoot) "stock-space-data-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_space_lifecycle.py"),
        $EngineRoot) "stock-space-lifecycle-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_space_sounds.py"),
        $EngineRoot) "stock-space-sounds-patch.log"
    # Edits the ClientSpace::clear body the two space patchers above changed.
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_geometry_retirement.py"),
        $EngineRoot) "stock-geometry-retirement-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_sound_zones.py"),
        $EngineRoot) "stock-sound-zones-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_display_modes.py"),
        $EngineRoot) "stock-display-modes-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_graphics_settings_consumers.py"),
        $EngineRoot) "stock-graphics-settings-consumers-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_trajectory_drawer.py"),
        $EngineRoot) "stock-trajectory-drawer-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_graphics_presets.py"),
        $EngineRoot) "stock-graphics-presets-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_graphics_settings.py"),
        $EngineRoot) "stock-graphics-settings-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_texture_quality.py"),
        $EngineRoot) "stock-texture-quality-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_graphics_detection.py"),
        $EngineRoot) "stock-graphics-detection-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_scene_settings.py"),
        $EngineRoot) "stock-scene-settings-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_tps.py"),
        $EngineRoot) "stock-tps-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_particle_clone.py"),
        $EngineRoot) "stock-particle-clone-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_particle_background.py"),
        $EngineRoot) "stock-particle-background-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_ground_effects.py"),
        $EngineRoot) "stock-ground-effects-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_space_control_points.py"),
        $EngineRoot) "stock-space-control-points-patch.log"
    # Declares the alpha-aware Water query beside the ground-effects one above.
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_battle_scene_queries.py"),
        $EngineRoot) "stock-battle-scene-queries-patch.log"
    # Requires the ground-effects Water query and Python teardown owner above.
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_cursor_camera.py"),
        $EngineRoot) "stock-cursor-camera-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_free_camera.py"),
        $EngineRoot) "stock-free-camera-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_damage_sticker_registration.py"),
        $EngineRoot) "stock-damage-sticker-registration-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_vehicle_stickers.py"),
        $EngineRoot) "stock-vehicle-stickers-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_time_of_day.py"),
        $EngineRoot) "stock-time-of-day-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_deferred_lighting.py"),
        $EngineRoot) "stock-deferred-lighting-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_preferences_path.py"),
        $EngineRoot) "stock-preferences-path-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_preferences_callbacks.py"),
        $EngineRoot) "stock-preferences-callbacks-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_dynamic_resolution.py"),
        $EngineRoot) "stock-dynamic-resolution-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_resource_access_logging.py"),
        $EngineRoot) "stock-resource-access-logging-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_gpu_profiler.py"),
        $EngineRoot) "stock-gpu-profiler-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_flash_restricted_viewport.py"),
        $EngineRoot) "stock-flash-restricted-viewport-patch.log"
    # MouseCursor.inFocus and GUIApp's background/edge presentation owners.
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_hangar_presentation.py"),
        $EngineRoot) "stock-hangar-presentation-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_mouse_targeting.py"),
        $EngineRoot) "stock-mouse-targeting-patch.log"
    # Starts the generated offline mod at the original guiModsInit seam.
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_offline_entry.py"),
        $EngineRoot) "stock-offline-entry-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_decals.py"),
        $EngineRoot) "stock-decals-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_screenshots.py"),
        $EngineRoot) "stock-screenshots-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_battleground_elements.py"),
        $EngineRoot) "stock-battleground-elements-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_particle_scale.py"),
        $EngineRoot) "stock-particle-scale-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_sprite_shimmer.py"),
        $EngineRoot) "stock-sprite-shimmer-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_deferred_buffers.py"),
        $EngineRoot) "stock-deferred-buffers-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_antialiasing.py"),
        $EngineRoot) "stock-antialiasing-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_vehicle_traces.py"),
        $EngineRoot) "stock-vehicle-traces-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_flock_manager.py"),
        $EngineRoot) "stock-flock-manager-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_boids_filter.py"),
        $EngineRoot) "stock-boids-filter-patch.log"

    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_space_settings.py"),
        $EngineRoot) "stock-space-settings-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_space_strings.py"),
        $EngineRoot) "stock-space-strings-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_space_assets.py"),
        $EngineRoot) "stock-space-assets-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_space_geometry.py"),
        $EngineRoot) "stock-space-geometry-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_space_terrain.py"),
        $EngineRoot) "stock-space-terrain-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_terrain_detail_lod.py"),
        $EngineRoot) "stock-terrain-detail-lod-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_terrain_vertices.py"),
        $EngineRoot) "stock-terrain-vertices-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_space_trees.py"),
        $EngineRoot) "stock-space-trees-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_space_models.py"),
        $EngineRoot) "stock-space-models-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_space_lights.py"),
        $EngineRoot) "stock-space-lights-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_effect_lights.py"),
        $EngineRoot) "stock-effect-lights-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_flash_bang.py"),
        $EngineRoot) "stock-flash-bang-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_space_water.py"),
        $EngineRoot) "stock-space-water-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_water_ripples.py"),
        $EngineRoot) "stock-water-ripples-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_space_flares.py"),
        $EngineRoot) "stock-space-flares-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_space_decals.py"),
        $EngineRoot) "stock-space-decals-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_dynamic_decals.py"),
        $EngineRoot) "stock-dynamic-decals-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_ground_decals.py"),
        $EngineRoot) "stock-ground-decals-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_vehicle_decals.py"),
        $EngineRoot) "stock-vehicle-decals-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_local_lighting.py"),
        $EngineRoot) "stock-local-lighting-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_compiled_space_mapping.py"),
        $EngineRoot) "stock-compiled-space-mapping-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_audio_space.py"),
        $EngineRoot) "stock-audio-space-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_dynamic_scene_ownership.py"),
        $EngineRoot) "stock-dynamic-scene-ownership-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_scene_profiler.py"),
        $EngineRoot) "stock-scene-profiler-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_occlusion_profiler.py"),
        $EngineRoot) "stock-occlusion-profiler-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_modelnode_identity.py"),
        $EngineRoot) "stock-modelnode-identity-patch.log"
    # Frame-cost changes run last, on the profiled sources they replace.
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_occlusion_poll.py"),
        $EngineRoot) "stock-occlusion-poll-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_effect_pass_for_commit.py"),
        $EngineRoot) "stock-effect-pass-for-commit-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_dynamic_octree.py"),
        $EngineRoot) "stock-dynamic-octree-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_render_thread.py"),
        $EngineRoot) "stock-render-thread-patch.log"
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "patch_stock_scaleform_render_thread.py"),
        $EngineRoot, $ScaleformRoot) "stock-scaleform-render-thread-patch.log"

    # Microsoft's D3DX package supplies the legacy headers/import libraries
    # alongside the runner's Windows SDK, without installing the old SDK.
    $D3dxArchive = Join-Path $WorkRoot "d3dx.nupkg"
    $D3dxRoot = Join-Path $WorkRoot "d3dx"
    Invoke-WebRequest -Uri "https://api.nuget.org/v3-flatcontainer/microsoft.dxsdk.d3dx/$D3dxVersion/microsoft.dxsdk.d3dx.$D3dxVersion.nupkg" -OutFile $D3dxArchive
    [IO.Compression.ZipFile]::ExtractToDirectory($D3dxArchive, $D3dxRoot)
    $D3dxNative = Join-Path $D3dxRoot "build\native"
    foreach ($RelativePath in @("include\d3dx9.h", "include\d3dx10.h",
            "release\lib\x86\d3dx9.lib", "release\bin\x86\D3DX9_43.dll")) {
        if (-not (Test-Path -LiteralPath (Join-Path $D3dxNative $RelativePath))) {
            throw "Pinned D3DX package is missing $RelativePath"
        }
    }
    $SdkRoot = Join-Path $WorkRoot "dxsdk"
    New-Item -ItemType Directory -Path (Join-Path $SdkRoot "Include"),
        (Join-Path $SdkRoot "Lib\x86") | Out-Null
    Copy-Item -Path (Join-Path $D3dxNative "include\*") `
        -Destination (Join-Path $SdkRoot "Include") -Recurse
    Copy-Item -Path (Join-Path $D3dxNative "release\lib\x86\*.lib") `
        -Destination (Join-Path $SdkRoot "Lib\x86")
    $env:DXSDK_DIR = "$SdkRoot\"

    # The Windows SDK dxguid.lib omits D3DX9 GUIDs. Follow the D3DX package
    # instructions, including IID_ID3DXEffectStateManager used by BigWorld.
    $GuidSource = Join-Path $EngineRoot "client\d3dx_guids.cpp"
    @'
#undef DEFINE_GUID
#include <initguid.h>
#include <d3dx9.h>
'@ | Set-Content -LiteralPath $GuidSource -Encoding utf8
    # Append after the upstream precompiled-header setup: this small source
    # must define its GUIDs without inheriting the client's precompiled header.
    Add-Content -LiteralPath (Join-Path $EngineRoot "client\CMakeLists.txt") `
        -Value "`nTARGET_SOURCES(bwclient PRIVATE d3dx_guids.cpp)`n" -Encoding utf8
    $StandaloneCmakePath = $StandaloneCmake.Replace('\', '/')
    Add-Content -LiteralPath (Join-Path $EngineRoot "client\CMakeLists.txt") `
        -Value "`nINCLUDE(`"$StandaloneCmakePath`")`n" -Encoding utf8

    @{
        upstream = "https://github.com/v2v3v4/BigWorld-Engine-14.4.1"
        revision = $Revision
        d3dx_package = "Microsoft.DXSDK.D3DX/$D3dxVersion"
        scaleform_upstream = "https://github.com/RetiredSDK/scaleform_sdk"
        scaleform_revision = $ScaleformRevision
        scaleform_source_version = $ScaleformVersion
        original_client_scaleform_version = "4.6.34"
        gloox_upstream = "https://camaya.net/download/gloox-$GlooxVersion.tar.bz2"
        gloox_version = $GlooxVersion
        gloox_tls = "Windows SChannel"
        offline_fork_upstream = "https://github.com/colorfulmeans/wot-0.9.22-offline-battles"
        offline_fork_release = "v0.10.1"
        offline_fork_revision = $OfflineForkRevision
        scaleform_audio = "GFx abstract SoundRenderer; optional FMOD backend disabled"
        generator = "Visual Studio 17 2022"
        toolset = "v142"
        platform = "Win32"
        configuration = "Hybrid"
        runtime_resources = "Public BigWorld SourceForge SVN r10"
        source_adaptation = @(
            "Define D3DX9 GUIDs for the modern Windows SDK",
            "Build pinned Scaleform 4.2.22 AS2/AS3 and D3D9 projects with the application /MD CRT",
            "Use the source SDK filter policy; the unavailable BW setRestrictFlashFilters extension is not emulated",
            "Keep GFx abstract audio support without introducing an unused FMOD runtime",
            "Suppress observed legacy API/nodiscard warnings C4996 and C4834; retain /WX",
            "Link the restored stock client adapters and startup diagnostics",
            "Load original processed effects through the restored native shader runtime"
            "Disable the upstream vendor crash uploader for this independent build"
            "Enter the pinned offline Account at guiModsInit without retail login or WGC"
            "Offline optional services: no credential or chat-cache IO, no lobby chat or voice, no-device vibration"
        )
    } | ConvertTo-Json | Set-Content -LiteralPath `
        (Join-Path $ArtifactRoot "build-inputs.json") -Encoding utf8

    $ScaleformCmakePath = $ScaleformCmake.Replace('\', '/')
    Invoke-Checked "cmake" @("-S", $EngineRoot, "-B", $BuildRoot,
        "-G", "Visual Studio 17 2022", "-A", "Win32", "-T", "v142",
        "-DBW_CMAKE_TARGET=client", "-DBW_UNIT_TESTS_ENABLED=OFF",
        "-DWOT_SCALEFORM_SOURCE_CMAKE=$ScaleformCmakePath",
        "-DWOT_SCALEFORM_SDK_ROOT=$ScaleformRoot",
        "-DWOT_SCALEFORM_D3DX_ROOT=$SdkRoot",
        "-DWOT_SCALEFORM_FMOD_ADDON=OFF",
        "-DWOT_WWISE_SDK_ROOT=$WwiseSdk",
        "-DWOT_GLOOX_SOURCE_ROOT=$GlooxRoot",
        "-DWOT_OFFLINE_FORK_ROOT=$OfflineForkRoot",
        "-DPython3_EXECUTABLE=$((Get-Command python).Source)",
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5") "configure.log"
    if ($env:SCCACHE_PATH) {
        $CacheExe = (Get-Command $env:SCCACHE_PATH -ErrorAction Stop).Source
        # Use sccache's reviewed MSBuild integration, not CMake's compiler
        # launcher (which is implemented only by Ninja/Make generators).
        # The renamed executable resolves the real v142 compiler through PATH.
        $CompilerConfigs = @(Get-ChildItem -Path (Join-Path $BuildRoot "CMakeFiles") `
            -Filter "CMakeCXXCompiler.cmake" -Recurse)
        if ($CompilerConfigs.Count -ne 1) {
            throw "Cannot identify the configured C++ compiler for the cache."
        }
        $CompilerMatch = [regex]::Match(
            (Get-Content -LiteralPath $CompilerConfigs[0].FullName -Raw),
            '(?m)^set\(CMAKE_CXX_COMPILER "([^"]+)"\)')
        if (-not $CompilerMatch.Success -or
                -not (Test-Path -LiteralPath $CompilerMatch.Groups[1].Value)) {
            throw "The configured C++ compiler path is unavailable."
        }
        $env:PATH = "$(Split-Path -Parent $CompilerMatch.Groups[1].Value);$PreviousPath"
        $CacheCompilerRoot = Join-Path $WorkRoot "cache-compiler"
        New-Item -ItemType Directory -Path $CacheCompilerRoot | Out-Null
        $CacheCompiler = Join-Path $CacheCompilerRoot "cl.exe"
        Copy-Item -LiteralPath $CacheExe -Destination $CacheCompiler
        Invoke-Checked "python" @((Join-Path $PSScriptRoot "configure_bigworld_build_cache.py"),
            $BuildRoot, $CacheCompiler) "compiler-cache-configure.log"
        Invoke-Checked $CacheExe @("--zero-stats") "compiler-cache-start.log"
    }
    Invoke-Checked "cmake" @("--build", $BuildRoot, "--config", "Hybrid",
        "--target", "probe_stock_audio_resources", "probe_resource_write_overlay",
        "--parallel", "4") "resource-probes-compile.log"
    $WriteProbe = Join-Path $BuildRoot "native-probes\Hybrid\probe_resource_write_overlay.exe"
    $WriteFixture = Join-Path $WorkRoot "resource-write-overlay-probe"
    Invoke-Checked "python" @((Join-Path $RepositoryRoot "native\standalone_client\tests\make_resource_write_overlay_fixture.py"),
        $WriteFixture) "resource-write-overlay-fixture.log"
    @'
from pathlib import Path
import subprocess, sys
root = Path(sys.argv[2])
subprocess.run([sys.argv[1], str(root / 'overlay'), str(root / 'generated-resources.pkg'),
                str(root / 'source'), '-unattended'], check=True, timeout=60)
'@ | & python - $WriteProbe $WriteFixture 2>&1 |
        Tee-Object -FilePath (Join-Path $LogRoot "resource-write-overlay-probe-run.log")
    if ($LASTEXITCODE -ne 0) {
        throw "The private resource write integration probe failed."
    }
    $ResourceProbe = Join-Path $BuildRoot "native-probes\Hybrid\probe_stock_audio_resources.exe"
    $ResourceFixture = Join-Path $WorkRoot "audio-resources-probe.pkg"
    # Exercise our actual resource adapter, BigWorld ZIP locking and the Wwise
    # scheduler together. The SDK-only probe above uses its default disk hook.
    @'
import subprocess, sys, zipfile
with zipfile.ZipFile(sys.argv[2], 'w', compression=zipfile.ZIP_STORED) as archive:
    for name, multiplier, offset in [('first', 17, 3), ('second', 29, 11)]:
        archive.writestr('audioww/' + name + '.bin',
                         bytes((i * multiplier + offset) & 255 for i in range(4096)))
subprocess.run([sys.argv[1], sys.argv[2]], check=True, timeout=60)
'@ | & python - $ResourceProbe $ResourceFixture 2>&1 |
        Tee-Object -FilePath (Join-Path $LogRoot "audio-resources-probe-run.log")
    if ($LASTEXITCODE -ne 0) {
        throw "The mounted audio resource integration probe failed."
    }
    Invoke-Checked "cmake" @("--build", $BuildRoot, "--config", "Hybrid",
        "--target", "probe_stock_xmpp", "--parallel", "4") "xmpp-probe-compile.log"
    Invoke-Checked "python" @((Join-Path $RepositoryRoot "native\standalone_client\tests\run_stock_xmpp_loopback.py"),
        (Join-Path $BuildRoot "native-probes\Hybrid\probe_stock_xmpp.exe")) "xmpp-probe-run.log"
    Invoke-Checked "cmake" @("--build", $BuildRoot, "--config", "Hybrid",
        "--target", "probe_stock_pinger", "--parallel", "4") "pinger-probe-compile.log"
    Invoke-Checked (Join-Path $BuildRoot "native-probes\Hybrid\probe_stock_pinger.exe") @() "pinger-probe-run.log"
    Invoke-Checked "cmake" @("--build", $BuildRoot, "--config", "Hybrid",
        "--target", "probe_stock_wot_module", "--parallel", "4") "wot-module-probe-compile.log"
    Copy-Item -Path (Join-Path $D3dxNative "release\bin\x86\*.dll") `
        -Destination (Join-Path $BuildRoot "native-probes\Hybrid") -Force
    Invoke-Checked (Join-Path $BuildRoot "native-probes\Hybrid\probe_stock_wot_module.exe") @() "wot-module-probe-run.log"
    Invoke-Checked "cmake" @("--build", $BuildRoot, "--config", "Hybrid",
        "--target", "probe_stock_battleground_elements", "--parallel", "4") "battleground-elements-probe-compile.log"
    Invoke-Checked (Join-Path $BuildRoot "native-probes\Hybrid\probe_stock_battleground_elements.exe") @() "battleground-elements-probe-run.log"
    Invoke-Checked "cmake" @("--build", $BuildRoot, "--config", "Hybrid",
        "--target", "probe_scaleform_lifecycle", "--parallel", "4") "scaleform-lifecycle-probe-compile.log"
    Invoke-Checked (Join-Path $BuildRoot "native-probes\Hybrid\probe_scaleform_lifecycle.exe") @() "scaleform-lifecycle-probe-run.log"
    Invoke-Checked "cmake" @("--build", $BuildRoot, "--config", "Hybrid",
        "--target", "probe_stock_scaleform_amf3", "--parallel", "4") "scaleform-amf3-probe-compile.log"
    Invoke-Checked (Join-Path $BuildRoot "native-probes\Hybrid\probe_stock_scaleform_amf3.exe") @() "scaleform-amf3-probe-run.log"
    Invoke-Checked "cmake" @("--build", $BuildRoot, "--config", "Hybrid",
        "--target", "probe_stock_scaleform_bitmap", "--parallel", "4") "scaleform-bitmap-probe-compile.log"
    Invoke-Checked (Join-Path $BuildRoot "native-probes\Hybrid\probe_stock_scaleform_bitmap.exe") @() "scaleform-bitmap-probe-run.log"
    Invoke-Checked "cmake" @("--build", $BuildRoot, "--config", "Hybrid",
        "--target", "probe_stock_replay_file", "--parallel", "4") "replay-file-probe-compile.log"
    # CPython ships an independent, same-architecture OpenSSL DLL. The native
    # Win32 probe below uses Mercury's separately linked bundled block cipher.
    $ReplayCrypto = @'
import ctypes, sys
from pathlib import Path
root = Path(sys.executable).parent
for folder in (root, root / 'DLLs'):
    for path in sorted(folder.glob('libcrypto*.dll')):
        try:
            library = ctypes.CDLL(str(path))
            library.BF_set_key
            library.BF_ecb_encrypt
        except (OSError, AttributeError):
            continue
        print(path)
        sys.exit(0)
raise RuntimeError('The build Python must ship libcrypto with real Blowfish exports')
'@ | & python -
    if ($LASTEXITCODE -ne 0 -or -not $ReplayCrypto) {
        throw "Cannot load the build Python's independent replay fixture cipher."
    }
    Invoke-Checked "python" @((Join-Path $RepositoryRoot "native\standalone_client\tests\run_stock_replay_file_probe.py"),
        (Join-Path $BuildRoot "native-probes\Hybrid\probe_stock_replay_file.exe"),
        "--crypto-library", $ReplayCrypto) "replay-file-probe-run.log"
    Invoke-Checked "cmake" @("--build", $BuildRoot, "--config", "Hybrid",
        "--target", "bwclient", "--parallel", "4", "--",
        "/bl:$(Join-Path $LogRoot 'msbuild.binlog')") "compile.log"

    $UpstreamOutput = Join-Path $SourceRoot "game\bin\client\win32"
    if (-not (Test-Path -LiteralPath (Join-Path $UpstreamOutput "bwclient_h.exe"))) {
        throw "The build returned success without the expected bwclient_h.exe."
    }
    New-Item -ItemType Directory -Path $ClientOutput | Out-Null
    Copy-Item -Path (Join-Path $UpstreamOutput "*") -Destination $ClientOutput -Recurse
    Copy-Item -LiteralPath (Join-Path $StandaloneRoot "res") `
        -Destination (Join-Path $ClientOutput "res") -Recurse
    # The pinned offline package mounts ahead of the original resources, like
    # the retail mod path; prepare_offline_fork.py generated it at configure.
    $OfflineRuntime = Join-Path $BuildRoot "generated\offline-fork"
    $OfflineManifest = Get-Content -LiteralPath (Join-Path $OfflineRuntime "offline-res.json") `
        -Raw | ConvertFrom-Json
    if ($OfflineManifest.revision -ne $OfflineForkRevision) {
        throw "The generated offline runtime is not the pinned gameplay source."
    }
    Copy-Item -LiteralPath (Join-Path $OfflineRuntime "offline-res") `
        -Destination (Join-Path $ClientOutput "offline-res") -Recurse
    Copy-Item -LiteralPath (Join-Path $OfflineRuntime "offline-res.json") `
        -Destination $ClientOutput
    # The pinned gameplay reads its catalogs from ./mods/configs beside the EXE.
    Copy-Item -LiteralPath (Join-Path $OfflineRuntime "mods") `
        -Destination (Join-Path $ClientOutput "mods") -Recurse
    Invoke-Checked "python" @((Join-Path $StandaloneRoot "fetch_resources.py"),
        (Join-Path $ClientOutput "engine-res"), "--overlay-resources",
        (Join-Path $ClientOutput "res\resources.xml")) "runtime-resources.log"
    @'
<root>
    <Paths>
        <Path>res</Path>
        <Path>engine-res</Path>
    </Paths>
</root>
'@ | Set-Content -LiteralPath (Join-Path $ClientOutput "paths.xml") -Encoding utf8
    # The standalone package reads prepared resources directly. The upstream
    # developer executable otherwise waits for an asset-conversion server.
    @'
@echo off
cd /d "%~dp0"
bwclient_h.exe -noConversion %*
'@ | Set-Content -LiteralPath (Join-Path $ClientOutput "run-client.cmd") -Encoding ascii
    Copy-Item -Path (Join-Path $D3dxNative "release\bin\x86\*.dll") `
        -Destination $ClientOutput
    # Hybrid uses /MD. Ship the installed redistributable CRT beside the EXE
    # so a clean test machine does not need the build runner's Visual Studio.
    $VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $VsRoot = & $VsWhere -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $VsRoot) {
        throw "Cannot locate the installed Visual C++ redistributable."
    }
    $Runtime = Get-ChildItem -Path (Join-Path $VsRoot `
        'VC\Redist\MSVC\*\x86\Microsoft.VC*.CRT\vcruntime140.dll') |
        Sort-Object { [version]::new($_.VersionInfo.FileMajorPart,
            $_.VersionInfo.FileMinorPart, $_.VersionInfo.FileBuildPart,
            $_.VersionInfo.FilePrivatePart) } -Descending |
        Select-Object -First 1
    if (-not $Runtime -or -not (Test-Path (Join-Path $Runtime.DirectoryName 'msvcp140.dll'))) {
        throw "The x86 Visual C++ runtime is incomplete."
    }
    Copy-Item -Path (Join-Path $Runtime.DirectoryName '*.dll') -Destination $ClientOutput
    @"
Microsoft Visual C++ Redistributable, x86, $($Runtime.VersionInfo.FileVersion)
Application-local runtime files from the build machine's Visual Studio redist directory.
https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files
https://visualstudio.microsoft.com/license-terms/
"@ | Set-Content -LiteralPath (Join-Path $ClientOutput 'VC-RUNTIME-NOTICE.txt') -Encoding ascii
    Copy-Item -LiteralPath (Join-Path $SourceRoot "LICENSE") `
        -Destination (Join-Path $ClientOutput "BigWorld-LICENSE.txt")
    Copy-Item -LiteralPath (Join-Path $SourceRoot "THIRD-PARTY-NOTICES") `
        -Destination (Join-Path $ClientOutput "BigWorld-THIRD-PARTY-NOTICES.txt")
    Copy-Item -LiteralPath (Join-Path $D3dxRoot "LICENSE.txt") `
        -Destination (Join-Path $ClientOutput "D3DX-LICENSE.txt")
    Copy-Item -LiteralPath (Join-Path $D3dxRoot "NOTICE.md") `
        -Destination (Join-Path $ClientOutput "D3DX-NOTICE.md")
    Copy-Item -LiteralPath (Join-Path $GlooxRoot "COPYING") `
        -Destination (Join-Path $ClientOutput "gloox-COPYING.txt")
    Copy-Item -LiteralPath (Join-Path $GlooxRoot "LICENSE") `
        -Destination (Join-Path $ClientOutput "gloox-LICENSE.txt")
    Copy-Item -LiteralPath (Join-Path $OfflineForkRoot "LICENSE") `
        -Destination (Join-Path $ClientOutput "offline-fork-LICENSE.txt")
    Write-Host "Built standalone BigWorld engine binary: $ClientOutput"
    Write-Host "Includes the pinned offline gameplay and public engine resources; original WoT client resources are connected at preparation."
}
catch {
    Write-Host ($_ | Out-String)
    throw
}
finally {
    if ($CacheExe) {
        # Preserve hit/miss/bypass reasons even when a compiler or linker fails.
        # Only compiled public SDK objects enter sccache; no SDK inputs,
        # request URLs, tokens, PCH files, or build directories are uploaded.
        & $CacheExe --show-adv-stats 2>&1 |
            Tee-Object -FilePath (Join-Path $LogRoot "compiler-cache-stats.log")
        & $CacheExe --show-stats --stats-format json 2>&1 |
            Set-Content -LiteralPath (Join-Path $LogRoot "compiler-cache-stats.json") -Encoding utf8
    }
    if (Test-Path -LiteralPath (Join-Path $BuildRoot "CMakeCache.txt")) {
        Copy-Item -LiteralPath (Join-Path $BuildRoot "CMakeCache.txt") -Destination $LogRoot
    }
    $env:DXSDK_DIR = $PreviousDxSdk
    $env:PATH = $PreviousPath
    Stop-Transcript | Out-Null
}
