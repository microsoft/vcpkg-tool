# This infrastructure is adapted from that the .NET team uses, see
# https://github.com/dotnet/dotnet-buildtools-prereqs-docker
# and
# https://github.com/dotnet/runtime/blob/main/docs/workflow/building/coreclr/linux-instructions.md
FROM mcr.microsoft.com/cbl-mariner/base/core:2.0

RUN tdnf install -y \
        # Common utilities
        ca-certificates \
        git \
        tar \
        curl \
        # Build tools
        binutils \
        cmake \
        ninja-build \
        # Rootfs build dependencies
        bzip2-devel \
        debootstrap \
        libarchive-devel \
        openssl-devel

# Obtain ubuntu package signing key (for use by debootstrap)
# 1. Download the ubuntu keyrings
RUN curl -L -o ubuntu-keyring_2021.03.26.tar.gz https://mirrors.edge.kernel.org/ubuntu/pool/main/u/ubuntu-keyring/ubuntu-keyring_2021.03.26.tar.gz && \
# 2. Check that they have the correct SHA
    echo "492eed5c06408c6f632577adb0796130af5d6542013ef418f47187a209e49bb1 ubuntu-keyring_2021.03.26.tar.gz" | sha256sum -c && \
    tar xf ubuntu-keyring_2021.03.26.tar.gz && \
    rm ubuntu-keyring_2021.03.26.tar.gz && \
# 3. Install the needed keyring and delete the rest
    pushd ubuntu-keyring-2021.03.26 && \
    mkdir -p /usr/share/keyrings && \
    mv keyrings/ubuntu-archive-keyring.gpg /usr/share/keyrings && \
    popd && \
    rm -r ubuntu-keyring-2021.03.26

# Build the 'crossrootfs'
# Note that we only need libstdc++-9-dev rather than all of g++-9, but choosing g++-9
# gets other optional dependencies to agree with g++-9 rather than older gccs
RUN debootstrap "--variant=minbase" --force-check-gpg --arch amd64 xenial /crossrootfs/x64 http://archive.ubuntu.com/ubuntu/
COPY sources.list .
RUN chroot /crossrootfs/x64 apt-get clean && \
    cp /etc/resolv.conf /crossrootfs/x64/etc/resolv.conf && \
    cp sources.list /crossrootfs/x64/etc/apt/sources.list && \
    chroot /crossrootfs/x64 apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 60C317803A41BA51845E371A1E9377A2BA9EF27F && \
    chroot /crossrootfs/x64 apt-get update -o Acquire::CompressionTypes::Order::=gz

# Repeated runs of apt-get install workaround 'hash sum mismatch' errors
# (This is also why these are a separate cache layer)
RUN chroot /crossrootfs/x64 apt-get install -y build-essential symlinks "g++-9" || \
    chroot /crossrootfs/x64 apt-get install -y build-essential symlinks "g++-9" || \
    chroot /crossrootfs/x64 apt-get install -y build-essential symlinks "g++-9" || \
    chroot /crossrootfs/x64 apt-get install -y build-essential symlinks "g++-9" || \
    chroot /crossrootfs/x64 apt-get install -y build-essential symlinks "g++-9" || \
    chroot /crossrootfs/x64 apt-get install -y build-essential symlinks "g++-9" || \
    chroot /crossrootfs/x64 apt-get install -y build-essential symlinks "g++-9" && \
    chroot /crossrootfs/x64 symlinks -cr /usr && \
    chroot /crossrootfs/x64 apt-get clean
