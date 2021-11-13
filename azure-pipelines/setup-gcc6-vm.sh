#!/bin/sh
# This script is run by cloud-init on top of an Ubuntu 18.04 blank image to install prerequisites.
sudo add-apt-repository universe
sudo apt-get update
sudo apt-get install -y g++-6 gcc-6 build-essential make
curl -o cmake-3.21.1-linux-x86_64.sh -L https://github.com/Kitware/CMake/releases/download/v3.21.1/cmake-3.21.1-linux-x86_64.sh
chmod +x cmake-3.21.1-linux-x86_64.sh
sudo ./cmake-3.21.1-linux-x86_64.sh --skip-license --prefix=/
