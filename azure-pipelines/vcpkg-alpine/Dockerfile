FROM alpine:3.11

RUN apk add alpine-sdk cmake ninja git curl tar gzip zip

COPY . /source

RUN cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DVCPKG_DEVELOPMENT_WARNINGS=ON -DVCPKG_WARNINGS_AS_ERRORS=ON -DVCPKG_BUILD_FUZZING=OFF -DVCPKG_EMBED_GIT_SHA=OFF -DCMAKE_CXX_FLAGS="-static -s -static-libgcc -static-libstdc++" -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DFETCHCONTENT_SOURCE_DIR_FMT=/source/external-fmtlib -S /source -B /build

RUN ninja -C build
