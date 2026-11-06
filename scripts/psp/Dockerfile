# PSPDEV toolchain image, every input pinned: base image by digest, apt packages
# from a dated Ubuntu snapshot with exact versions, PSPDEV release by sha256.
FROM ubuntu@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852

ARG UBUNTU_SNAPSHOT=20261001T000000Z
ARG PSPDEV_VERSION=v20261001
ARG PSPDEV_SHA256=a86efe624e770005290919617859e9df24bd8fcd3e364f4587f23ec7364b0acd

ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8

# The base image has no CA bundle, so the first update runs without TLS peer
# verification; apt still verifies the signed Release files. Verification is
# switched back on as soon as ca-certificates is installed.
RUN rm -f /etc/apt/sources.list.d/ubuntu.sources \
 && for suite in noble noble-updates noble-security; do \
      echo "deb https://snapshot.ubuntu.com/ubuntu/${UBUNTU_SNAPSHOT} ${suite} main universe"; \
    done > /etc/apt/sources.list.d/snapshot.list \
 && echo 'Acquire::https::Verify-Peer "false";' > /etc/apt/apt.conf.d/99bootstrap \
 && apt-get update \
 && apt-get install --yes --no-install-recommends ca-certificates=20260601~24.04.1 \
 && rm /etc/apt/apt.conf.d/99bootstrap \
 && apt-get update \
 && apt-get install --yes --no-install-recommends \
      make=4.3-4.1build2 \
      curl=8.5.0-2ubuntu10.15 \
      libreadline8t64=8.2-4build1 \
      libusb-0.1-4=2:0.1.12-35build1 \
      libgpgme11t64=1.18.0-4.1ubuntu4 \
      libarchive13t64=3.7.2-2ubuntu0.8 \
      libncurses6=6.4+20240113-1ubuntu2.2 \
      libmpc3=1.3.1-1build1.1 \
      libmpfr6=4.2.1-1build1.1 \
      libgmp10=2:6.3.0+dfsg-2ubuntu6.1 \
      libexpat1=2.6.1-2ubuntu0.6 \
      libusb-1.0-0=2:1.0.27-1 \
 && rm -rf /var/lib/apt/lists/*

RUN curl --fail --location --retry 3 \
      "https://github.com/pspdev/pspdev/releases/download/${PSPDEV_VERSION}/pspdev-ubuntu-latest-x86_64.tar.gz" \
      --output /tmp/pspdev.tar.gz \
 && echo "${PSPDEV_SHA256}  /tmp/pspdev.tar.gz" | sha256sum --check \
 && tar -xzf /tmp/pspdev.tar.gz -C /opt \
 && rm /tmp/pspdev.tar.gz \
 && /opt/pspdev/bin/psp-gcc --version

ENV PSPDEV=/opt/pspdev
ENV PATH=/opt/pspdev/bin:$PATH
