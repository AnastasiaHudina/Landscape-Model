@echo off
set FXC="C:\Program Files (x86)\Windows Kits\10\bin\10.0.17763.0\x64\fxc.exe"

%FXC% /T vs_5_0 /E main /Fo FullscreenQuad_VS.cso FullscreenQuad_VS.hlsl
%FXC% /T vs_5_0 /E main /Fo HDRToCubeMap_VS.cso HDRToCubeMap_VS.hlsl
%FXC% /T ps_5_0 /E main /Fo HDRToCubeMap_PS.cso HDRToCubeMap_PS.hlsl
%FXC% /T ps_5_0 /E main /Fo IrradianceMap_PS.cso IrradianceMap_PS.hlsl
%FXC% /T ps_5_0 /E main /Fo PrefilterEnvMap_PS.cso PrefilterEnvMap_PS.hlsl
%FXC% /T ps_5_0 /E main /Fo BRDFLUT_PS.cso BRDFLUT_PS.hlsl

echo All shaders compiled to .cso
pause