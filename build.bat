c:\cygwin64\bin\nasm -fwin64 run.asm -l run.lst
cl /EHsc /O2 /Ox /std:c++17 /DNDEBUG p2.cxx run.obj