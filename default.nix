{ pkgs ? import (fetchTarball {
  url =
    "https://github.com/NixOS/nixpkgs/archive/f02597fb8e07c57c969df77f39a63bda64b09848.tar.gz";
  sha256 = "sha256:1a76da1j10m60jhfn5xdi8fmy7c2a8lhijrimn62ysd990m795wf";
}) { } }:

let

  # Override asio to a custom version
  asio = pkgs.asio.overrideAttrs (finalAttrs: previousAttrs: rec {
    version = "1.24.0";
    src = pkgs.fetchurl {
      url = "mirror://sourceforge/asio/asio-${version}.tar.bz2";
      sha256 = "iXaBLCShGGAPb88HGiBgZjCmmv5MCr7jsN6lKOaCxYU=";
    };
    enableParallelBuilding = true;
  });

  openssl = pkgs.openssl_1_1;

in pkgs.stdenv.mkDerivation rec {
  pname = "zit";
  version = "0.1.0";

  src = ./.;

  buildInputs =
    [ asio pkgs.cmake pkgs.gtest pkgs.ninja openssl.dev openssl.bin pkgs.spdlog pkgs.tbb pkgs.pkg-config ];

  # Adding FindXXX.cmake for packages not providing CMake support
  configurePhase = ''
        echo '
    find_path(ASIO_INCLUDE_DIR NAMES asio.hpp PATHS ${asio}/include REQUIRED NO_DEFAULT_PATH)
    message(STATUS "Found asio include: ''${ASIO_INCLUDE_DIR}")
    set(ASIO_FOUND TRUE)
    set(ASIO_INCLUDE_DIRS ''${ASIO_INCLUDE_DIR})
    add_library(asio::asio INTERFACE IMPORTED GLOBAL)
    mark_as_advanced(ASIO_INCLUDE_DIR)' > Findasio.cmake

        echo '
    find_path(TBB_INCLUDE_DIR NAMES tbb PATHS ${pkgs.tbb}/include REQUIRED NO_DEFAULT_PATH)
    message(STATUS "Found tbb include: ''${TBB_INCLUDE_DIR}")
    find_library(TBB_LIBRARY NAMES tbb PATHS ${pkgs.tbb}/lib REQUIRED NO_DEFAULT_PATH)
    message(STATUS "Found tbb lib: ''${TBB_LIBRARY}")
    set(TBB_FOUND TRUE)
    set(TBB_INCLUDE_DIRS ''${TBB_INCLUDE_DIR})
    set(TBB_LIBRARIES TBB::tbb)
    add_library(TBB::tbb SHARED IMPORTED GLOBAL)
    set_target_properties(TBB::tbb PROPERTIES
      IMPORTED_LOCATION "''${TBB_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "''${TBB_INCLUDE_DIR}"
    )
    mark_as_advanced(TBB_INCLUDE_DIR)
    mark_as_advanced(TBB_LIBRARIES)' > FindTBB.cmake

    rm -vf CMakeCache.txt
    cmake . -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MODULE_PATH="$PWD" -DCMAKE_INSTALL_PREFIX="$out"
  '';

  buildPhase = ''
    ninja
  '';

  installPhase = ''
    ninja install
  '';
}
