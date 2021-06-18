@echo off
del pv.obj
del pv.res
del pv.exe
del pv.pdb
@echo on

REM to build without LibRaw:
rc pv.rc
cl /nologo pv.cxx /I.\ /MT /Ox /Qpar /O2 /Oi /Ob2 /EHac /Zi /Gy /DNDEBUG /D_AMD64_ /link pv.res /OPT:REF /subsystem:windows

REM to build with LibRaw:
REM rc /DPV_USE_LIBRAW pv.rc
REM cl /nologo pv.cxx /DPV_USE_LIBRAW /I.\ /MT /Ox /Qpar /O2 /Oi /Ob2 /EHac /Zi /Gy /DNDEBUG /D_AMD64_ /link pv.res /OPT:REF /subsystem:windows


