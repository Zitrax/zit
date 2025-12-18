![Build](https://github.com/Zitrax/zit/actions/workflows/build.yml/badge.svg)

# Build instructions

## Compilers

Note that at the moment the tested compilers are clang 22 and gcc 14 on Linux.

## CMake + vcpkg

* `git submodule update --init --recursive`
* `.\vcpkg\bootstrap-vcpkg.bat` or `./vcpkg/bootstrap-vcpkg.sh`
* `mkdir build`
* `cd build`
* `cmake -GNinja .. --preset zit-clang -DCMAKE_BUILD_TYPE=Release`
* `cmake --build . --config Release`

Or with --preset zit-gcc.

Note that on Linux `pkg-config` and `clang` or `gcc` must be installed.
