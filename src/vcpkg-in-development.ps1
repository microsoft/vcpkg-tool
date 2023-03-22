# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

$ENV:NODE_OPTIONS="--enable-source-maps"

# setup the postscript file
# Generate 31 bits of randomness, to avoid clashing with concurrent executions.
$env:Z_VCPKG_POSTSCRIPT = Join-Path ([System.IO.Path]::GetTempPath()) "VCPKG_tmp_$(Get-Random -SetSeed $PID).ps1"

[string]$VCPKG = "$PSScriptRoot/vcpkg"
# The variable:IsWindows test is a workaround for $IsWindows not existing in Windows PowerShell
if (-Not (Test-Path variable:IsWindows) -Or $IsWindows) {
  $VCPKG += ".exe"
}

& $VCPKG @args

# dot-source the postscript file to modify the environment
if (Test-Path $env:Z_VCPKG_POSTSCRIPT) {
  $postscr = Get-Content -Raw $env:Z_VCPKG_POSTSCRIPT
  if( $postscr ) {
    iex $postscr
  }

  Remove-Item -Force -ea 0 $env:Z_VCPKG_POSTSCRIPT,env:Z_VCPKG_POSTSCRIPT
}
