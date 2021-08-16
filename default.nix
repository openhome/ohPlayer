{ pkgs ? import (
  builtins.fetchTarball {
    url = "https://github.com/nixos/nixpkgs/archive/20.09.tar.gz";
    sha256 = "1wg61h4gndm3vcprdcg7rc4s1v3jkm5xd7lw8r2f67w502y94gcy";
  }
) {} }:

with pkgs;

mkShell {
  buildInputs = [
    alsaLib
    autoconf
    automake
    clang
    cmake
    docker
    docker-compose
    doxygen
    gcc
    glib
    libav
    libtool
    lsb-release
    pkg-config
    python2
    python2Packages.setuptools
    python2Packages.pip
    python2Packages.requests
    python2Packages.virtualenv
    python2Packages.boto3
  ];
  shellHook = ''
    virtualenv venv
    source venv/bin/activate
  '';
}
