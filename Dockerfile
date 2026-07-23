# Sử dụng chính xác phiên bản OS của board
FROM ubuntu:22.04

# Bỏ qua các prompt hỏi Yes/No khi cài đặt
ENV DEBIAN_FRONTEND=noninteractive

# Cài đặt bộ công cụ biên dịch chéo cho ARM64 và CMake
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    g++-aarch64-linux-gnu \
    gcc-aarch64-linux-gnu \
    gdb-multiarch \
    git \
    wget \
    python3 \
    && rm -rf /var/lib/apt/lists/*

# Tạo thư mục làm việc mặc định
WORKDIR /workspace

# Lệnh chạy mặc định để giữ container luôn sống
CMD ["tail", "-f", "/dev/null"]