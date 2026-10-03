# syntax=docker/dockerfile:1.7
ARG DEPS_IMAGE=minion:deps
ARG STATIC_IMAGE=cgr.dev/chainguard/static:latest
ARG BUILD_JOBS=8

FROM ${DEPS_IMAGE} AS deps

FROM deps AS build

WORKDIR /src

COPY CMakeLists.txt ./
COPY 3rdparty ./3rdparty
COPY cmake ./cmake
COPY schemas ./schemas
COPY src ./src
COPY cmd ./cmd

ARG BUILD_JOBS

RUN cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release -DMINION_TEST=OFF \
    && cmake --build build --parallel "${BUILD_JOBS}"

FROM deps AS assemble

COPY --from=build /src/build/bin/ /out/usr/local/bin/

RUN mkdir -p /out/app/data \
    && chown -R 65532:65532 /out/app \
    && cp "$(command -v tini)" /out/usr/local/bin/tini \
    && loader="$(find /usr/lib /lib -maxdepth 1 -name 'ld-linux-*.so.*' | head -n1)" \
    && for bin in /out/usr/local/bin/*; do \
         "$loader" --list "$bin" 2>/dev/null; \
       done \
       | sed -nE 's#^[[:space:]]*([^[:space:]]+)[[:space:]]=>[[:space:]]*(/[^[:space:]]+).*#\1|\2#p; s#^[[:space:]]*(/[^[:space:]]+)[[:space:]].*#\1|\1#p' \
       | sort -u > /tmp/libs.txt \
    && while IFS='|' read -r name path; do \
         real="$(realpath "$path")" \
         && mkdir -p "/out$(dirname "$real")" \
         && cp -Lf "$real" "/out$real" \
         && base="$(basename "$name")" \
         && [ "$base" = "$(basename "$real")" ] \
         || ln -sf "$(basename "$real")" "/out$(dirname "$real")/$base"; \
       done < /tmp/libs.txt \
    && cp -L "$loader" "/out$loader"

FROM ${STATIC_IMAGE} AS execute

COPY --from=assemble /out/ /

WORKDIR /app

ENV TZ=UTC \
    MINION_DB=/app/data/minion.db

VOLUME /app/data

USER 65532:65532

ENTRYPOINT ["/usr/local/bin/tini", "--", "/usr/local/bin/minion"]
