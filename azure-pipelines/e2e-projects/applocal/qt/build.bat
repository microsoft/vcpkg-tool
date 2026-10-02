@echo off
setlocal
cd /d %~dp0

call :build_qt 5 "" qt5-release installed\bin installed\plugins
if errorlevel 1 exit /b 1
call :build_qt 5 d qt5-debug installed\debug\bin installed\debug\plugins
if errorlevel 1 exit /b 1
call :build_qt 6 "" qt6-release installed\bin installed\Qt6\plugins
if errorlevel 1 exit /b 1
call :build_qt 6 d qt6-debug installed\debug\bin installed\debug\Qt6\plugins
if errorlevel 1 exit /b 1
exit /b 0

:build_qt
set "qt_major=%~1"
set "debug_suffix=%~2"
set "app_dir=%~3"
set "bin_dir=%~4"
set "plugins_dir=%~5"

if not exist "%app_dir%" mkdir "%app_dir%"
if not exist "%bin_dir%" mkdir "%bin_dir%"
if not exist "%plugins_dir%\generic" mkdir "%plugins_dir%\generic"
if not exist "%plugins_dir%\imageformats" mkdir "%plugins_dir%\imageformats"
if not exist "%plugins_dir%\platforms" mkdir "%plugins_dir%\platforms"
if not exist "%plugins_dir%\platformthemes" mkdir "%plugins_dir%\platformthemes"
if not exist "%plugins_dir%\printsupport" mkdir "%plugins_dir%\printsupport"
if not exist "%plugins_dir%\sqldrivers" mkdir "%plugins_dir%\sqldrivers"
if not exist "%plugins_dir%\styles" mkdir "%plugins_dir%\styles"

cl /nologo /LD qtcore.cpp /Fe:%bin_dir%\Qt%qt_major%Core%debug_suffix%.dll /link /IMPLIB:%bin_dir%\Qt%qt_major%Core%debug_suffix%.lib
if errorlevel 1 exit /b 1
cl /nologo /LD qtnetwork.cpp %bin_dir%\Qt%qt_major%Core%debug_suffix%.lib /Fe:%bin_dir%\Qt%qt_major%Network%debug_suffix%.dll /link /IMPLIB:%bin_dir%\Qt%qt_major%Network%debug_suffix%.lib
if errorlevel 1 exit /b 1
cl /nologo /LD qtsql.cpp %bin_dir%\Qt%qt_major%Core%debug_suffix%.lib /Fe:%bin_dir%\Qt%qt_major%Sql%debug_suffix%.dll /link /IMPLIB:%bin_dir%\Qt%qt_major%Sql%debug_suffix%.lib
if errorlevel 1 exit /b 1
cl /nologo /LD qtprintsupport.cpp %bin_dir%\Qt%qt_major%Core%debug_suffix%.lib /Fe:%bin_dir%\Qt%qt_major%PrintSupport%debug_suffix%.dll /link /IMPLIB:%bin_dir%\Qt%qt_major%PrintSupport%debug_suffix%.lib
if errorlevel 1 exit /b 1
cl /nologo /LD qtgui.cpp %bin_dir%\Qt%qt_major%Core%debug_suffix%.lib /Fe:%bin_dir%\Qt%qt_major%Gui%debug_suffix%.dll /link /IMPLIB:%bin_dir%\Qt%qt_major%Gui%debug_suffix%.lib
if errorlevel 1 exit /b 1
cl /nologo /LD qtwidgets.cpp %bin_dir%\Qt%qt_major%Gui%debug_suffix%.lib /Fe:%bin_dir%\Qt%qt_major%Widgets%debug_suffix%.dll /link /IMPLIB:%bin_dir%\Qt%qt_major%Widgets%debug_suffix%.lib
if errorlevel 1 exit /b 1
cl /nologo /EHsc main.cpp %bin_dir%\Qt%qt_major%Gui%debug_suffix%.lib %bin_dir%\Qt%qt_major%Network%debug_suffix%.lib %bin_dir%\Qt%qt_major%PrintSupport%debug_suffix%.lib %bin_dir%\Qt%qt_major%Sql%debug_suffix%.lib %bin_dir%\Qt%qt_major%Widgets%debug_suffix%.lib /Fe:%app_dir%\main.exe
if errorlevel 1 exit /b 1
cl /nologo /LD qtplugin.cpp /Fe:%plugins_dir%\qtplugin.dll
if errorlevel 1 exit /b 1
call :copy_plugin "%plugins_dir%\qtplugin.dll" "%bin_dir%\libssl-unused.dll"
if errorlevel 1 exit /b 1

call :copy_plugin "%plugins_dir%\qtplugin.dll" "%plugins_dir%\imageformats\qjpeg%debug_suffix%.dll"
if errorlevel 1 exit /b 1
call :copy_plugin "%plugins_dir%\qtplugin.dll" "%plugins_dir%\platforms\qwindows%debug_suffix%.dll"
if errorlevel 1 exit /b 1
call :copy_plugin "%plugins_dir%\qtplugin.dll" "%plugins_dir%\generic\qgeneric%debug_suffix%.dll"
if errorlevel 1 exit /b 1
call :copy_plugin "%plugins_dir%\qtplugin.dll" "%plugins_dir%\platformthemes\qplatformtheme%debug_suffix%.dll"
if errorlevel 1 exit /b 1
call :copy_plugin "%plugins_dir%\qtplugin.dll" "%plugins_dir%\printsupport\qprint%debug_suffix%.dll"
if errorlevel 1 exit /b 1
call :copy_plugin "%plugins_dir%\qtplugin.dll" "%plugins_dir%\sqldrivers\qsqlite%debug_suffix%.dll"
if errorlevel 1 exit /b 1
call :copy_plugin "%plugins_dir%\qtplugin.dll" "%plugins_dir%\styles\qstyle%debug_suffix%.dll"
if errorlevel 1 exit /b 1

if "%qt_major%"=="5" (
    if not exist "%plugins_dir%\bearer" mkdir "%plugins_dir%\bearer"
    call :copy_plugin "%plugins_dir%\qtplugin.dll" "%plugins_dir%\bearer\qbearer%debug_suffix%.dll"
) else (
    if not exist "%plugins_dir%\networkinformation" mkdir "%plugins_dir%\networkinformation"
    if not exist "%plugins_dir%\tls" mkdir "%plugins_dir%\tls"
    call :copy_plugin "%plugins_dir%\qtplugin.dll" "%plugins_dir%\networkinformation\qnetworkinformation%debug_suffix%.dll"
    if errorlevel 1 exit /b 1
    call :copy_plugin "%plugins_dir%\qtplugin.dll" "%plugins_dir%\tls\qtls%debug_suffix%.dll"
)
if errorlevel 1 exit /b 1
exit /b 0

:copy_plugin
copy /Y "%~1" "%~2" >nul
exit /b %errorlevel%
