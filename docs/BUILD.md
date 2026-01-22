# Build ApexNNUE

## Requirements
- CMake >= 3.20
- C++20 compiler (MSVC 2022 / clang / GCC)
- Optional: CUDA Toolkit (for GPU NNUE backend)

## Windows (MSVC)
```powershell
cd D:\Programming\Chess Engine\ApexNNUE
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

With CUDA:
```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DUSE_CUDA=ON
cmake --build build --config Release
```

## Linux
```bash
cd /path/to/ApexNNUE
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

With CUDA:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DUSE_CUDA=ON
cmake --build build -j
```

## Tests
```bash
ctest --test-dir build
```
or run directly:
```bash
./build/test_perft
./build/test_movegen
```

## Running
```bash
./build/ApexNNUE
```
