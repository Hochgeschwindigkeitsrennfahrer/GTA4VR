call tools\EmbedPDB\EmbedPDB.exe bin\GTAIV.EFLC.FusionFix.asi
call tools\EmbedPDB\EmbedPDB.exe bin\d3d9.dll

powershell -NoProfile -ExecutionPolicy Bypass -File "sign.ps1" -SearchPaths ".\bin\GTAIV.EFLC.FusionFix.asi .\bin\d3d9.dll"

copy bin\GTAIV.EFLC.FusionFix.asi data\plugins\GTAIV.EFLC.FusionFix.asi
copy bin\d3d9.dll data\d3d9.dll
if not exist build\openxr-loader-win32\src\loader\Release\openxr_loader.dll (
    echo Missing x86 OpenXR loader. Build the Release openxr_loader target first.
    exit /b 1
)
copy /Y build\openxr-loader-win32\src\loader\Release\openxr_loader.dll data\plugins\openxr_loader.dll
copy /Y external\openxr-sdk\LICENSE data\plugins\openxr_loader.LICENSE.txt
if not exist tools\openvr-extension-helper\openvr_api.dll (
    echo Missing x86 plugins\openvr_api.dll helper. Build tools\openvr-extension-helper\openvr_stub.cpp first.
    exit /b 1
)
copy /Y tools\openvr-extension-helper\openvr_api.dll data\plugins\openvr_api.dll

powershell -NoProfile -ExecutionPolicy Bypass -File tools\prepare-vr-package.ps1
if ERRORLEVEL 1 exit /b 1

call buildimg.bat
call buildwtd.bat
call buildshaders.bat
call buildgxt.bat

if exist GTA4VR.zip del GTA4VR.zip
7z a "GTA4VR.zip" ".\data\*" ^
-x!update\GTAIV.EFLC.FusionFix\GTAIV.EFLC.FusionFix ^
-x!update\GTAIV.EFLC.FusionFix\GTAIV.FusionFix ^
-x!update\GTAIV.EFLC.FusionFix\TLAD.FusionFix ^
-x!update\GTAIV.EFLC.FusionFix\TBOGT.FusionFix ^
-x!update\GTAIV.EFLC.FusionFix\FusionTrees ^
-x!update\GTAIV.EFLC.FusionFix\FusionLights ^
-x!update\GTAIV.EFLC.FusionFix\cdimagemake.ims ^
-xr!*\.gitkeep
if ERRORLEVEL 1 exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify-vr-package.ps1
if ERRORLEVEL 1 exit /b 1

copy /Y bin\Release\GTAIV.EFLC.FusionFixInstaller.exe GTAIV.EFLC.FusionFixWebInstaller.exe
if ERRORLEVEL 1 exit /b 1
start /B /WAIT GTAIV.EFLC.FusionFixWebInstaller.exe GTA4VR.zip GTAIV.EFLC.FusionFixOfflineInstaller.exe
if ERRORLEVEL 1 exit /b 1
if not exist GTAIV.EFLC.FusionFixOfflineInstaller.exe exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -File "Sign.ps1" -SearchPaths ".\GTAIV.EFLC.FusionFixOfflineInstaller.exe .\GTAIV.EFLC.FusionFixWebInstaller.exe"

