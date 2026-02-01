/*
 * =================================================================
 * ESP32 内存管理 - 演示代码
 * =================================================================
 * YouTube频道：轻松易学嵌入式
 * * 本代码根据文案内容，通过实际操作演示ESP32的几种主要存储空间：
 * 1. Flash (闪存): 存放常量数据 (const)
 * 2. SRAM (静态内存): 存放全局变量和静态变量
 * 3. Stack (栈): 存放函数局部变量 (演示栈溢出)
 * 4. Heap (堆): 用于动态内存分配 (malloc)，演示SRAM和PSRAM的协同工作
 * * 硬件要求: ESP32 开发板 (推荐使用带PSRAM的型号以获得完整体验)
 * * 操作指南:
 * 1. 在Arduino IDE中选择正确的ESP32开发板型号和端口。
 * 2. 上传代码。
 * 3. 打开串口监视器 (波特率设置为 115200) 查看输出。
 * 4. 按照代码中的注释提示，可以尝试取消注释某些行来观察编译错误或运行时崩溃。
 * =================================================================
 */

// === 演示 1: Flash (闪存) ===
// 使用 'const' 关键字定义的常量数据会被编译到 Flash 中，不占用宝贵的SRAM。
// 这是一个 10KB 的常量数组，它存在于Flash中。
const uint8_t dataInFlash[10 * 1024] = {0};


// --- 挑战极限 (会导致编译失败) ---
// 如果我们定义一个超大的常量数组，例如100MB，而你的应用分区空间不足100MB，
// 编译器会直接报错，提示 ".rodata" 段溢出。
// 这证明了常量数据确实存放在Flash的应用空间里。
//
//const uint8_t largedataInFlash[100 * 1024 * 1024] = { 0xee }; 
const uint8_t largedataInFlash[1 *1024 * 1024] = {0};

// === 演示 2: SRAM (静态内存) ===
// 全局变量和静态变量(static)会存放在内部SRAM中。
// 这是一个 4KB 的全局数组，它会占用SRAM空间。
uint8_t dataInSram[4 * 1024];


// --- 挑战极限 (会导致编译失败) ---
// ESP32-S3 拥有 512KB SRAM。如果我们定义一个超过SRAM大小的全局数组，例如 600KB，
// 链接器在链接阶段就会报错，提示 SRAM 溢出。
// 注意：全局变量和静态变量无法直接定义在PSRAM上。
//
//uint8_t dataInSram[600 * 1024] = {0}; // 取消注释会导致编译错误



void setup() {
  // 初始化串口通讯，波特率为 115200
  Serial.begin(115200);
  delay(2000); // 等待串口监视器打开

  // 按要求输出频道名称
  Serial.println("\n\n=============================================");
  Serial.println("      YouTube频道：轻松易学嵌入式");
  Serial.println("=============================================\n");
  Serial.println("ESP32 内存管理演示开始...\n");

  // --- 验证 Flash 和 SRAM ---
  Serial.println("--- 演示 1 & 2: Flash 和 SRAM ---");
  Serial.printf("常量数组 'dataInFlash' (10KB) 已成功定义在 Flash 中。%d",dataInFlash);
  Serial.printf("全局数组 'dataInSram' (4KB) 已成功定义在 SRAM 中。%d",dataInSram);
  Serial.println("您可以尝试取消代码中超大数组的注释，来观察编译错误。\n");
  
  // 打印当前可用的堆内存信息
  Serial.printf("初始化后，总可用堆内存(SRAM+PSRAM): %d bytes\n", ESP.getFreeHeap());
  Serial.printf("其中，内部SRAM可用: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  if (psramFound()) {
    Serial.printf("外部PSRAM可用: %d bytes\n\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  } else {
    Serial.println("未找到或未启用 PSRAM。\n");
  }


  // --- 演示 3: Stack (栈) 溢出 ---
  Serial.println("--- 演示 3: Stack (栈) ---");
  Serial.println("接下来将调用一个函数，该函数会尝试在栈上定义一个巨大的局部变量。");
  Serial.println("注意：这很可能会导致设备崩溃和无限重启！");
  Serial.println("为了安全，实际的调用已被注释掉。");
  
  //demonstrateStackOverflow(); // <--- !!! 警告 !!! 取消此行注释将导致栈溢出，设备会崩溃重启！
  Serial.println("演示函数 'demonstrateStackOverflow()' 已被注释，以防止崩溃。\n");
  
  
  // --- 演示 4: Heap (堆) - 动态内存分配 ---
  Serial.println("--- 演示 4: Heap (堆) ---");
  Serial.println("我们将尝试用 malloc() 分配一块 512KB 的内存。");
  Serial.println("在有PSRAM的情况下，这通常会成功。如果没有PSRAM，则会失败。\n");
  
  // 尝试分配一块很大的内存 (512 KB)
  size_t allocationSize = 512 * 1024;
  void* largeMemoryBlock = malloc(allocationSize);
  
  if (largeMemoryBlock != NULL) {
    Serial.printf("成功！从堆上分配了 %d KB 的内存。\n", allocationSize / 1024);
    Serial.println("这块内存很可能来自于 PSRAM。");
    // 使用完内存后，必须释放它
    free(largeMemoryBlock);
    Serial.println("内存已释放。\n");
  } else {
    Serial.printf("失败！无法从堆上分配 %d KB 的内存。\n", allocationSize / 1024);
    Serial.println("这通常是因为内部SRAM没有足够大的连续空间，并且没有启用PSRAM。\n");
  }


  // --- 总结 ---
  Serial.println("--- 今日知识点总结 ---");
  Serial.println("1. 常量数据 (const): 存放在 Flash，编译时确定，节省SRAM。");
  Serial.println("2. 全局/静态变量: 存放在内部 SRAM，大小受SRAM物理限制。");
  Serial.println("3. 函数局部变量: 存放在 栈(Stack)，大小受任务栈限制，严防溢出导致崩溃。");
  Serial.println("4. 动态内存 (malloc): 存放在 堆(Heap)，是SRAM和PSRAM的结合体，是分配大块内存的正确方式。");
  Serial.println("\n演示结束。");
  Serial.println("=============================================");
}

void loop() {
  // --- 测试：读取 Flash 中的数据 ---
  Serial.print("Flash 起始数据 (largedataInFlash[0]): 0x");
  Serial.println(largedataInFlash[0], HEX);
  
  Serial.print("Flash 末尾数据 (largedataInFlash[最后]): 0x");
  Serial.println(largedataInFlash[sizeof(largedataInFlash) - 1], HEX);
  
  delay(5000); 
}



// 演示栈溢出的函数
void demonstrateStackOverflow() {
  Serial.println("进入 'demonstrateStackOverflow' 函数...");
  Serial.println("尝试在栈上创建一个 500KB 的局部数组...");
  
  // 在函数内部定义一个巨大的数组。任务栈默认大小通常只有几KB (例如8KB)。
  // 如此大的局部变量会立刻耗尽所有栈空间，导致“栈溢出”。
  // 其结果就是程序崩溃，看门狗定时器(Watchdog)会触发系统重启。
  volatile uint8_t largeLocalArray[500 * 1024]; 
  
  // 如果程序能运行到这里 (实际上不可能)，我们打印一条消息
  // 但由于栈溢出，程序在此之前就已经崩溃了。
  largeLocalArray[0] = 1; // 尝试访问一下这个数组
  Serial.println("...如果能看到这条消息，说明没有发生栈溢出 (奇迹！)");
}

