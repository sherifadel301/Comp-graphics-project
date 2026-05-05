@echo off
set OPENGL_SRC=C:\Users\shref\OneDrive\Desktop\OpenGL\src
set OPENGL_INCLUDE=C:\Users\shref\OneDrive\Desktop\OpenGL\include
set OPENGL_LIBS=C:\Users\shref\OneDrive\Desktop\OpenGL\libs
set OUTDIR=C:\Users\shref\AppData\Local\Temp\cppbuild

if not exist "%OUTDIR%" mkdir "%OUTDIR%"

set "DIR=%~1"
if "%DIR:~-1%"=="\" set "DIR=%DIR:~0,-1%"
set "DIR=%DIR%\"

set "FILE=%~2"
set "FILENOEXT=%FILE:.cpp=%"
set "OUT=%OUTDIR%\%FILENOEXT%.exe"

findstr /m "glut.h" "%DIR%%FILE%" >nul 2>&1
if %errorlevel%==0 (
    echo [Building with FreeGLUT...]
    g++ "%DIR%%FILE%" -o "%OUT%" -lfreeglut -lopengl32 -lglu32
    if %errorlevel%==0 ( "%OUT%" ) else ( echo Build failed. )
    goto end
)

findstr /m "glfw3.h" "%DIR%%FILE%" >nul 2>&1
if %errorlevel%==0 (
    echo [Building with GLAD + GLFW...]
    g++ "%DIR%%FILE%" "%OPENGL_SRC%\glad.c" -o "%OUT%" -I"%OPENGL_INCLUDE%" -L"%OPENGL_LIBS%" -lglfw3 -lopengl32 -lgdi32
    if %errorlevel%==0 ( "%OUT%" ) else ( echo Build failed. )
    goto end
)

echo [Building normal C++...]
g++ "%DIR%%FILE%" -o "%OUT%"
if %errorlevel%==0 ( "%OUT%" ) else ( echo Build failed. )

:end