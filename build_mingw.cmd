@echo off
setlocal

rem Enable CUDA and CUDNN
set CUDNN=true
set CUDA=true
set DX12=false
set OPENCL=false
set MKL=false
set DNNL=false
set OPENBLAS=false
set EIGEN=false
set TEST=false
set CUTLASS=true

rem Set E-drive CUDA and cuDNN paths
set CUDA_PATH=E:\Program Files\NVIDIA GPU ComputingToolkit\CUDA\v12.9
set CUDNN_PATH=E:\Program Files\NVIDIA\CUDNN\v9.23

rem Set up PATH to prioritize MSYS2 MinGW64 and CUDA
set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%CUDA_PATH%\bin;%PATH%

rem Set compiler variables for Meson to detect GCC
set CC=gcc
set CXX=g++
set CC_LD=bfd
set CXX_LD=bfd

echo Deleting build directory:
rd /s /q build

rem Map CUDA and CUDNN paths
set CUDNN_LIB_PATH=%CUDA_PATH%\lib\x64,%CUDNN_PATH%\lib\12.9\x64
set CUDNN_INCLUDE_PATH=%CUDA_PATH%\include,%CUDNN_PATH%\include\12.9
set OPENCL_LIB_PATH=%CUDA_PATH%\lib\x64
set OPENCL_INCLUDE_PATH=%CUDA_PATH%\include

echo Running Meson setup with MinGW...
meson setup build --backend ninja --buildtype release -Ddx=%DX12% -Dcudnn=%CUDNN% -Dplain_cuda=%CUDA% ^
-Dopencl=%OPENCL% -Dblas=false -Dmkl=%MKL% -Dopenblas=%OPENBLAS% -Ddnnl=%DNNL% -Dgtest=%TEST% ^
-Dcudnn_include="%CUDNN_INCLUDE_PATH%" -Dcudnn_libdirs="%CUDNN_LIB_PATH%" ^
-Dopencl_libdirs="%OPENCL_LIB_PATH%" -Dopencl_include="%OPENCL_INCLUDE_PATH%" ^
-Dcutlass="%CUTLASS%" ^
-Ddefault_library=static

if errorlevel 1 exit /b

cd build
echo Starting Ninja compile...
ninja -v
