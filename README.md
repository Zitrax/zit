![Build](https://github.com/Zitrax/zit/actions/workflows/build.yml/badge.svg)

Build instructions

* mkdir build
* cd build
* conan install .. --build --profile <profile>
* cmake -GNinja ..
* cmake --build .

Notes:

When doing the conan install and running cmake it's important to set CXX/CC to
the expected compiler versions that matches the conan profile.
