FROM ubuntu:22.04

WORKDIR /app

COPY . /app

RUN apt-get update && apt-get install -y \
    build-essential \
    g++-12 \
    gcc-12 \
    libboost-all-dev \
    libfmt-dev \
    git \
    protobuf-compiler \
    libprotobuf-dev \
    autoconf \
    automake \
    libtool \
    curl \
    make \
    unzip \
    wget \
    software-properties-common \
    lsb-release \
    gnupg \
    cmake \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 100

RUN wget https://apt.llvm.org/llvm.sh && \
    chmod +x llvm.sh && \
    ./llvm.sh 17 && \
    ln -s /usr/bin/clangd-17 /usr/bin/clangd && \
    rm llvm.sh

CMD ["bash"]
