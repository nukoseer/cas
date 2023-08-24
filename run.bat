@echo off

pushd build

call set_cpu_affinity.exe BlackDesertPatcher32.pae 0x5554

popd
