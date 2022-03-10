# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

$ENV:NODE_OPTIONS="--enable-source-maps"


function resolve {
    param ( [string] $name )
    $name = Resolve-Path $name -ErrorAction 0 -ErrorVariable _err
    if (-not($name)) { return $_err[0].TargetObject }
    $Error.clear()
    return $name
}


if( $ENV:VCPKG_ROOT ) {
  $SCRIPT:VCPKG_ROOT=(resolve $ENV:VCPKG_ROOT)
  $ENV:VCPKG_ROOT=$VCPKG_ROOT
} else {
  $SCRIPT:VCPKG_ROOT=(resolve "$HOME/.vcpkg")
  $ENV:VCPKG_ROOT=$VCPKG_ROOT
}

# setup the postscript file
# Generate 31 bits of randomness, to avoid clashing with concurrent executions.
$env:Z_VCPKG_POSTSCRIPT = resolve "${VCPKG_ROOT}/VCPKG_tmp_$(Get-Random -SetSeed $PID).ps1"

node $PSScriptRoot/ce @args 

# dot-source the postscript file to modify the environment
if ($env:Z_VCPKG_POSTSCRIPT -and (Test-Path $env:Z_VCPKG_POSTSCRIPT)) {
  # write-host (get-content -raw $env:Z_VCPKG_POSTSCRIPT)
  $content = get-content -raw $env:Z_VCPKG_POSTSCRIPT

  if( $content ) {
    iex $content
  }
  Remove-Item -Force $env:Z_VCPKG_POSTSCRIPT
  remove-item -ea 0 -force env:Z_VCPKG_POSTSCRIPT
}
