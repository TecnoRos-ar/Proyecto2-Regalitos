# ---------- Etapa 1: compilar ----------
FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git ca-certificates pkg-config \
        libpq-dev libssl-dev libsodium-dev nlohmann-json3-dev \
        doctest-dev libasio-dev \
    && rm -rf /var/lib/apt/lists/*

# Crow (framework web, solo headers)
RUN git clone -q -c advice.detachedHead=false --depth=1 --branch v1.2.1 \
        https://github.com/CrowCpp/Crow.git /tmp/crow \
    && cmake -S /tmp/crow -B /tmp/crow/b -G Ninja \
        -DCROW_BUILD_EXAMPLES=OFF -DCROW_BUILD_TESTS=OFF \
    && cmake --install /tmp/crow/b \
    && rm -rf /tmp/crow

# libpqxx 8 (el código usa su API nueva; Ubuntu trae una versión vieja)
RUN git clone -q -c advice.detachedHead=false --depth=1 --branch 8.0.2 \
        https://github.com/jtv/libpqxx.git /tmp/pqxx \
    && cmake -S /tmp/pqxx -B /tmp/pqxx/b -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DSKIP_BUILD_TEST=ON -DBUILD_SHARED_LIBS=OFF \
    && cmake --build /tmp/pqxx/b -j"$(nproc)" \
    && cmake --install /tmp/pqxx/b \
    && rm -rf /tmp/pqxx

# Adaptador: el CMakeLists busca libsodium con el nombre que usa vcpkg
RUN mkdir -p /opt/shims/unofficial-sodium \
    && printf '%s\n' \
        'if(NOT TARGET unofficial-sodium::sodium)' \
        '  find_package(PkgConfig REQUIRED)' \
        '  pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)' \
        '  add_library(unofficial-sodium::sodium INTERFACE IMPORTED)' \
        '  target_link_libraries(unofficial-sodium::sodium INTERFACE PkgConfig::SODIUM)' \
        'endif()' \
        'set(unofficial-sodium_FOUND TRUE)' \
        > /opt/shims/unofficial-sodium/unofficial-sodiumConfig.cmake

WORKDIR /src
COPY . .
RUN cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH=/opt/shims \
    && cmake --build build --target regalitos -j"$(nproc)"

# ---------- Etapa 2: imagen final, liviana ----------
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        libpq5 libssl3t64 libsodium23 postgresql-client curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /src/build/regalitos ./regalitos
COPY public ./public
COPY migrations ./migrations
COPY docker-entrypoint.sh ./docker-entrypoint.sh
RUN chmod +x ./docker-entrypoint.sh

ENV PORT=8080 \
    PUBLIC_DIR=/app/public
EXPOSE 8080

ENTRYPOINT ["./docker-entrypoint.sh"]
