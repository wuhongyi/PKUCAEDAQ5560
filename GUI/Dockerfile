FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential cmake qt6-base-dev libgl1-mesa-dev libzmq5 python3 fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN bash tests/verify_linux.sh /tmp/r5560-build
CMD ["/tmp/r5560-build/gddaq5560", "--demo"]
