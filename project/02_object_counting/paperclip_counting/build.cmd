@echo off
setlocal
if not defined OPENCV_ROOT (
  echo OPENCV_ROOT is not set. Set it to your OpenCV build directory.
  exit /b 1
)
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo MSVC environment not found. Check build.cmd.
  exit /b 1
)
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /EHsc /std:c++17 /utf-8 /W4 /external:I"%OPENCV_ROOT%\include" /external:W0 /Zi /MDd src\main.cpp /Fe:build\paperclip_counting.exe /Fo:build\main.obj /Fd:build\compiler.pdb /link /DEBUG /PDB:build\paperclip_counting.pdb /LIBPATH:"%OPENCV_ROOT%\x64\vc16\lib" opencv_world4120d.lib
exit /b %errorlevel%


