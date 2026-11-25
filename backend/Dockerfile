# ---------- Etapa 1: compilar ----------
FROM debian:trixie AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl ca-certificates \
        zip unzip tar pkg-config autoconf automake autoconf-archive libtool \
        bison flex perl python3 linux-libc-dev \
    && rm -rf /var/lib/apt/lists/*

# vcpkg (gestor de dependencias C++ que usa el proyecto)
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone --depth=1 https://github.com/microsoft/vcpkg.git $VCPKG_ROOT \
    && $VCPKG_ROOT/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /src

# Primero solo las dependencias: Docker las cachea y no se recompilan
# en cada deploy mientras vcpkg.json no cambie.
COPY vcpkg.json .
RUN $VCPKG_ROOT/vcpkg install --x-install-root=/src/vcpkg_installed

# Después el código
COPY . .
RUN cmake -B build -S . -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
        -DVCPKG_INSTALLED_DIR=/src/vcpkg_installed \
    && cmake --build build --target regalitos -j"$(nproc)"

# ---------- Etapa 2: imagen final, liviana ----------
FROM debian:trixie-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates postgresql-client curl \
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

HEALTHCHECK --interval=15s --timeout=5s --start-period=20s --retries=3 \
    CMD curl -fs http://localhost:${PORT}/health || exit 1

ENTRYPOINT ["./docker-entrypoint.sh"]
