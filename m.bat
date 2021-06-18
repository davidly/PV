@echo off
del pv.obj
del pv.res
del pv.exe
del pv.pdb
@echo on

call VsDevCmd.bat

REM to build without LibRaw:
REM rc pv.rc
REM cl /nologo pv.cxx /MT /Ox /Qpar /O2 /Oi /Ob2 /EHac /Zi /Gy /DNDEBUG /D_AMD64_ /link pv.res /OPT:REF /subsystem:windows

REM to build with LibRaw:
rc /DPV_USE_LIBRAW pv.rc
cl /nologo pv.cxx /DPV_USE_LIBRAW /MT /Ox /Qpar /O2 /Oi /Ob2 /EHac /Zi /Gy /DNDEBUG /D_AMD64_ /link pv.res /OPT:REF /subsystem:windows


