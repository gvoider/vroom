# Two-stage Dockerfile for the gvoider/vroom fork.
#
# Stage 1 builds the vroom CLI binary from source.
# Stage 2 bundles vroom + vroom-express into a slim runtime image listening on :3000.
#
# The runtime contract (port 3000, GET /health, POST /) matches upstream
# vroom-docker so the image is a drop-in replacement for the consumer
# service at http://vroom:3000/.

ARG DEBIAN_TAG=trixie-slim
ARG VROOM_EXPRESS_VERSION=v0.12.0

FROM debian:${DEBIAN_TAG} AS build

RUN apt-get update \
 && apt-get install --no-install-recommends -y \
      build-essential \
      ca-certificates \
      git \
      libasio-dev \
      libglpk-dev \
      libssl-dev \
      pkg-config \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

# Submodules come from the build context so `git submodule update` is a no-op
# when the caller has already initialised them. If they haven't, fall back to
# a network init.
RUN git config --global --add safe.directory /src \
 && ([ -f include/cxxopts/include/cxxopts.hpp ] \
     || git submodule update --init --recursive)

RUN make -C src -j"$(nproc)"


FROM debian:${DEBIAN_TAG} AS runtime

ARG VROOM_EXPRESS_VERSION

RUN apt-get update \
 && apt-get install --no-install-recommends -y \
      ca-certificates \
      curl \
      git \
      libglpk40 \
      libssl3 \
      nodejs \
      npm \
 && rm -rf /var/lib/apt/lists/*

RUN useradd --system --create-home --shell /usr/sbin/nologin vroom

COPY --from=build /src/bin/vroom /usr/local/bin/vroom

# Pin vroom-express to a known-good tag; upstream maintains matching releases.
# `HUSKY=0` + `--ignore-scripts` avoids the husky `prepare` hook that fails on
# shallow clones (no .git dir to install hooks into).
RUN git clone --depth 1 --branch "${VROOM_EXPRESS_VERSION}" \
      https://github.com/VROOM-Project/vroom-express /opt/vroom-express \
 && cd /opt/vroom-express \
 && HUSKY=0 npm install --omit=dev --ignore-scripts \
 && chown -R vroom:vroom /opt/vroom-express

COPY docker/entrypoint.sh /usr/local/bin/vroom-entrypoint
# Strip Windows CRLF in case the checkout came from a core.autocrlf=true clone
# and restore the executable bit.
RUN sed -i 's/\r$//' /usr/local/bin/vroom-entrypoint \
 && chmod +x /usr/local/bin/vroom-entrypoint

USER vroom
WORKDIR /opt/vroom-express

EXPOSE 3000
HEALTHCHECK --interval=30s --timeout=3s --start-period=10s --retries=3 \
  CMD curl -fsS http://localhost:3000/health || exit 1

ENTRYPOINT ["/usr/local/bin/vroom-entrypoint"]
CMD ["node", "/opt/vroom-express/src/index.js"]
