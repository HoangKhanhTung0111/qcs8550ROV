cmake_minimum_required(VERSION 3.10)
project(ROV_VLA_App CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_COMPILER "aarch64-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "aarch64-linux-gnu-g++")

set(QNN_SYSROOT "${CMAKE_CURRENT_SOURCE_DIR}/qnn_sysroot")
include_directories(${QNN_SYSROOT}/include)
link_directories(${QNN_SYSROOT}/lib)

# =========================================================
# 1. KHỐI VISION ENCODER 
# =========================================================
add_executable(test_vision src/test_vision_encoder.cpp)
target_link_libraries(test_vision QnnHtp QnnSystem dl pthread)

# =========================================================
# 2. KHỐI TOKENIZER (Sẽ mở comment khi code xong file src)
# =========================================================
# add_executable(test_token src/test_tokenizer.cpp)
# target_link_libraries(test_token QnnHtp QnnSystem dl pthread)

# =========================================================
# 3. KHỐI BACKBONE (Sẽ mở comment khi code xong file src)
# =========================================================
# add_executable(test_backbone src/test_backbone.cpp)
# target_link_libraries(test_backbone QnnHtp QnnSystem dl pthread)

# =========================================================
# 4. KHỐI ACTION EXPERT (Sẽ mở comment khi code xong file src)
# =========================================================
# add_executable(test_action src/test_action_expert.cpp)
# target_link_libraries(test_action QnnHtp QnnSystem dl pthread)