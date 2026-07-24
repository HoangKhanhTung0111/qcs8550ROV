#include <iostream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dlfcn.h>
#include <cstdlib>
#include <cstdio>

#include "QnnInterface.h"
#include "QnnBackend.h"
#include "QnnDevice.h"
#include "QnnContext.h"
#include "QnnGraph.h"
#include "System/QnnSystemInterface.h"
#include "System/QnnSystemContext.h"

typedef Qnn_ErrorHandle_t (*QnnInterfaceGetProvidersFn_t)(const QnnInterface_t*** providerList, uint32_t* numProviders);
typedef Qnn_ErrorHandle_t (*QnnSystemInterfaceGetProvidersFn_t)(const QnnSystemInterface_t*** providerList, uint32_t* numProviders);

void* readBinaryFileMmap(const std::string& filePath, size_t& fileSize) {
    int fd = open(filePath.c_str(), O_RDONLY);
    if (fd < 0) return nullptr;
    struct stat st;
    fstat(fd, &st);
    fileSize = st.st_size;
    void* mappedData = mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return (mappedData == MAP_FAILED) ? nullptr : mappedData;
}

int main() {
    std::cout << "========== VLA VISION: REAL-TIME GSTREAMER INFERENCE ==========\n";
    
    // --- 1. LOAD SYSTEM & HTP LIBRARIES ---
    void* sys_handle = dlopen("libQnnSystem.so", RTLD_NOW | RTLD_LOCAL);
    auto getSysProviders = (QnnSystemInterfaceGetProvidersFn_t)dlsym(sys_handle, "QnnSystemInterface_getProviders");
    const QnnSystemInterface_t** sysProviders = nullptr;
    uint32_t numSysProviders = 0;
    getSysProviders(&sysProviders, &numSysProviders);
    const QnnSystemInterface_t* qnnSys = sysProviders[0];

    void* htp_handle = dlopen("libQnnHtp.so", RTLD_NOW | RTLD_LOCAL);
    auto getHtpProviders = (QnnInterfaceGetProvidersFn_t)dlsym(htp_handle, "QnnInterface_getProviders");
    const QnnInterface_t** htpProviders = nullptr;
    uint32_t numHtpProviders = 0;
    getHtpProviders(&htpProviders, &numHtpProviders);
    const QnnInterface_t* qnn = htpProviders[0];

    // --- 2. ĐỌC FILE BIN & QUÉT METADATA ---
    std::string binPath = "models/vision_encoder_qairt_context.bin";
    size_t contextSize = 0;
    void* contextBuffer = readBinaryFileMmap(binPath, contextSize);

    QnnSystemContext_Handle_t sysCtxHandle = nullptr;
    qnnSys->QNN_SYSTEM_INTERFACE_VER_NAME.systemContextCreate(&sysCtxHandle);
    
    const QnnSystemContext_BinaryInfo_t* binaryInfo = nullptr;
    Qnn_ContextBinarySize_t binaryInfoSize = 0;
    qnnSys->QNN_SYSTEM_INTERFACE_VER_NAME.systemContextGetBinaryInfo(
        sysCtxHandle, contextBuffer, contextSize, &binaryInfo, &binaryInfoSize);

    Qnn_Tensor_t inTensor = QNN_TENSOR_INIT;
    Qnn_Tensor_t outTensor = QNN_TENSOR_INIT;
    size_t inSize = 1, outSize = 1;
    const char* graphName = nullptr;

    if (binaryInfo && binaryInfo->version == 3) {
        auto graph = binaryInfo->contextBinaryInfoV3.graphs[0];
        if (graph.version == 3) {
            graphName = graph.graphInfoV3.graphName;
            inTensor = graph.graphInfoV3.graphInputs[0];
            if (inTensor.version == 2) {
                for (uint32_t i = 0; i < inTensor.v2.rank; i++) inSize *= inTensor.v2.dimensions[i];
                inSize *= 4; 
                inTensor.v2.memType = QNN_TENSORMEMTYPE_RAW; 
            }
            outTensor = graph.graphInfoV3.graphOutputs[0];
            if (outTensor.version == 2) {
                for (uint32_t i = 0; i < outTensor.v2.rank; i++) outSize *= outTensor.v2.dimensions[i];
                outSize *= 4;
                outTensor.v2.memType = QNN_TENSORMEMTYPE_RAW; 
            }
        }
    }

    // --- 3. CẤP PHÁT BỘ NHỚ DMA ---
    void *inData = nullptr, *outData = nullptr;
    posix_memalign(&inData, 4096, inSize);
    posix_memalign(&outData, 4096, outSize);

    if (inTensor.version == 2) inTensor.v2.clientBuf = {inData, (uint32_t)inSize};
    if (outTensor.version == 2) outTensor.v2.clientBuf = {outData, (uint32_t)outSize};

    // --- 4. NẠP NPU (Nổ máy DSP) ---
    Qnn_BackendHandle_t backendHandle = nullptr;
    qnn->QNN_INTERFACE_VER_NAME.backendCreate(nullptr, (const QnnBackend_Config_t**)nullptr, &backendHandle);
    Qnn_DeviceHandle_t deviceHandle = nullptr;
    qnn->QNN_INTERFACE_VER_NAME.deviceCreate(nullptr, (const QnnDevice_Config_t**)nullptr, &deviceHandle);
    Qnn_ContextHandle_t contextHandle = nullptr;
    qnn->QNN_INTERFACE_VER_NAME.contextCreateFromBinary(
        backendHandle, deviceHandle, nullptr, contextBuffer, contextSize, &contextHandle, nullptr);
    Qnn_GraphHandle_t graphHandle = nullptr;
    qnn->QNN_INTERFACE_VER_NAME.graphRetrieve(contextHandle, graphName, &graphHandle);
    std::cout << "[INFO] NPU da nap model. Mo luong Camera...\n";

    // --- 5. HÚT STREAM CAMERA (GSTREAMER PIPE) ---
    std::string gstCmd = "gst-launch-1.0 -q v4l2src device=/dev/video2 num-buffers=1 ! "
                         "video/x-raw,width=640,height=480,framerate=30/1 ! "
                         "videocrop left=80 right=80 ! "
                         "videoscale ! video/x-raw,width=224,height=224 ! "
                         "videoconvert ! video/x-raw,format=RGB ! fdsink";

    FILE* pipe = popen(gstCmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "[ERROR] Khong the mo GStreamer pipe!\n";
        return -1;
    }

    // Đọc chính xác 224 * 224 * 3 = 150528 bytes
    size_t frameBytes = 224 * 224 * 3;
    std::vector<uint8_t> rgbBuffer(frameBytes);
    size_t bytesRead = fread(rgbBuffer.data(), 1, frameBytes, pipe);
    pclose(pipe);

    if (bytesRead != frameBytes) {
        std::cerr << "[ERROR] Loi doc frame! Chi nhan duoc " << bytesRead << " bytes.\n";
        return -1;
    }
    std::cout << "[INFO] Bat frame Camera (224x224 RGB) thanh cong!\n";

    // --- 6. CHUYỂN ĐỔI BỘ NHỚ VÀO DMA (HWC -> NCHW & Normalize) ---
    float* inFloat = static_cast<float*>(inData);
    int hw = 224 * 224;
    for (int c = 0; c < 3; ++c) {
        for (int h = 0; h < 224; ++h) {
            for (int w = 0; w < 224; ++w) {
                uint8_t pixel = rgbBuffer[(h * 224 + w) * 3 + c];
                inFloat[c * hw + h * 224 + w] = static_cast<float>(pixel) / 255.0f;
            }
        }
    }
    std::cout << "[INFO] Day anh thuc te vao NPU...\n";

    // --- 7. THỰC THI INFERENCE ---
    Qnn_ErrorHandle_t execErr = qnn->QNN_INTERFACE_VER_NAME.graphExecute(
        graphHandle, &inTensor, 1, &outTensor, 1, nullptr, nullptr);

    if (execErr == QNN_SUCCESS) {
        std::cout << "[SUCCESS] Vision NPU Inference thanh cong!\n";
        float* outFloat = static_cast<float*>(outData);
        std::cout << ">> 5 gia tri Vector Embedding tu Camera thuc te:\n";
        std::cout << "   [ " << outFloat[0] << ", " << outFloat[1] << ", " 
                  << outFloat[2] << ", " << outFloat[3] << ", " << outFloat[4] << " ]\n";
    } else {
        std::cerr << "[ERROR] DSP Execute that bai! Ma loi: " << execErr << "\n";
    }

    // --- 8. DỌN DẸP ---
    free(inData); free(outData);
    qnn->QNN_INTERFACE_VER_NAME.contextFree(contextHandle, nullptr);
    qnn->QNN_INTERFACE_VER_NAME.deviceFree(deviceHandle);
    qnn->QNN_INTERFACE_VER_NAME.backendFree(backendHandle);
    qnnSys->QNN_SYSTEM_INTERFACE_VER_NAME.systemContextFree(sysCtxHandle);
    munmap(contextBuffer, contextSize);
    dlclose(sys_handle); dlclose(htp_handle);

    return 0;
}