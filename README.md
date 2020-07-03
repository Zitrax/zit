[![Build Status](https://travis-ci.org/Zitrax/zit.svg?branch=master)](https://travis-ci.org/Zitrax/zit)

Build instructions

* mkdir build
* cd build
* conan install .. --build --profile <profile>
* cmake -GNinja ..
* cmake --build .

Notes:

When doing the conan install and running cmake it's important to set CXX/CC to
the expected compiler versions that matches the conan profile.
