#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "Wire.h"

/*
 * =========================================================================
 * MPU6050 基础库使用教程 (全功能数据显示版)
 * =========================================================================
 * 本教程演示如何读取 MPU6050 的所有核心数据。
 * 
 * 核心功能：
 * 1. 初始化 I2C 总线与 MPU6050 (DMP)
 * 2. 读取 DMP 解算后的姿态数据 (四元数 -> 欧拉角/YawPitchRoll)
 * 3. 读取 传感器原始 6轴数据 (加速度计 + 陀螺仪)
 * 4. 串口实时打印所有信息，方便调试和观察
 * 
 * 串口输出说明 (制表符分隔):
 * ypr[Yaw, Pitch, Roll]  acc[Ax, Ay, Az]  gyro[Gx, Gy, Gz]
 * 
 * 硬件连接：
 * ESP32 SDA -> MPU6050 SDA (GPIO 8)
 * ESP32 SCL -> MPU6050 SCL (GPIO 9)
 * VCC -> 3.3V
 * GND -> GND
 */

// -------------------------------------------------------------------------
// 全局对象声明
// -------------------------------------------------------------------------
MPU6050 mpu;           // MPU6050 驱动实例

// -------------------------------------------------------------------------
// MPU6050 状态控制变量
// -------------------------------------------------------------------------
bool dmpReady = false;      // DMP 初始化完成标志
uint16_t packetSize;        // DMP 数据包大小 (42 bytes)
uint8_t fifoBuffer[64];     // FIFO 读取缓冲区

// DMP 解算相关变量
/* 
 * -------------------------------------------------------------------------
 * 关于 Yaw, Pitch, Roll (欧拉角) 的说明
 * -------------------------------------------------------------------------
 * 这里的定义基于飞行动力学标准，但在 MPU6050 DMP 中通常如下：
 * 
 * 1. Yaw (偏航角) - 绕 Z 轴旋转
 *    - 在水平面上旋转（像指南针一样）。
 *    - 范围通常是 -180 到 +180 度。
 *    - 注意：MPU6050 没有磁力计，Yaw 值会随时间缓慢漂移 (Drift)，无法指示绝对北方。
 * 
 * 2. Pitch (俯仰角) - 绕 Y 轴旋转
 *    - 像飞机抬头或低头。
 *    - 也就是模块的前后倾斜。
 * 
 * 3. Roll (横滚角) - 绕 X 轴旋转
 *    - 像飞机机翼左右倾斜。
 *    - 也就是模块的左右翻滚。
 * 
 * 坐标系方向 (取决于模块默认安装方向):
 * 通常 X轴指向前方，Y轴指向右方，Z轴垂直。
 * -------------------------------------------------------------------------
 */
Quaternion q;           // [w, x, y, z]         四元数容器
VectorFloat gravity;    // [x, y, z]            重力向量
float ypr[3];           // [yaw, pitch, roll]   欧拉角 (单位: 弧度)

// 运动/姿态解算变量
VectorInt16 aa;         // [x, y, z]            加速度传感器测量值
VectorInt16 aaReal;     // [x, y, z]            去重力加速度 (去除重力分量)
VectorInt16 aaWorld;    // [x, y, z]            世界坐标系加速度 (考虑旋转后的运动分量)

// 原始 6轴数据变量
int16_t ax, ay, az;     // 加速度计原始值
int16_t gx, gy, gz;     // 陀螺仪原始值

// -------------------------------------------------------------------------
// 函数原型声明
// -------------------------------------------------------------------------
void initMPU();
void mpuLoop();

/**
 * @brief Arduino 程序的初始化入口
 */
void setup() {
    Serial.begin(115200); 
    delay(1000);
    Serial.println("\n=== MPU6050 Full Info Demo Start ===");
    Serial.println("Output Format:");
    Serial.println("ypr[yaw,pitch,roll]\t| world_acc[x,y,z]\t| raw_acc[ax,ay,az]\t| raw_gyro[gx,gy,gz]");

    // 1. 初始化 MPU6050 传感器 (含 DMP 加载)
    initMPU();      
    
    Serial.println("System Ready. Outputting data...");
}

/**
 * @brief Arduino 主循环
 */
void loop() {
    // 只要 DMP 准备好，就不断尝试读取数据
    if (dmpReady) {
        mpuLoop();
    }
    
    // 简单的延时，不宜过长
    delay(10); 
}

// =========================================================================
// MPU6050 核心逻辑
// =========================================================================

/**
 * @brief 初始化 MPU6050 及其 DMP 引擎
 */
void initMPU() {
    // 步骤 1: I2C 初始化
    // ESP32-C3/S3 等板子可自定义引脚，这里使用 SDA=8, SCL=9
    Wire.begin(8, 9);
    Wire.setClock(400000); // 推荐使用 400kHz

    Serial.println("Initializing MPU6050...");
    mpu.initialize();
    
    // 检查连接是否成功
    if (!mpu.testConnection()) {
        Serial.println("MPU6050 connection failed!");
        while(1); 
    }
    Serial.println("MPU6050 connection successful");

    // 步骤 2: 初始化 DMP
    Serial.println("Initializing DMP...");
    uint8_t devStatus = mpu.dmpInitialize();
    
    // 在这里添加您的偏移量校准数据 (如果需要更精准的 DMP)
    // mpu.setXGyroOffset(0);
    // mpu.setYGyroOffset(0);
    // mpu.setZGyroOffset(0);
    // mpu.setZAccelOffset(1688); 
    
    if (devStatus == 0) {
        // 开启 DMP
        mpu.setDMPEnabled(true);
        dmpReady = true;
        
        packetSize = mpu.dmpGetFIFOPacketSize();
        Serial.print("DMP ready! Packet size: ");
        Serial.println(packetSize);
    } else {
        Serial.print("DMP Initialization failed (code ");
        Serial.print(devStatus);
        Serial.println(")");
    }
}

/**
 * @brief 在 Loop 中调用的 MPU 处理函数
 *        同时读取 DMP 融合姿态 和 传感器原始数据
 */
void mpuLoop() {
    // 获取当前 FIFO 字节数
    uint16_t fifoCount = mpu.getFIFOCount();

    // 异常处理：FIFO 溢出
    if (fifoCount == 1024) {
        mpu.resetFIFO();
        Serial.println("FIFO overflow!");
        return;
    }

    // 等待数据，不阻塞
    if (fifoCount < packetSize) {
        return;
    }

    // 读取最新数据包
    while (fifoCount >= packetSize) {
        mpu.getFIFOBytes(fifoBuffer, packetSize);
        fifoCount -= packetSize;
    }

    // =====================================================================
    // 1. 获取 原始 6轴信息 (Accel + Gyro)
    // =====================================================================
    // 注意：getMotion6 读取的是当前寄存器的瞬时值，可能与 DMP 数据包有微小时间差
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    // =====================================================================
    // 2. 获取 DMP 解算信息 (Yaw, Pitch, Roll) & 运动分量
    // =====================================================================
    mpu.dmpGetQuaternion(&q, fifoBuffer);           // 从 FIFO 数据包解析四元数 (Quat)
    mpu.dmpGetAccel(&aa, fifoBuffer);               // 从 FIFO 解析加速度 (Accel)
    mpu.dmpGetGravity(&gravity, &q);                // 基于四元数计算重力向量 (Gravity)
    mpu.dmpGetLinearAccel(&aaReal, &aa, &gravity);  // 去除重力，计算物体坐标系的线性加速度 (Linear Accel)
    
    // 如果库版本缺失 dmpGetLinearAccelInWorld，使用手动旋转代替
    // 将物体坐标系的线性加速度旋转至世界坐标系
    aaWorld.x = aaReal.x;
    aaWorld.y = aaReal.y;
    aaWorld.z = aaReal.z;
    aaWorld.rotate(&q);                             // 旋转至世界坐标系 (World Accel)
    
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);      // 从四元数和重力计算欧拉角 (Yaw, Pitch, Roll)

    // =====================================================================
    // 3. 串口打印
    // =====================================================================
    // 打印 Yaw, Pitch, Roll (单位：角度)
    Serial.print("ypr\t");
    Serial.print(ypr[0] * 180/M_PI); 
    Serial.print("\t");
    Serial.print(ypr[1] * 180/M_PI);
    Serial.print("\t");
    Serial.print(ypr[2] * 180/M_PI);
    
    Serial.print("\tworld\t");
    Serial.print(aaWorld.x);
    Serial.print("\t");
    Serial.print(aaWorld.y);
    Serial.print("\t");
    Serial.print(aaWorld.z);
    
    // 打印 加速度计原始值
    Serial.print("\tacc\t");
    Serial.print(ax);
    Serial.print("\t");
    Serial.print(ay);
    Serial.print("\t");
    Serial.print(az);

    // 打印 陀螺仪原始值
    Serial.print("\tgyro\t");
    Serial.print(gx);
    Serial.print("\t");
    Serial.print(gy);
    Serial.print("\t");
    Serial.println(gz);
}