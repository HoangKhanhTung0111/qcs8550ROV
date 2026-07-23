# qcs8550ROV

# 1. Docker run và truy cập vào docker
- docker run -d --name rov-builder -v ${PWD}:/workspace qcs8550-cross-env
- docker exec -it rov-builder bash

# 2. Kiểm tra .h và .so

```
# 1. Tìm vị trí chính xác của thư mục chứa các file Header của QNN
find /opt/qcom/qirp-sdk -type d -name "QNN" 2>/dev/null

# 2. Liệt kê các thư viện lõi của nhánh gcc11.2 để đảm bảo các file .so tồn tại
ls -l /opt/qcom/qirp-sdk/lib/aarch64-oe-linux-gcc11.2 | grep -i qnn
```

# 3. Kéo bằng scp

```
scp -r root@192.168.5.122:/opt/qcom/qirp-sdk/include ./qnn_sysroot/
scp -r root@192.168.5.122:/opt/qcom/qirp-sdk/lib/aarch64-oe-linux-gcc11.2 ./qnn_sysroot/lib/
```

# 4. cmake

```
mkdir -p build && cd build cmake .. make -j4
```

# 5. scp sang board
```
# Đẩy file thực thi sang thư mục /home/hkt/ của board
scp build/rov_vla_app root@192.168.5.122:/home/hkt/
```

# 6. Vào board chạy
```
cd /home/hkt/
chmod +x rov_vla_app

# Đừng quên khai báo đường dẫn thư viện cho CPU trước khi chạy (nếu lỡ tắt terminal cũ)
export LD_LIBRARY_PATH=/opt/qcom/qirp-sdk/lib/aarch64-oe-linux-gcc11.2:$LD_LIBRARY_PATH

./rov_vla_app
```