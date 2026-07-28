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
#include <chrono>
#include <cstring>

#include "QnnInterface.h"
#include "QnnBackend.h"
#include "QnnDevice.h"
#include "QnnContext.h"
#include "QnnGraph.h"
#include "QnnTypes.h"
#include "System/QnnSystemInterface.h"
#include "System/QnnSystemContext.h"
#include "HTP/QnnHtpDevice.h"
#include "HTP/QnnHtpPerfInfrastructure.h"

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

size_t getQnnDataTypeSize(Qnn_DataType_t dataType) {
    switch (dataType) {
        case QNN_DATATYPE_FLOAT_32: return 4;
        case QNN_DATATYPE_FLOAT_16: return 2;
        case QNN_DATATYPE_UINT_8:
        case QNN_DATATYPE_INT_8: return 1;
        default: return 4; // Fallback
    }
}

int main() {
    std::cout << "========== VLA VISION: QAIRT OPTIMIZED PIPELINE ==========\n";
    
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

    // --- 2. ĐỌC METADATA (TỰ ĐỘNG PHÁT HIỆN KIỂU DỮ LIỆU) ---
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
    size_t inElements = 1, outElements = 1;
    size_t inSize = 1, outSize = 1;
    const char* graphName = nullptr;

    if (binaryInfo && binaryInfo->version == 3) {
        auto graph = binaryInfo->contextBinaryInfoV3.graphs[0];
        if (graph.version == 3) {
            graphName = graph.graphInfoV3.graphName;
            
            inTensor = graph.graphInfoV3.graphInputs[0];
            if (inTensor.version == 2) {
                for (uint32_t i = 0; i < inTensor.v2.rank; i++) inElements *= inTensor.v2.dimensions[i];
                inSize = inElements * getQnnDataTypeSize(inTensor.v2.dataType);
                inTensor.v2.memType = QNN_TENSORMEMTYPE_RAW; 
            }
            
            outTensor = graph.graphInfoV3.graphOutputs[0];
            if (outTensor.version == 2) {
                for (uint32_t i = 0; i < outTensor.v2.rank; i++) outElements *= outTensor.v2.dimensions[i];
                outSize = outElements * getQnnDataTypeSize(outTensor.v2.dataType);
                outTensor.v2.memType = QNN_TENSORMEMTYPE_RAW; 
            }
        }
    }

    std::cout << "[INFO] Input type: " << inTensor.v2.dataType << " | Bytes: " << inSize << "\n";
    std::cout << "[INFO] Output type: " << outTensor.v2.dataType << " | Bytes: " << outSize << "\n";

    // --- 3. CẤP PHÁT BỘ NHỚ ALIGN ---
    void *inData = nullptr, *outData = nullptr;
    posix_memalign(&inData, 4096, inSize);
    posix_memalign(&outData, 4096, outSize);

    if (inTensor.version == 2) inTensor.v2.clientBuf = {inData, (uint32_t)inSize};
    if (outTensor.version == 2) outTensor.v2.clientBuf = {outData, (uint32_t)outSize};

    // --- 4. NẠP NPU & ÉP XUNG ---
    Qnn_BackendHandle_t backendHandle = nullptr;
    qnn->QNN_INTERFACE_VER_NAME.backendCreate(nullptr, (const QnnBackend_Config_t**)nullptr, &backendHandle);
    Qnn_DeviceHandle_t deviceHandle = nullptr;
    qnn->QNN_INTERFACE_VER_NAME.deviceCreate(nullptr, (const QnnDevice_Config_t**)nullptr, &deviceHandle);

    QnnDevice_Infrastructure_t deviceInfra = nullptr;
    if (qnn->QNN_INTERFACE_VER_NAME.deviceGetInfrastructure(&deviceInfra) == QNN_SUCCESS) {
        QnnHtpDevice_Infrastructure_t* htpInfra = static_cast<QnnHtpDevice_Infrastructure_t*>(deviceInfra);
        auto& perfInfra = htpInfra->perfInfra; 

        uint32_t powerConfigId = 0;
        perfInfra.createPowerConfigId(0, 0, &powerConfigId);

        QnnHtpPerfInfrastructure_PowerConfig_t powerConfig;
        std::memset(&powerConfig, 0, sizeof(powerConfig));
        
        powerConfig.option = QNN_HTP_PERF_INFRASTRUCTURE_POWER_CONFIGOPTION_DCVS_V3;
        powerConfig.dcvsV3Config.dcvsEnable = 0;             
        powerConfig.dcvsV3Config.setDcvsEnable = 1;
        powerConfig.dcvsV3Config.contextId = powerConfigId;
        powerConfig.dcvsV3Config.powerMode = QNN_HTP_PERF_INFRASTRUCTURE_POWERMODE_PERFORMANCE_MODE; 
        powerConfig.dcvsV3Config.sleepDisable = 1;           
        powerConfig.dcvsV3Config.setSleepDisable = 1;

        const QnnHtpPerfInfrastructure_PowerConfig_t* configs[] = {&powerConfig, nullptr};
        perfInfra.setPowerConfig(powerConfigId, configs);
    }

    Qnn_ContextHandle_t contextHandle = nullptr;
    qnn->QNN_INTERFACE_VER_NAME.contextCreateFromBinary(
        backendHandle, deviceHandle, nullptr, contextBuffer, contextSize, &contextHandle, nullptr);
    Qnn_GraphHandle_t graphHandle = nullptr;
    qnn->QNN_INTERFACE_VER_NAME.graphRetrieve(contextHandle, graphName, &graphHandle);

    // --- 5. STREAMING CAMERA ---
    std::string gstCmd = "gst-launch-1.0 -q v4l2src device=/dev/video2 ! "
                         "video/x-raw,width=640,height=480,framerate=30/1 ! "
                         "videocrop left=80 right=80 ! "
                         "videoscale ! video/x-raw,width=224,height=224 ! "
                         "videoconvert ! video/x-raw,format=RGB ! "
                         "queue max-size-buffers=1 leaky=downstream ! fdsink";

    FILE* pipe = popen(gstCmd.c_str(), "r");
    if (!pipe) return -1;

    size_t frameBytes = 224 * 224 * 3;
    std::vector<uint8_t> rgbBuffer(frameBytes);
    int hw = 224 * 224;
    int frameCount = 0;

    std::cout << "[INFO] Bat dau Real-time Loop...\n";
    auto startTotal = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 100; ++i) {
        auto t_start = std::chrono::high_resolution_clock::now();
        size_t bytesRead = fread(rgbBuffer.data(), 1, frameBytes, pipe);
        if (bytesRead != frameBytes) continue;
        auto t_read = std::chrono::high_resolution_clock::now();

        // [TỰ ĐỘNG KHỚP KIỂU DỮ LIỆU INPUT]
        if (inTensor.v2.dataType == QNN_DATATYPE_UINT_8 || inTensor.v2.dataType == QNN_DATATYPE_INT_8) {
            uint8_t* inUint8 = static_cast<uint8_t*>(inData);
            for (int p = 0; p < hw; ++p) {
                inUint8[p]          = rgbBuffer[p * 3 + 0]; // Giữ nguyên giá trị 0-255
                inUint8[hw + p]     = rgbBuffer[p * 3 + 1];
                inUint8[2 * hw + p] = rgbBuffer[p * 3 + 2];
            }
        } else {
            float* inFloat = static_cast<float*>(inData);
            for (int p = 0; p < hw; ++p) {
                inFloat[p]          = rgbBuffer[p * 3 + 0] / 255.0f; // Chuyển sang 0.0 - 1.0
                inFloat[hw + p]     = rgbBuffer[p * 3 + 1] / 255.0f;
                inFloat[2 * hw + p] = rgbBuffer[p * 3 + 2] / 255.0f;
            }
        }
        
        auto t_prep = std::chrono::high_resolution_clock::now();

        qnn->QNN_INTERFACE_VER_NAME.graphExecute(
            graphHandle, &inTensor, 1, &outTensor, 1, nullptr, nullptr);
        auto t_npu = std::chrono::high_resolution_clock::now();

        frameCount++;

        if (i % 10 == 0) {
            double ms_read = std::chrono::duration<double, std::milli>(t_read - t_start).count();
            double ms_prep = std::chrono::duration<double, std::milli>(t_prep - t_read).count();
            double ms_npu  = std::chrono::duration<double, std::milli>(t_npu - t_prep).count();
            double ms_total = ms_read + ms_prep + ms_npu;
            
            std::printf("[Frame %2d] Total: %5.1f ms (~%.1f FPS) | Read: %4.1f ms | CPU Prep: %4.1f ms | NPU Exec: %4.1f ms\n", 
                        i, ms_total, 1000.0 / ms_total, ms_read, ms_prep, ms_npu);
        }
    }

    auto endTotal = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> totalElapsed = endTotal - startTotal;
    std::cout << "========================================\n";
    std::cout << ">> TONG TOC DO TRUNG BINH: " << (frameCount / totalElapsed.count()) << " FPS\n";
    std::cout << "========================================\n";

    pclose(pipe);
    free(inData); free(outData);
    qnn->QNN_INTERFACE_VER_NAME.contextFree(contextHandle, nullptr);
    qnn->QNN_INTERFACE_VER_NAME.deviceFree(deviceHandle);
    qnn->QNN_INTERFACE_VER_NAME.backendFree(backendHandle);
    qnnSys->QNN_SYSTEM_INTERFACE_VER_NAME.systemContextFree(sysCtxHandle);
    munmap(contextBuffer, contextSize);
    dlclose(sys_handle); dlclose(htp_handle);

    return 0;
}