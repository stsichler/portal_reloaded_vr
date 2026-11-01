@ECHO OFF
rem Removes the Portal Reloaded VR Mod. Lives in the Portal Reloaded game folder.
cd /d "%~dp0"

:choice
set /P c=Are you sure you want to uninstall Portal Reloaded VR Mod [Y/N]?
if /I "%c%" EQU "Y" goto :removemod
if /I "%c%" EQU "N" goto :donotremove
goto :choice

:removemod

echo Uninstalling...
del /Q ".\bin\d3d9.dll"
del /Q ".\bin\d3d9.pdb"
del /Q ".\bin\openvr_api.dll"
RD /Q /S ".\VR"
RD /Q /S ".\portal2_dlc3"
echo Portal Reloaded VR Mod is uninstalled.
pause
exit

:donotremove
echo Portal Reloaded VR Mod will not be uninstalled. Please run this program again if you wish to uninstall.
pause
exit
