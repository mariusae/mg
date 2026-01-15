#!/bin/sh
# Generate the configure script using Docker/Podman with autotools
# Useful when autoconf/automake are not installed locally

# Build proxy environment arguments for docker if proxy is set
PROXY_ARGS=""
if [ -n "$http_proxy" ] || [ -n "$HTTP_PROXY" ]; then
    PROXY_ARGS="$PROXY_ARGS -e http_proxy=${http_proxy:-$HTTP_PROXY}"
fi
if [ -n "$https_proxy" ] || [ -n "$HTTPS_PROXY" ]; then
    PROXY_ARGS="$PROXY_ARGS -e https_proxy=${https_proxy:-$HTTPS_PROXY}"
fi

docker run --rm --network=host \
    -v "$(pwd):/src" -w /src \
    $PROXY_ARGS \
    alpine:latest \
    sh -c "apk add --no-cache autoconf automake && autoreconf -fi"
