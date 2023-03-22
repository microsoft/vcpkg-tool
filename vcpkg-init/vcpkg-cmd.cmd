@echo off
:: Generate 30 bits of randomness, to avoid clashing with concurrent executions.
SET /A Z_VCPKG_POSTSCRIPT=%RANDOM% * 32768 + %RANDOM%
:: Set a temporary postscript path
SET Z_VCPKG_POSTSCRIPT=%TEMP%\VCPKG_tmp_%Z_VCPKG_POSTSCRIPT%.cmd
:: Actually run vcpkg and save its exit code
"%~dp0\vcpkg.exe" %*
SET Z_VCPKG_ERRORLEVEL=%ERRORLEVEL%
:: If vcpkg wanted to make any environment changes, make them
IF EXIST "%Z_VCPKG_POSTSCRIPT%" (
    CALL "%Z_VCPKG_POSTSCRIPT%"
    DEL "%Z_VCPKG_POSTSCRIPT%"
)

:: Cleanup
SET Z_VCPKG_POSTSCRIPT=
EXIT /B %Z_VCPKG_ERRORLEVEL%
