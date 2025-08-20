FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ADD https://packages.microsoft.com/config/ubuntu/24.04/packages-microsoft-prod.deb /packages-microsoft-prod.deb
ADD https://deb.nodesource.com/setup_24.x /nodesource_setup.sh

ENV APT_PACKAGES="git curl zip unzip tar build-essential cmake ninja-build powershell nodejs mono-complete"

RUN <<END_OF_SCRIPT
chmod +x /nodesource_setup.sh
/nodesource_setup.sh
rm /nodesource_setup.sh

# For powershell
dpkg -i packages-microsoft-prod.deb
rm -f packages-microsoft-prod.deb

apt-get -y update
apt-get upgrade -y
apt-get install -y $APT_PACKAGES

END_OF_SCRIPT
