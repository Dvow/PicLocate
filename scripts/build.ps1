[CmdletBinding()]
param(
    [ValidateSet('x64','arm64')][string]$Architecture = 'x64',
    [string]$QtRoot,
    [string]$OrtRoot,
    [string]$DependencyCache,
    [string]$BuildDirectory,
    [string]$PackageDirectory,
    [string]$InstallerDirectory,
    [string]$Iscc,
    [string]$UpdateRepository,
    [switch]$WithoutModels,
    [switch]$SkipTests,
    [switch]$SkipInstaller,
    [switch]$Offline
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$native = if ($BuildDirectory) { [IO.Path]::GetFullPath($BuildDirectory) } elseif ($Architecture -eq 'arm64') { Join-Path $root '.build/windows-arm64' } else { Join-Path $root '.build/cpm-release' }
$package = if ($PackageDirectory) { [IO.Path]::GetFullPath($PackageDirectory) } elseif ($Architecture -eq 'arm64') { Join-Path $root 'dist/PicLocate-Windows-arm64' } else { Join-Path $root 'dist/PicLocate-Windows' }
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue) -or $env:VSCMD_ARG_TGT_ARCH -ne $Architecture) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio Build Tools with Desktop development with C++ first.' }
    $component = if ($Architecture -eq 'arm64') { 'Microsoft.VisualStudio.Component.VC.Tools.ARM64' } else { 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' }
    $vs = & $vswhere -latest -products '*' -requires $component -property installationPath
    if (-not $vs) { throw "No $Architecture MSVC toolchain was found. Install the matching Visual Studio C++ tools." }
    $devCmd = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
    # Import only the compiler environment produced by the installed Visual Studio tools.
    $compilerSetup = [Diagnostics.ProcessStartInfo]::new()
    $compilerSetup.FileName = $env:ComSpec
    $compilerSetup.Arguments = '/d /s /c ""' + $devCmd + '" -no_logo -arch=' + $Architecture + ' -host_arch=x64 >nul && set"'
    $compilerSetup.UseShellExecute = $false
    $compilerSetup.CreateNoWindow = $true
    $compilerSetup.RedirectStandardOutput = $true
    $compilerSetup.RedirectStandardError = $true
    $setupProcess = [Diagnostics.Process]::Start($compilerSetup)
    $environmentLines = $setupProcess.StandardOutput.ReadToEnd() -split "`r?`n"
    $setupError = $setupProcess.StandardError.ReadToEnd()
    $setupProcess.WaitForExit()
    if ($setupProcess.ExitCode -ne 0) { throw "Visual Studio environment setup failed: $setupError" }
    foreach ($line in $environmentLines) {
        $split = $line.IndexOf('=')
        if ($split -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0,$split), $line.Substring($split+1), 'Process') }
    }
}
foreach ($tool in @('cmake','ninja','cl.exe')) { if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Required build tool missing: $tool" } }
$configure = @('-S',$root,'-B',$native,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=ON')
if ($PSBoundParameters.ContainsKey('UpdateRepository')) { $configure += "-DPICLOCATE_UPDATE_REPOSITORY=$UpdateRepository" }
if ($Architecture -eq 'arm64' -and $env:PROCESSOR_ARCHITECTURE -ne 'ARM64' -and -not $SkipTests) {
    throw 'Cross-compiling ARM64 requires -SkipTests; execute its tests on a native ARM64 machine before release.'
}
$configure += if ($WithoutModels) { '-DPICLOCATE_BUNDLE_MODELS=OFF' } else { '-DPICLOCATE_BUNDLE_MODELS=ON' }
$configure += if ($Offline) { '-DFETCHCONTENT_FULLY_DISCONNECTED=ON' } else { '-DFETCHCONTENT_FULLY_DISCONNECTED=OFF' }
$configure += if ($SkipInstaller) { '-DPICLOCATE_BUILD_INSTALLER=OFF' } else { '-DPICLOCATE_BUILD_INSTALLER=ON' }
if ($InstallerDirectory) { $configure += "-DPICLOCATE_INSTALLER_OUTPUT_DIR=$([IO.Path]::GetFullPath($InstallerDirectory).Replace('\','/'))" }
if ($Iscc) { $configure += "-DPICLOCATE_ISCC=$((Resolve-Path -LiteralPath $Iscc).Path.Replace('\','/'))" }
if ($QtRoot) { $configure += "-DPICLOCATE_QT_ROOT=$((Resolve-Path -LiteralPath $QtRoot).Path.Replace('\','/'))" }
if ($OrtRoot) { $configure += "-DONNXRUNTIME_ROOT=$((Resolve-Path -LiteralPath $OrtRoot).Path.Replace('\','/'))" }
if ($DependencyCache) { $configure += "-DPICLOCATE_DEPENDENCY_CACHE=$([IO.Path]::GetFullPath($DependencyCache).Replace('\','/'))" }
Invoke-Checked 'cmake' $configure
Invoke-Checked 'cmake' @('--build',$native,'--parallel','4')
if (-not $SkipTests) {
    & ctest --test-dir $native --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        Get-ChildItem -LiteralPath $native -Filter '*-test-results.txt' | ForEach-Object { Get-Content -LiteralPath $_.FullName }
        throw 'Tests failed. Packaging stopped.'
    }
}
Invoke-Checked 'cmake' @('--install',$native,'--prefix',$package)
Write-Host "Ready: $package/PicLocate.exe"
if (-not $SkipInstaller) {
    Invoke-Checked 'cmake' @('--build',$native,'--target','setup','--parallel','4')
}
