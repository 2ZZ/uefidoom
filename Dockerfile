# Build environment for uefidoom: EDK II + edk2-libc + AudioPkg, GCC toolchain.
# Works natively on x86_64 and aarch64 hosts (cross-compiles to X64 on aarch64).
FROM ubuntu:24.04

ARG EDK2_REF=edk2-stable202408
ARG EDK2_LIBC_REF=73056078b60c9378af08dd122a29913e25e84e1c
ARG AUDIOPKG_REF=e6ee2a317e0c9229ebad238a29cf37fd0aa9c422

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential git ca-certificates python3 python-is-python3 \
        uuid-dev nasm acpica-tools \
        $([ "$(uname -m)" = "x86_64" ] || echo gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu) \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 --branch "$EDK2_REF" https://github.com/tianocore/edk2.git /opt/edk2 \
    && git -C /opt/edk2 submodule update --init --depth 1 \
        BaseTools/Source/C/BrotliCompress/brotli MdePkg/Library/MipiSysTLib/mipisyst \
        MdeModulePkg/Library/BrotliCustomDecompressLib/brotli

RUN git init -q /opt/edk2-libc \
    && git -C /opt/edk2-libc fetch -q --depth 1 https://github.com/tianocore/edk2-libc.git "$EDK2_LIBC_REF" \
    && git -C /opt/edk2-libc checkout -q FETCH_HEAD

RUN git init -q /opt/pkgs/AudioPkg \
    && git -C /opt/pkgs/AudioPkg fetch -q --depth 1 https://github.com/FangJiaZhen/AudioPkg.git "$AUDIOPKG_REF" \
    && git -C /opt/pkgs/AudioPkg checkout -q FETCH_HEAD

RUN make -C /opt/edk2/BaseTools -j"$(nproc)"

# The project's .dsc references uefidoom/uefidoom/doom.inf, so the repo is
# mounted at /src/uefidoom and /src goes on PACKAGES_PATH.
ENV WORKSPACE=/build \
    EDK_TOOLS_PATH=/opt/edk2/BaseTools \
    PACKAGES_PATH=/src:/opt/edk2:/opt/edk2-libc:/opt/pkgs

COPY docker-build.sh /usr/local/bin/docker-build.sh
ENTRYPOINT ["/usr/local/bin/docker-build.sh"]
