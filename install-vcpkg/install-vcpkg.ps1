@(echo off) > $null
if #ftw NEQ '' goto :init
($true){ $Error.clear(); }

# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

# wrapper script for vcpkg
# this is intended to be dot-sourced and then you can use the vcpkg() function

# unpack arguments if they came from CMD
$hash=@{};
get-item env:argz* |% { $hash[$_.name] = $_.value }
if ($hash.count -gt 0) {
  $args=for ($i=0; $i -lt $hash.count;$i++) { $hash["ARGZ[$i]"] }
}
# force the array to be an arraylist since we like to mutate it.
$args=[System.Collections.ArrayList][System.Array]$args

# GLOBALS
$VCPKG_START_TIME=get-date

function resolve([string]$name) {
  $name = Resolve-Path $name -ErrorAction 0 -ErrorVariable _err
  if (-not($name)) { return $_err[0].TargetObject }
  $Error.clear()
  return $name
}

$SCRIPT:DEBUG=( $args.indexOf('--debug') -gt -1 )

function vcpkg-debug() {
  $t = [int32]((get-date).Subtract(($VCPKG_START_TIME)).ticks/10000)
  if($SCRIPT:DEBUG) {
    write-host -fore green "[$t msec] " -nonewline
    write-host -fore gray $args
  }
  write-output "[$t msec] $args" >> $VCPKG_ROOT/log.txt
}

function download($url, $path) {
  $wc = New-Object net.webclient

  if( test-path -ea 0 $path) {
    # check to see if the size is a match before downloading
    $s = $wc.OpenRead($url)
    $len = $wc.ResponseHeaders['Content-Length']
    $s.Dispose()
    if( (get-item $path).Length -eq $len ){
      $wc.Dispose();
      vcpkg-debug "skipping download of '$url' - '$path' is ok."
      return $path;
    }
  }
  vcpkg-debug "Downloading '$url' -> '$path'"
  $wc.DownloadFile($url, $path);
  $wc.Dispose();
  if( (get-item $path).Length -ne $wc.ResponseHeaders['Content-Length'] ) {
    throw "Download of '$url' failed.  Check your internet connection."
  }
  vcpkg-debug "Completed Download of $url"
  return $path
}

# set the home path.
if( $ENV:VCPKG_ROOT ) {
  $SCRIPT:VCPKG_ROOT=(resolve $ENV:VCPKG_ROOT)
  $ENV:VCPKG_ROOT=$VCPKG_ROOT
} else {
  $SCRIPT:VCPKG_ROOT=(resolve "$HOME/.vcpkg")
  $ENV:VCPKG_ROOT=$VCPKG_ROOT
}

$VCPKG = "${VCPKG_ROOT}/vcpkg.exe"
$SCRIPT:VCPKG_SCRIPT = "${VCPKG_ROOT}/vcpkg.ps1"

$reset = $args.IndexOf('--reset-vcpkg') -gt -1
$remove = $args.IndexOf('--remove-vcpkg') -gt -1

if( $reset -or -$remove ) {
  $args.remove('--reset-vcpkg');
  $args.remove('--remove-vcpkg');

  if( $reset ) {
    write-host "Resetting vcpkg"
  }

  remove-item -recurse -force -ea 0 "$VCPKG_ROOT"
  $error.clear();

  if( $remove ) {
    write-host "Removing vcpkg"
    exit
  }
}

function bootstrap-vcpkg {
  if( test-path $VCPKG_SCRIPT ) {
    return $true
  }

  write-host "Installing vcpkg to $VCPKG_ROOT"
  if (-not (Test-Path $VCPKG_ROOT)) {
      mkdir $VCPKG_ROOT
  }

  download https://github.com/microsoft/vcpkg-tool/releases/latest/download/vcpkg.exe $VCPKG
  & $VCPKG z-bootstrap-readonly

  $PATH = $ENV:PATH
  $ENV:PATH="$VCPKG_ROOT;$PATH"

  vcpkg-debug "Bootstrapped vcpkg: ${VCPKG_ROOT}"

  if( -not ( test-path $VCPKG_SCRIPT )) {
    Write-Error "ERROR! Bootstrapping vcpkg failed."
    return $false
  }
  return $true
}

if( -not (bootstrap-vcpkg )) {
  write-error "Unable to install vcpkg."
  throw "Installation Unsuccessful."
}

# export vcpkg to the current shell.
$shh = New-Module -name vcpkg -ArgumentList @($VCPKG,$VCPKG_ROOT) -ScriptBlock {
  param($VCPKG,$VCPKG_ROOT)

  function resolve([string]$name) {
    $name = Resolve-Path $name -ErrorAction 0 -ErrorVariable _err
    if (-not($name)) { return $_err[0].TargetObject }
    $Error.clear()
    return $name
  }

  function vcpkg() {
    if( ($args.indexOf('--remove-vcpkg') -gt -1) -or ($args.indexOf('--reset-vcpkg') -gt -1)) {
      # we really want to do call the ps1 script to do this.
      if( test-path "${VCPKG_ROOT}/vcpkg.ps1" ) {
        & "${VCPKG_ROOT}/vcpkg.ps1" @args
      }
      return
    }

    if( -not (test-path $VCPKG )) {
      write-error "vcpkg is not installed."
      write-host -nonewline "You can reinstall vcpkg by running "
      write-host -fore green "iex (iwr -useb https://aka.ms/install-vcpkg.ps1)"
      return
    }

    # setup the postscript file
    # Generate 31 bits of randomness, to avoid clashing with concurrent executions.
    $env:Z_VCPKG_POSTSCRIPT = resolve "${VCPKG_ROOT}/VCPKG_tmp_$(Get-Random -SetSeed $PID).ps1"

    & $VCPKG @args

    # dot-source the postscript file to modify the environment
    if ($env:Z_VCPKG_POSTSCRIPT -and (Test-Path $env:Z_VCPKG_POSTSCRIPT)) {
      # write-host (get-content -raw $env:Z_VCPKG_POSTSCRIPT)
      $postscr = get-content -raw $env:Z_VCPKG_POSTSCRIPT
      if( $postscr ) {
        iex $postscr
      }
      Remove-Item -Force -ea 0 $env:Z_VCPKG_POSTSCRIPT,env:Z_VCPKG_POSTSCRIPT
    }
  }
}

return vcpkg @args
<#
:set
set ARGZ[%i%]=%1&set /a i+=1 & goto :eof

:unset
set %1=& goto :eof

:init
if exist $null erase $null

:: do anything we need to before calling into powershell
if exist $null erase $null

IF "%VCPKG_ROOT%"=="" SET VCPKG_ROOT=%USERPROFILE%\.vcpkg

:: we're running vcpkg from the home folder
set VCPKG_CMD="%VCPKG_ROOT%\vcpkg.cmd"
set VCPKG_EXE="%VCPKG_ROOT%\vcpkg.exe"

:: if we're being asked to reset the install, call bootstrap
if "%1" EQU "--reset-vcpkg" goto BOOTSTRAP

:: if we're being asked to remove the install, call bootstrap
if "%1" EQU "--remove-vcpkg" (
  set REMOVE_CE=TRUE
  doskey vcpkg=
  goto BOOTSTRAP
)

:: do we even have it installed?
if NOT exist "%VCPKG_CMD%" goto BOOTSTRAP

:: if this is the actual installed vcpkg-ce, let's get to the invocation
if "%~dfp0" == "%VCPKG_CMD%" goto INVOKE

:: this is not the 'right' vcpkg cmd, let's forward this on to that one.
call %VCPKG_CMD% %*
set VCPKG_EXITCODE=%ERRORLEVEL%
goto :eof

:INVOKE
:: Generate 30 bits of randomness, to avoid clashing with concurrent executions.
SET /A Z_VCPKG_POSTSCRIPT=%RANDOM% * 32768 + %RANDOM%
SET Z_VCPKG_POSTSCRIPT=%VCPKG_ROOT%\VCPKG_tmp_%Z_VCPKG_POSTSCRIPT%.cmd

:: call the program
"%VCPKG_EXE%"" %*
set VCPKG_EXITCODE=%ERRORLEVEL%
doskey vcpkg="%VCPKG_CMD%" $*

:POSTSCRIPT
:: Call the post-invocation script if it is present, then delete it.
:: This allows the invocation to potentially modify the caller's environment (e.g. PATH).
IF NOT EXIST "%Z_VCPKG_POSTSCRIPT%" GOTO :fin
CALL "%Z_VCPKG_POSTSCRIPT%"
DEL "%Z_VCPKG_POSTSCRIPT%"

goto :fin

:BOOTSTRAP
:: add the cmdline args to the environment so powershell can use them
set /a i=0 & for %%a in (%*) do call :set %%a

set POWERSHELL_EXE=
for %%i in (pwsh.exe powershell.exe) do (
  if EXIST "%%~$PATH:i" set POWERSHELL_EXE=%%~$PATH:i & goto :gotpwsh
)
:gotpwsh

"%POWERSHELL_EXE%" -noprofile -executionpolicy unrestricted -command "iex (get-content %~dfp0 -raw)#" &&  set REMOVE_VCPKG=
set VCPKG_EXITCODE=%ERRORLEVEL%

:: clear out the arguments
@for /f "delims==" %%_ in ('set ^|  findstr -i argz') do call :unset %%_

:: if we're being asked to remove it,we're done.
if "%REMOVE_VCPKG%" EQU "TRUE" (
  goto :fin
)

:CREATEALIAS
doskey vcpkg="%VCPKG_ROOT%\vcpkg.cmd" $*

:fin
SET Z_VCPKG_POSTSCRIPT=
SET VCPKG_CMD=
set VCPKG_EXE=

EXIT /B %VCPKG_EXITCODE%
goto :eof
#>
