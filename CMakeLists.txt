cmake_minimum_required(VERSION 3.10)
project(ROV_VLA_App CXX)

# Ép sử dụng chuẩn C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Chỉ định trình biên dịch chéo ARM64 (sẽ chạy trong Docker)
set(CMAKE_C_COMPILER "aarch64-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "aarch64-linux-gnu-g++")

# Trỏ đến thư mục sysroot (Headers và Libs)
set(QNN_SYSROOT "${CMAKE_CURRENT_SOURCE_DIR}/qnn_sysroot")

# Thêm đường dẫn Headers
include_directories(${QNN_SYSROOT}/include)

# Thêm đường dẫn thư viện (.so)
link_directories(${QNN_SYSROOT}/lib)

# Tạo file thực thi
add_executable(rov_vla_app main.cpp)

# Liên kết với các thư viện lõi của Qualcomm và Linux
# dl: Thư viện dynamic linker của Linux để load .so lúc runtime
target_link_libraries(rov_vla_app QnnHtp QnnSystem dl pthread)