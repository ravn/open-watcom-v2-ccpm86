# Open Watcom v2 (ravn fork) toolchain image.
# Supports linux/amd64 (BINDIR=binl64) and linux/arm64 (BINDIR=arml64).
#
# Build context: the OW `rel/` directory.
# CP/M-86 clib (alle 4 modeller s/m/c/l) skal allerede ligge i
# rel/lib286/cpm86/ inden docker build køres -- build_open_watcom_docker.sh
# eller docker-cpm86.yml CI-workflow sørger for det.
#
# Build args:
#   BINDIR  amd64: binl64 (default)   arm64: arml64
FROM debian:stable-slim

ARG BINDIR=binl64

LABEL org.opencontainers.image.title="open-watcom-cpm86 (ravn fork)" \
      org.opencontainers.image.source="https://github.com/ravn/open-watcom-v2-ccpm86" \
      org.opencontainers.image.description="Open Watcom v2 toolchain; CP/M-86 first-class med alle memory modeller (owcc -bcpm86 -m{s,m,c,l})"

COPY . /opt/watcom

# BINDIR i PATH vælger host-arkitekturens tool-dir (binl64 eller arml64).
ENV WATCOM=/opt/watcom \
    EDPATH=/opt/watcom/eddat \
    WIPFC=/opt/watcom/wipfc \
    INCLUDE=/opt/watcom/h
ENV PATH=/opt/watcom/${BINDIR}:/opt/watcom/binl:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

WORKDIR /work
CMD ["bash"]
