[![Build Status](https://travis-ci.org/Zitrax/zit.svg?branch=master)](https://travis-ci.org/Zitrax/zit)

Build instructions

* mkdir build
* cd build
* conan install ..
* cmake -GNinja ..
* cmake --build .

The automatic download of googletest might fail with errors like CURL_OPENSSL_3
not found. In that case the current workaround is to remove libcurl4 and install
libcurl3.

Also useful, to set the compiler pass -DCMAKE_CXX_COMPILER=g++-8 or similar to
cmake.
