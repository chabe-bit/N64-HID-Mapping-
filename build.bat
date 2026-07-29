@echo off

set CommonCompilerFlags=/nologo /Zi 
set CommonLinkerFlags=user32.lib gdi32.lib winmm.lib hid.lib setupapi.lib 

cl %CommonCompilerFlags% main.cpp /Femain.exe /link /PDB:main.pdb /incremental:no /subsystem:windows %CommonLinkerFlags%
