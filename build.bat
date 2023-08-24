@echo off

IF NOT EXIST build mkdir build
pushd build

call vcvarsall.bat x64

set debug_compiler_flags=/Od /MTd /Zo /Z7 /RTC1 /fsanitize=address
set release_compiler_flags=/O2
set common_compiler_flags=/Oi /TC /FC /GS- /nologo /Wall /WX /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /wd5045 /wd4710 /wd4711 
rem /wd4774 /wd4062 /wd4201

set debug_linker_flags=/debug
set release_linker_flags=/fixed /opt:icf /opt:ref libvcruntime.lib
set common_linker_flags=/incremental:no

set debug=no
set compiler=cl

if %debug%==yes (
   set common_compiler_flags=%common_compiler_flags% %debug_compiler_flags%
   set common_linker_flags=%common_linker_flags% %debug_linker_flags%
) else (
   set common_compiler_flags=%common_compiler_flags% %release_compiler_flags%
   set common_linker_flags=%common_linker_flags% %release_linker_flags%
)

%compiler% %common_compiler_flags% ..\set_cpu_affinity.c /link %common_linker_flags% /out:set_cpu_affinity.exe

popd

