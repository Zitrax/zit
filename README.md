[![Build Status](https://travis-ci.org/Zitrax/zit.svg?branch=master)](https://travis-ci.org/Zitrax/zit)

Build instructions

* mkdir build
* cd build
* conan install ..
* cmake -GNinja ..
* cmake --build .


Notes:

The conan install step might fail if the package configuration is not in the
central repository. Then pass --build to build it locally. When doing this
and when running cmake it's important to set CXX/CC to the expected compiler
versions that matches the conan profile.
