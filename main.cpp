#include <iostream>
#include <dlfcn.h>
#include "QnnInterface.h" // Header cốt lõi của Qualcomm QNN

// Khai báo kiểu (typedef) cho hàm mỏ neo
typedef Qnn_ErrorHandle_t (*QnnInterfaceGetProvidersFn_t)(const QnnInterface_t*** providerList, uint32_t* numProviders);

int main() {
    std::cout << "========== KHOI DONG QNN API (VLA PI0.5) ==========" << std::endl;

    // 1. Mở thư viện NPU backend
    void* htp_handle = dlopen("libQnnHtp.so", RTLD_NOW | RTLD_LOCAL);
    if (!htp_handle) {
        std::cerr << "[ERROR] Khong the load libQnnHtp.so: " << dlerror() << std::endl;
        return -1;
    }

    // 2. Trích xuất con trỏ hàm mỏ neo
    auto getProvidersFn = (QnnInterfaceGetProvidersFn_t)dlsym(htp_handle, "QnnInterface_getProviders");
    if (!getProvidersFn) {
        std::cerr << "[ERROR] Loi tim ham QnnInterface_getProviders: " << dlerror() << std::endl;
        dlclose(htp_handle);
        return -1;
    }
    
    std::cout << "[INFO] Da tim thay ham mo neo QnnInterface_getProviders." << std::endl;

    // 3. Lấy bảng giao diện (Interface Table)
    const QnnInterface_t** interfaceProviders = nullptr;
    uint32_t numProviders = 0;
    
    Qnn_ErrorHandle_t err = getProvidersFn(&interfaceProviders, &numProviders);
    
    if (err != QNN_SUCCESS || numProviders == 0 || interfaceProviders == nullptr) {
        std::cerr << "[ERROR] Khong the lay QNN Interface!" << std::endl;
        dlclose(htp_handle);
        return -1;
    }

    // In ra kết quả thành công và phiên bản API
    std::cout << "[SUCCESS] Da lay thanh cong " << numProviders << " QNN Interface provider(s)!" << std::endl;
    std::cout << "[INFO] Phien ban Core API Backend: " 
              << interfaceProviders[0]->apiVersion.coreApiVersion.major << "." 
              << interfaceProviders[0]->apiVersion.coreApiVersion.minor << std::endl;

    std::cout << "[INFO] San sang thuc thi Graph tren Hexagon DSP!" << std::endl;

    dlclose(htp_handle);
    return 0;
}