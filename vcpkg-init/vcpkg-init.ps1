@(echo off) > $null
if #ftw NEQ '' goto :init
($true){ $Error.clear(); }

# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

# wrapper script for vcpkg
# this is intended to be dot-sourced and then you can use the vcpkg() function

# Workaround for $IsWindows not existing in Windows PowerShell
if (-Not (Test-Path variable:IsWindows)) {
  $IsWindows = $true
}

function download($url, $path) {
  $wc = New-Object net.webclient
  Write-Host "Downloading '$url' -> '$path'"
  $wc.DownloadFile($url, $path);
  $wc.Dispose();
  if( (get-item $path).Length -ne $wc.ResponseHeaders['Content-Length'] ) {
    throw "Download of '$url' failed.  Check your internet connection."
  }
  if (-Not $IsWindows) {
    chmod +x $path
  }

  return $path
}

# Determine VCPKG_ROOT
if (Test-Path "$PSScriptRoot/.vcpkg-root") {
  $env:VCPKG_ROOT = "$PSScriptRoot"
} elseif (-Not (Test-Path env:VCPKG_ROOT)) {
  $env:VCPKG_ROOT = "$HOME/.vcpkg"
}

$VCPKG = Join-Path $env:VCPKG_ROOT vcpkg
if ($IsWindows) {
  $VCPKG += '.exe'
}

$SCRIPT:VCPKG_VERSION_MARKER = Join-Path $env:VCPKG_ROOT vcpkg-version.txt
$SCRIPT:VCPKG_INIT_VERSION = 'latest'

function bootstrap-vcpkg {
  if (-Not ($VCPKG_INIT_VERSION -eq 'latest') `
    -And (Test-Path $VCPKG_VERSION_MARKER) `
    -And ((Get-Content -Path $VCPKG_VERSION_MARKER -Raw).Trim() -eq $VCPKG_INIT_VERSION)) {
        return $True
  }

  Write-Host "Installing vcpkg to $env:VCPKG_ROOT"
  New-Item -ItemType Directory -Force $env:VCPKG_ROOT | Out-Null

  if ($IsWindows) {
    download https://github.com/microsoft/vcpkg-tool/releases/latest/download/vcpkg.exe $VCPKG
  } elseif ($IsMacOS) {
    download https://github.com/microsoft/vcpkg-tool/releases/latest/download/vcpkg-macos $VCPKG
  } elseif (Test-Path '/etc/alpine-release') {
    download https://github.com/microsoft/vcpkg-tool/releases/latest/download/vcpkg-muslc $VCPKG
  } else {
    download https://github.com/microsoft/vcpkg-tool/releases/latest/download/vcpkg-glibc $VCPKG
  }

  & $VCPKG bootstrap-standalone
  if(-Not $?) {
    Write-Error "Bootstrap failed."
    return $False
  }

  Write-Host "Bootstrapped vcpkg: $env:VCPKG_ROOT"
  return $True
}

if(-Not (bootstrap-vcpkg)) {
  throw "Unable to install vcpkg."
}

# Export vcpkg to the current shell.
New-Module -name vcpkg -ArgumentList @($VCPKG) -ScriptBlock {
  param($VCPKG)
  function vcpkg-shell() {
    # setup the postscript file
    # Generate 31 bits of randomness, to avoid clashing with concurrent executions.
    $env:Z_VCPKG_POSTSCRIPT = Join-Path ([System.IO.Path]::GetTempPath()) "VCPKG_tmp_$(Get-Random -SetSeed $PID).ps1"
    & $VCPKG @args
    # dot-source the postscript file to modify the environment
    if (Test-Path $env:Z_VCPKG_POSTSCRIPT) {
      $postscr = Get-Content -Raw $env:Z_VCPKG_POSTSCRIPT
      if( $postscr ) {
        iex $postscr
      }

      Remove-Item -Force -ea 0 $env:Z_VCPKG_POSTSCRIPT
    }

    Remove-Item env:Z_VCPKG_POSTSCRIPT
  }
} | Out-Null

if ($args.Count -ne 0) {
  return vcpkg @args
}

return
<#
:init
:: If the first line of this script created a file named $null, delete it
IF EXIST $null DEL $null

:: Figure out where VCPKG_ROOT is
IF EXIST "%~dp0.vcpkg-root" (
  SET "VCPKG_ROOT=%~dp0"
)

IF "%VCPKG_ROOT:~-1%"=="\" (
  SET "VCPKG_ROOT=%VCPKG_ROOT:~0,-1%"
)

IF "%VCPKG_ROOT%"=="" (
  SET "VCPKG_ROOT=%USERPROFILE%\.vcpkg"
)

:: Call powershell which may or may not invoke bootstrap if there's a version mismatch
SET Z_POWERSHELL_EXE=
FOR %%i IN (pwsh.exe powershell.exe) DO (
  IF EXIST "%%~$PATH:i" (
    SET "Z_POWERSHELL_EXE=%%~$PATH:i"
    GOTO :gotpwsh
  )
)

:gotpwsh
"%Z_POWERSHELL_EXE%" -NoProfile -ExecutionPolicy Unrestricted -Command "iex (get-content \"%~dfp0\" -raw)#"
IF ERRORLEVEL 1 (
  :: leak VCPKG_ROOT
  SET Z_POWERSHELL_EXE=
  EXIT /B 1
)

SET Z_POWERSHELL_EXE=

:: Install the doskey
DOSKEY vcpkg-shell="%VCPKG_ROOT%\vcpkg-cmd.cmd" $*

:: If there were any arguments, also invoke vcpkg with them
IF "%1"=="" GOTO fin
CALL "%VCPKG_ROOT%\vcpkg-cmd.cmd" %*
:fin

EXIT /B
#>
