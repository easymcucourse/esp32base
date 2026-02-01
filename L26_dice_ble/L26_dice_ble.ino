#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "Wire.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

// BLE UUIDs
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// 全局实例
MPU6050 mpu;           // MPU6050传感器对象
Preferences prefs;     // 偏好设置，用于Flash读写

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// MPU6050 相关状态变量
bool dmpReady = false;      // DMP初始化成功标志
uint16_t packetSize;        // DMP数据包大小
uint8_t fifoBuffer[64];     // FIFO缓冲区

// 数据交换结构
struct {
    int16_t w, x, y, z;     // 存储放大100倍后的四元数整数值
} q_int;
SemaphoreHandle_t xGuiSemaphore = xSemaphoreCreateMutex(); // 互斥信号量，保护I2C访问和全局数据安全

// 均值滤波缓冲区 (保存最近5组数据，放大100倍存储以避免浮点开销)
int16_t history_w[5] = {100,0,0,0,0}, history_x[5]={0}, history_y[5]={0}, history_z[5]={0};
int history_idx = 0; // 滑动窗口当前索引

// 函数声明
void mpuTask(void *pvParameters);
void updateMPUData();
bool performFullCalibration();
void getQuaternionInt(const uint8_t* packet);
void initMPU();
void initBLE();
void saveToFlash();
void loadFromFlash();

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

/**
 * @brief 系统入口，负责核心硬件调度
 */
void setup() {
    Serial.begin(115200); 
    delay(1000);
    
    // 模块化初始化
    initMPU();        // 传感器及后台采样任务初始化
    initBLE();        // BLE 初始化
    
    Serial.println("Setup Complete. BLE Advertising...");
}

/**
 * @brief 初始化MPU6050传感器
 */
void initMPU() {
    // 初始化I2C总线，引脚映射为 SDA=8, SCL=9
    Wire.begin(8, 9);
    Wire.setClock(400000); // 设置I2C频率为400kHz

    Serial.println("Initializing MPU6050...");
    mpu.initialize();
    
    // 从Flash加载之前保存的校准偏移量
    loadFromFlash();
    
    Serial.println("Initialing DMP...");
    // 初始化DMP三轴解算引擎
    uint8_t devStatus = mpu.dmpInitialize();
    
    if (devStatus == 0) {
        Serial.println("DMP initialized successfully.");
        mpu.setDMPEnabled(true);
        dmpReady = true;
        packetSize = mpu.dmpGetFIFOPacketSize();
        Serial.print("DMP ready. Packet size: ");
        Serial.println(packetSize);
    } else {
        Serial.print("DMP Initialization failed (code ");
        Serial.print(devStatus);
        Serial.println(")");
    }

    // 创建后台高优先级采样任务 (Pin到核心0或1自动分配)
    Serial.println("Starting MPU Background Task...");
    xTaskCreatePinnedToCore(mpuTask, "MPUTask", 4096, NULL, 1, NULL, tskNO_AFFINITY);
}

class MyCharacteristicCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      if (value.length() > 0) {
        Serial.println("*********");
        Serial.print("New value: ");
        for (int i = 0; i < value.length(); i++)
          Serial.print(value[i]);
        Serial.println();
        Serial.println("*********");

        // 如果收到 "calibrate" 指令，执行校准
        if (value == "calibrate") {
          performFullCalibration();
        }
      }
    }
};

/**
 * @brief 初始化BLE服务
 */
void initBLE() {
    // 创建 BLE 设备
    BLEDevice::init("ESP32_Dice_BLE");

    // 创建 BLE 服务端
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // 创建 BLE 服务
    BLEService *pService = pServer->createService(SERVICE_UUID);

    // 创建 BLE 特征
    pCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_READ   |
                        BLECharacteristic::PROPERTY_WRITE  |
                        BLECharacteristic::PROPERTY_NOTIFY |
                        BLECharacteristic::PROPERTY_INDICATE
                      );

    // 设置特征回调
    pCharacteristic->setCallbacks(new MyCharacteristicCallbacks());

    // 创建 BLE 描述符
    pCharacteristic->addDescriptor(new BLE2902());

    // 启动服务
    pService->start();

    // 开始广播
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(false);
    pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
    pServer->getAdvertising()->start();
    Serial.println("Waiting for a client connection to notify...");
}

/**
 * @brief FreeRTOS 任务：以固定频率轮询 MPU6050 FIFO
 */
void mpuTask(void *pvParameters) {
    while(1) {
        updateMPUData();

        // 如果设备已连接，通过 BLE 发送数据
        if (deviceConnected) {
            // 将结构体数据作为字节数组发送
            pCharacteristic->setValue((uint8_t*)&q_int, sizeof(q_int));
            pCharacteristic->notify();
        }

        vTaskDelay(pdMS_TO_TICKS(25)); // 40Hz (25ms) 更新率，兼顾流畅度与性能
    }
}

/**
 * @brief 读取传感器并更新滑动平均缓冲区
 */
void updateMPUData() {
    if (!dmpReady) return;
    
    // I2C 访问锁，防止采样任务冲突
    if (xSemaphoreTake(xGuiSemaphore, pdMS_TO_TICKS(10)) != pdTRUE) return;

    uint16_t fifoCount = mpu.getFIFOCount();
    if (fifoCount == 1024) {
        mpu.resetFIFO();
        xSemaphoreGive(xGuiSemaphore);
        return;
    }

    if (fifoCount < packetSize) {
        xSemaphoreGive(xGuiSemaphore);
        return;
    }

    // 清空FIFO，只读取最新的一组数据包
    while (fifoCount >= packetSize) {
        mpu.getFIFOBytes(fifoBuffer, packetSize);
        fifoCount -= packetSize;
    }

    // 解析原始数据存入历史缓冲区
    getQuaternionInt(fifoBuffer);
    history_idx = (history_idx + 1) % 5;

    // 计算滑动平均值，平滑数据跳动
    int32_t sum_w=0, sum_x=0, sum_y=0, sum_z=0;
    for(int i=0; i<5; i++) {
        sum_w += history_w[i]; sum_x += history_x[i];
        sum_y += history_y[i]; sum_z += history_z[i];
    }

    // 更新全局待分发数据
    q_int.w = (int16_t)(sum_w / 5);
    q_int.x = (int16_t)(sum_x / 5);
    q_int.y = (int16_t)(sum_y / 5);
    q_int.z = (int16_t)(sum_z / 5);

    xSemaphoreGive(xGuiSemaphore);
}

/**
 * @brief 直接从 DMP 原始数据提取四元数并存入历史缓冲区 (放大100倍)
 * @param packet 原始 16 字节 DMP 数据
 */
void getQuaternionInt(const uint8_t* packet) {
    // DMP 输出缩放比例为 1.0 = 2^14 = 16384
    // 提取高16位有效数据
    int16_t w = (int16_t)((packet[0] << 8) | packet[1]);
    int16_t x = (int16_t)((packet[4] << 8) | packet[5]);
    int16_t y = (int16_t)((packet[8] << 8) | packet[9]);
    int16_t z = (int16_t)((packet[12] << 8) | packet[13]);

    // 定点运算：(raw * 100) / 16384 -> 相当于 *100 后右移 14 位
    history_w[history_idx] = (int16_t)(((int32_t)w * 100) >> 14);
    history_x[history_idx] = (int16_t)(((int32_t)x * 100) >> 14);
    history_y[history_idx] = (int16_t)(((int32_t)y * 100) >> 14);
    history_z[history_idx] = (int16_t)(((int32_t)z * 100) >> 14);
}

/**
 * @brief 执行完整的硬件零偏校准
 * @return true 成功，false 失败（通信冲突等）
 */
bool performFullCalibration() {
    bool success = false;
    if (xSemaphoreTake(xGuiSemaphore, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    
    Serial.println(">>> 正在执行完整硬件复位校准...");
    dmpReady = false; 
    mpu.setDMPEnabled(false);
    delay(100);
    
    mpu.reset();
    delay(500); 

    mpu.initialize();
    delay(100);

    if (mpu.testConnection()) {
        uint8_t devStatus = mpu.dmpInitialize();
        if (devStatus == 0) {
            // 校准时需将模块水平静止平放（Z轴向上）
            mpu.CalibrateGyro(15);
            mpu.CalibrateAccel(15); 
            
            saveToFlash(); // 将结果持久化
            
            mpu.setDMPEnabled(true);
            packetSize = mpu.dmpGetFIFOPacketSize();
            mpu.resetFIFO();
            dmpReady = true;
            success = true;
            Serial.println(">>> 复位校准成功，DMP已重启");
            Serial.println(">>> 注意：请确保校准时模块处于水平静止平放状态。");
        }
    }

    xSemaphoreGive(xGuiSemaphore);
    return success;
}

/**
 * @brief 将当前 MPU6050 的硬件偏移量存入 Flash
 */
void saveToFlash() {
    prefs.begin("mpu_cfg", false);
    prefs.putShort("ax", mpu.getXAccelOffset());
    prefs.putShort("ay", mpu.getYAccelOffset());
    prefs.putShort("az", mpu.getZAccelOffset());
    prefs.putShort("gx", mpu.getXGyroOffset());
    prefs.putShort("gy", mpu.getYGyroOffset());
    prefs.putShort("gz", mpu.getZGyroOffset());
    prefs.end();
}

/**
 * @brief 从 Flash 读取偏移量并应用到传感器
 */
void loadFromFlash() {
    prefs.begin("mpu_cfg", true);
    mpu.setXAccelOffset(prefs.getShort("ax", 0));
    mpu.setYAccelOffset(prefs.getShort("ay", 0));
    mpu.setZAccelOffset(prefs.getShort("az", 0));
    mpu.setXGyroOffset(prefs.getShort("gx", 0));
    mpu.setYGyroOffset(prefs.getShort("gy", 0));
    mpu.setZGyroOffset(prefs.getShort("gz", 0));
    prefs.end();
}

/**
 * @brief 主循环，处理 BLE 重连
 */
void loop() {
    // disconnecting
    if (!deviceConnected && oldDeviceConnected) {
        delay(500); // give the bluetooth stack the chance to get things ready
        pServer->startAdvertising(); // restart advertising
        Serial.println("Start advertising");
        oldDeviceConnected = deviceConnected;
    }
    // connecting
    if (deviceConnected && !oldDeviceConnected) {
        // do stuff here on connecting
        oldDeviceConnected = deviceConnected;
    }
}