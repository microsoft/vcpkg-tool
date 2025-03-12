FROM mcr.microsoft.com/devcontainers/base:ubuntu-22.04

ADD https://packages.microsoft.com/config/ubuntu/22.04/packages-microsoft-prod.deb /packages-microsoft-prod.deb

ENV APT_PACKAGES="git curl zip unzip tar build-essential cmake ninja-build powershell"

RUN <<END_OF_SCRIPT
# For powershell
dpkg -i packages-microsoft-prod.deb
rm -f packages-microsoft-prod.deb

apt-get -y update
apt-get upgrade -y
apt-get install -y $APT_PACKAGES

END_OF_SCRIPT