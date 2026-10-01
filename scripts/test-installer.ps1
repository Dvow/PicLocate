[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Installer,
    [string]$PreviousInstaller,
    [string]$PayloadDirectory,
    [string]$OffscreenPlugin,
    [string]$TestDirectory
)
$ErrorActionPreference = 'Stop'
$installerPath = (Resolve-Path -LiteralPath $Installer).Path
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PicLocate.LocalImageSearch_is1'
# This integration test exercises the real installer identity. Never replace a user's install.
if (Test-Path -LiteralPath $uninstallKey) { throw 'PicLocate is already installed for this user. Run this test in a separate Windows account or CI runner.' }
if (-not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell for this test.' }
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = if ($TestDirectory) { [IO.Path]::GetFullPath($TestDirectory) } else { Join-Path ([IO.Path]::GetTempPath()) "PicLocate installer test $testId" }
if (Test-Path -LiteralPath $testRoot) { throw "Test directory must be new: $testRoot" }
$appPath = Join-Path $testRoot 'Installed app'
$dataPath = Join-Path $testRoot 'Preserved library'
$fixturePath = Join-Path $testRoot 'Fixture images'
$group = "PicLocate installer test $testId"
$shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) "$group/PicLocate.lnk"
New-Item -ItemType Directory -Path $testRoot,$dataPath,$fixturePath | Out-Null
$oldEnvironment = @{}
foreach ($name in @('PATH','PICLOCATE_MODEL_DIR','QT_PLUGIN_PATH','QT_QPA_PLATFORM','QT_QPA_PLATFORM_PLUGIN_PATH','PICLOCATE_DISABLE_UPDATE_CHECK','QT_QPA_FONTDIR')) {
    $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name,'Process')
}
function Invoke-TestProcess([string]$Program, [string[]]$Arguments) {
    $logName = [IO.Path]::GetFileNameWithoutExtension($Program) + '-' + [Guid]::NewGuid().ToString('N')
    $process = Start-Process -FilePath $Program -ArgumentList $Arguments -PassThru -Wait -WindowStyle Hidden `
        -RedirectStandardOutput "$testRoot/$logName.stdout.log" -RedirectStandardError "$testRoot/$logName.stderr.log"
    if ($process.ExitCode -ne 0) { throw "$Program failed with exit code $($process.ExitCode)" }
}
function Assert-Test([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Assert-Payload {
    foreach ($relative in @('PicLocate.exe','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','onnxruntime.dll',
        'plugins/platforms/qwindows.dll','plugins/sqldrivers/qsqlite.dll','vcruntime140.dll',
        'models/tokenizer.json','models/text_model_quantized.onnx','models/vision_model_quantized.onnx',
        'models/vision_model_fp16.onnx','gpu/DirectML.dll','gpu/onnxruntime-dml.dll',
        'licenses/CLIP-MIT.txt','third-party-source/qtbase-everywhere-src-6.11.2.tar.xz')) {
        Assert-Test (Test-Path -LiteralPath (Join-Path $appPath $relative)) "Missing installed file: $relative"
    }
    Assert-Test (-not (Test-Path -LiteralPath (Join-Path $appPath '.piclocate-installer-stage'))) 'Staging marker was packaged.'
    if ($PayloadDirectory) {
        $payloadRoot = (Resolve-Path -LiteralPath $PayloadDirectory).Path.TrimEnd('\','/')
        foreach ($file in (Get-ChildItem -LiteralPath $payloadRoot -File -Recurse -Force)) {
            $relative = $file.FullName.Substring($payloadRoot.Length + 1)
            if ($relative -eq '.piclocate-installer-stage') { continue }
            $installed = Join-Path $appPath $relative
            Assert-Test (Test-Path -LiteralPath $installed) "Payload missing: $relative"
            Assert-Test ((Get-FileHash -LiteralPath $installed).Hash -eq (Get-FileHash -LiteralPath $file.FullName).Hash) "Payload differs: $relative"
        }
    }
}
try {
    # Installed binaries must find their own DLLs, plugins and models, without a developer SDK.
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:PICLOCATE_MODEL_DIR = $null
    $env:QT_PLUGIN_PATH = $null
    # Headless mode initializes the deployed Windows Qt plugin without showing a window.
    # The offscreen plugin belongs to the SDK/UI-test environment, not the release payload.
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $null
    $setupArguments = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/NOCLOSEAPPLICATIONS',
        "/DIR=`"$appPath`"", "/GROUP=`"$group`"")
    Add-Type -AssemblyName System.Drawing
    $bitmap = [Drawing.Bitmap]::new(64,64)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([Drawing.Color]::White)
    $graphics.FillEllipse([Drawing.Brushes]::Red,8,8,48,48)
    $bitmap.Save("$fixturePath/installer-fixture.png",[Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    if ($PreviousInstaller) {
        Invoke-TestProcess (Resolve-Path -LiteralPath $PreviousInstaller).Path ($setupArguments + "/LOG=`"$testRoot/previous-install.log`"")
        $previousExe = "$appPath/PicLocate.exe"
        Assert-Test (Test-Path -LiteralPath $previousExe) 'Previous installer did not install a supported executable.'
        Invoke-TestProcess $previousExe @('--headless','--cpu','--no-ocr','--data-dir',"`"$dataPath`"",
            '--index',"`"$fixturePath`"",'--report',"`"$testRoot/previous-index.json`"")
        Set-Content -LiteralPath "$dataPath/settings.ini" -Value "[UpgradeVerification]`npreserve=true"
        $previousLibraryHash = (Get-FileHash -LiteralPath "$dataPath/library.sqlite").Hash
        $previousSettingsHash = (Get-FileHash -LiteralPath "$dataPath/settings.ini").Hash
    }
    Invoke-TestProcess $installerPath ($setupArguments + "/LOG=`"$testRoot/install.log`"")
    if ($PreviousInstaller) {
        Assert-Test ((Get-FileHash -LiteralPath "$dataPath/library.sqlite").Hash -eq $previousLibraryHash) 'Upgrade changed the library.'
        Assert-Test ((Get-FileHash -LiteralPath "$dataPath/settings.ini").Hash -eq $previousSettingsHash) 'Upgrade changed settings.'
    }
    Assert-Payload
    $registration = Get-ItemProperty -LiteralPath $uninstallKey
    Assert-Test ($registration.InstallLocation.TrimEnd('\') -eq $appPath) 'Wrong uninstall registration path.'
    $version = (Get-Item -LiteralPath "$appPath/PicLocate.exe").VersionInfo.ProductVersion
    Assert-Test ($registration.DisplayVersion -eq $version) 'Installed version differs from executable version.'
    Assert-Test (Test-Path -LiteralPath $shortcut) 'Start menu shortcut missing.'
    $shell = New-Object -ComObject WScript.Shell
    Assert-Test ($shell.CreateShortcut($shortcut).TargetPath -eq "$appPath\PicLocate.exe") 'Shortcut points to the wrong executable.'
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell)

    Invoke-TestProcess "$appPath/PicLocate.exe" @('--headless','--cpu','--no-ocr','--data-dir',"`"$dataPath`"",
        '--index',"`"$fixturePath`"",'--query','"a red circle"','--report',"`"$testRoot/search.json`"")
    $search = Get-Content -LiteralPath "$testRoot/search.json" -Raw | ConvertFrom-Json
    Assert-Test ($search.library_images -eq 1 -and $search.index.failed -eq 0) 'Installed app could not index the fixture.'
    Assert-Test ($search.search.results[0].name -eq 'installer-fixture.png') 'Installed semantic search returned no fixture.'
    # Exercise automatic fallback on a machine with no optional GPU runtime.
    # Only this test's fresh installation is changed, and its file is restored.
    $gpuRuntime = Join-Path $appPath 'gpu/onnxruntime-dml.dll'
    $gpuBackup = Join-Path $appPath 'gpu/onnxruntime-dml.dll.test-backup'
    Move-Item -LiteralPath $gpuRuntime -Destination $gpuBackup
    try {
        Invoke-TestProcess "$appPath/PicLocate.exe" @('--headless','--no-ocr','--data-dir',"`"$testRoot/CPU fallback library`"",
            '--index',"`"$fixturePath`"",'--query','"red circle"','--report',"`"$testRoot/cpu-fallback.json`"")
        $fallback = Get-Content -LiteralPath "$testRoot/cpu-fallback.json" -Raw | ConvertFrom-Json
        Assert-Test ($fallback.library_images -eq 1 -and $fallback.index.failed -eq 0) 'Automatic CPU fallback lost an image.'
        Assert-Test ($fallback.index.profile.backend -eq 'CPU' -and $fallback.index.profile.gpu_fallback) 'Expected an automatic GPU-to-CPU fallback.'
    } finally {
        Move-Item -LiteralPath $gpuBackup -Destination $gpuRuntime
    }
    Set-Content -LiteralPath "$dataPath/settings.ini" -Value "[InstallerVerification]`npreserve=true"
    $libraryHash = (Get-FileHash -LiteralPath "$dataPath/library.sqlite").Hash
    $settingsHash = (Get-FileHash -LiteralPath "$dataPath/settings.ini").Hash
    Set-Content -LiteralPath "$appPath/user-created-file.txt" -Value 'Keep user-owned files.'
    Invoke-TestProcess $installerPath ($setupArguments + "/LOG=`"$testRoot/reinstall.log`"")
    Assert-Payload
    Assert-Test ((Get-FileHash -LiteralPath "$dataPath/library.sqlite").Hash -eq $libraryHash) 'Reinstall changed library data.'
    Assert-Test ((Get-FileHash -LiteralPath "$dataPath/settings.ini").Hash -eq $settingsHash) 'Reinstall changed settings.'
    $updateVerified = $false
    if ($OffscreenPlugin) {
        # Add a matching SDK plugin only to this disposable fixture, so restart opens no desktop window.
        $offscreenPath = Join-Path $appPath 'plugins/platforms/qoffscreen.dll'
        Copy-Item -LiteralPath (Resolve-Path -LiteralPath $OffscreenPlugin).Path -Destination $offscreenPath
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:PICLOCATE_DISABLE_UPDATE_CHECK = '1'
        $env:QT_QPA_FONTDIR = "$env:SystemRoot/Fonts"
        $exe = Join-Path $appPath 'PicLocate.exe'
        $stream = [IO.File]::Open($exe, [IO.FileMode]::Append)
        $stream.WriteByte(0); $stream.Dispose()
        $waitingHash = (Get-FileHash -LiteralPath $exe).Hash
        $parent = Start-Process -FilePath "$env:SystemRoot/System32/WindowsPowerShell/v1.0/powershell.exe" -ArgumentList @('-NoProfile','-Command','Start-Sleep -Seconds 120') -PassThru -WindowStyle Hidden
        $update = $null; $restarted = $null
        try {
            $update = Start-Process -FilePath $installerPath -ArgumentList ($setupArguments + "/UPDATEPID=$($parent.Id)" + "/UPDATEDATA=`"$dataPath`"" + "/LOG=`"$testRoot/update.log`"") -PassThru -WindowStyle Hidden
            Start-Sleep -Seconds 2
            Assert-Test (-not $update.HasExited) 'Updater did not wait for its parent.'
            Assert-Test ((Get-FileHash -LiteralPath $exe).Hash -eq $waitingHash) 'Updater replaced files before app exit.'
            Stop-Process -Id $parent.Id
            $parent.WaitForExit()
            Assert-Test ($update.WaitForExit(180000)) 'Update installer timed out.'
            Assert-Test ($update.ExitCode -eq 0) 'Update installer failed.'
            $deadline = [DateTime]::UtcNow.AddSeconds(30)
            while (-not $restarted -and [DateTime]::UtcNow -lt $deadline) {
                $restarted = Get-Process -Name PicLocate -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } | Select-Object -First 1
                if (-not $restarted) { Start-Sleep -Milliseconds 200 }
            }
            Assert-Test ([bool]$restarted) 'Updater did not restart the installed application.'
            $command = (Get-CimInstance Win32_Process -Filter "ProcessId=$($restarted.Id)").CommandLine
            Assert-Test ($command.Contains($dataPath)) 'Restart used the wrong library.'
            Start-Sleep -Seconds 2
            Assert-Test (-not $restarted.HasExited) 'Restarted application failed at startup.'
            Assert-Test ((Get-FileHash -LiteralPath $exe).Hash -ne $waitingHash) 'Updater did not replace the old payload.'
            $updateVerified = $true
        } finally {
            if (-not $parent.HasExited) { Stop-Process -Id $parent.Id }
            if ($restarted -and -not $restarted.HasExited) { Stop-Process -Id $restarted.Id; $restarted.WaitForExit() }
            if ($update -and -not $update.HasExited) { Stop-Process -Id $update.Id; $update.WaitForExit() }
            $env:QT_QPA_PLATFORM = 'windows'
            Remove-Item -LiteralPath $offscreenPath
        }
        Assert-Payload
        Assert-Test ((Get-FileHash -LiteralPath "$dataPath/settings.ini").Hash -eq $settingsHash) 'Update changed saved settings.'
    }
    Invoke-TestProcess "$appPath/unins000.exe" @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/LOG=`"$testRoot/uninstall.log`"")
    Assert-Test (-not (Test-Path -LiteralPath "$appPath/PicLocate.exe")) 'Uninstall left the executable.'
    Assert-Test (-not (Test-Path -LiteralPath $uninstallKey)) 'Uninstall registration was not removed.'
    Assert-Test (-not (Test-Path -LiteralPath $shortcut)) 'Uninstall left the shortcut.'
    Assert-Test (Test-Path -LiteralPath "$appPath/user-created-file.txt") 'Uninstall removed a user-owned file.'
    Assert-Test ((Get-FileHash -LiteralPath "$dataPath/library.sqlite").Hash -eq $libraryHash) 'Uninstall changed library data.'
    Assert-Test ((Get-FileHash -LiteralPath "$dataPath/settings.ini").Hash -eq $settingsHash) 'Uninstall changed settings.'
    $result = [ordered]@{ version=$version; installer_sha256=(Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant(); installed=$true; payload_verified=$true; indexed_and_searched=$true;
        upgrade=[bool]$PreviousInstaller; auto_update=$updateVerified; cpu_fallback=$true; reinstalled=$true; uninstalled=$true; library_preserved=$true; settings_preserved=$true; user_file_preserved=$true }
    $result | ConvertTo-Json | Set-Content -LiteralPath "$testRoot/result.json" -Encoding UTF8
    Write-Host "Installer checks passed. Logs: $testRoot"
} finally {
    foreach ($name in $oldEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name,$oldEnvironment[$name],'Process') }
    # Leave a failed installation and its diagnostics available for inspection, never force cleanup.
}
