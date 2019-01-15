
ElfCode x86-64 Recompiler (Advent of Code 2018)
===============================================

My [original solution](https://github.com/usrlocalben/aoc2018/blob/master/21/p2.cxx), an interpreter implemented in C++:
```
aoc2018/21 % time ./p2.exe < input.txt
13943296
./p2.exe < input.txt  22.723 total
```

Recompiled to x86-64:
```
elfcode-x64-recompiler % time ./p2.exe < input.txt
13943296
./p2.exe < input.txt  1.090 total
```
