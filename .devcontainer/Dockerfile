FROM python:3.12-slim

# Install developer dools
RUN apt-get update && apt-get install -y \
    git \
    curl \
    vim \
    nano \
    wget \
    sudo \
    build-essential \
    libjsoncpp-dev \
    libvtk9-dev \
    libgdcm-dev \
    libssl-dev \
    zlib1g-dev \
    openssl \
    cmake \
    ninja-build \
    ca-certificates \
    qt6-base-dev \
    libvtk9-qt-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt

RUN git clone https://github.com/InsightSoftwareConsortium/ITK.git \
    && cd ITK \
    && git submodule update --init \
    && mkdir build && cd build \
    && cmake .. \
    && make -j$(nproc) \
    && make install 

ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=$USER_UID
RUN groupadd --gid $USER_GID $USERNAME \
    && useradd --uid $USER_UID --gid $USER_GID -m $USERNAME \
    && echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME \
    && mkdir -p /workspace \
    && chown $USER_UID:$USER_GID /workspace

USER $USERNAME

WORKDIR /workspace

# Install Python development tools
RUN pip install --no-cache-dir --upgrade pip setuptools wheel

# Create directories
RUN mkdir -p /workspace/backend/logs
RUN mkdir -p /workspace/frontend/node_modules && chown -R vscode:vscode /workspace || true && chmod +x /workspace/.devcontainer/post-create.sh || true

# Set environment variables
ENV PYTHONPATH=/workspace/backend
ENV PYTHONUNBUFFERED=1
ENV NODE_ENV=development
ENV SHELL=/bin/bash

EXPOSE 8501
