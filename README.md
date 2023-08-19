![Build](https://github.com/Zitrax/zit/actions/workflows/build.yml/badge.svg)

# Build instructions

## CMake + vcpkg

* `mkdir build`
* `cd build`
* `cmake -GNinja .. --preset zit-clang -DCMAKE_BUILD_TYPE=Release`
* `cmake --build . --config Release`

Or with --preset zit-gcc.

## [Nix](https://nixos.org)

* `nix-build`
