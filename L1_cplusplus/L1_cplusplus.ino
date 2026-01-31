// 编译器演示代码

#include <Arduino.h>
#include <vector>
#include <string>
#include <numeric>
#include <algorithm>
#include <ranges> // C++20 ranges a

// YouTube 频道：轻松易学嵌入式

// C++20 特性：指定初始化
// 允许我们使用 .member = value 的语法来初始化聚合体
struct Point {
    int x;
    int y;
};

// C++20 特性：三路比较运算符 <=> (spaceship operator)
// 自动为我们生成 ==, !=, <, <=, >, >= 运算符
struct Point3D {
    int x;
    int y;
    int z;

    auto operator<=>(const Point3D& other) const = default;
};

void setup() {
  Serial.begin(115200);
  delay(5000); // 等待串口监视器打开

  // YouTube 频道：轻松易学嵌入式
  Serial.println("\n--- 编译器及 C++ 版本信息 ---");
  
  // 最开始输出当前 gcc 版本和 c++ 版本
  Serial.print("GCC Version: ");
  Serial.println(__VERSION__); // 输出 GCC 版本号
  
  Serial.print("C++ Standard: ");
  Serial.println(__cplusplus); // 输出 __cplusplus 宏的值
  
  // 根据 __cplusplus 的值判断 C++ 标准
  if (__cplusplus == 202002L) Serial.println("-> C++20 Standard");
  else if (__cplusplus == 201703L) Serial.println("-> C++17 Standard");
  else if (__cplusplus == 201402L) Serial.println("-> C++14 Standard");
  else if (__cplusplus == 201103L) Serial.println("-> C++11 Standard");
  else Serial.println("-> Pre-C++11 Standard");
  
  Serial.println("------------------------------------");

  // 输出 "YouTube 频道：轻松易学嵌入式" 演示
  Serial.println("\nYouTube 频道：轻松易学嵌入式演示");
  Serial.println("--- C++20 特性演示 ---");

  // 1. 指定初始化 (Designated Initializers)
  Serial.println("\n1. 指定初始化:");
  Point p{ .x = 10, .y = 20 };
  Serial.print("Point p = { .x = ");
  Serial.print(p.x);
  Serial.print(", .y = ");
  Serial.print(p.y);
  Serial.println(" }");

  // 2. 范围-based for 循环与初始化器 (Range-based for loop with initializer)
  Serial.println("\n2. 范围-based for 循环:");
  std::vector<int> numbers = {1, 2, 3, 4, 5};
  for (int sum = 0; auto i : numbers) {
    sum += i;
    Serial.print("Current sum: ");
    Serial.println(sum);
  }

  // 3. 三路比较运算符 (Three-way comparison operator)
  Serial.println("\n3. 三路比较运算符 (<=>):");
  Point3D p1{1, 2, 3};
  Point3D p2{1, 2, 3};
  Point3D p3{4, 5, 6};

  Serial.print("p1 == p2: ");
  Serial.println((p1 == p2) ? "true" : "false");
  Serial.print("p1 != p3: ");
  Serial.println((p1 != p3) ? "true" : "false");
  Serial.print("p1 < p3: ");
  Serial.println((p1 < p3) ? "true" : "false");


  // 4. std::string::starts_with 和 std::string::ends_with
  Serial.println("\n4. std::string::starts_with 和 std::string::ends_with:");
  std::string text = "Hello, ESP32 world!";
  Serial.print("Text: '");
  Serial.print(text.c_str());
  Serial.println("'");
  Serial.print("Text starts with 'Hello': ");
  Serial.println(text.starts_with("Hello") ? "true" : "false");
  Serial.print("Text ends with 'world!': ");
  Serial.println(text.ends_with("world!") ? "true" : "false");


  // 5. 使用 ranges 过滤和转换
  // 注意：这需要更完整的 C++20 ranges 支持，在某些 ESP32 Arduino 核心中可能不可用。
  // 这是一个概念演示。
  Serial.println("\n5. Ranges (概念演示):");
  Serial.println("Even numbers doubled (conceptual):");
  // 下面的代码需要编译器完全支持 C++20 ranges 才能编译
  for(int n : numbers | std::views::filter([](int n){ return n % 2 == 0; })
                      | std::views::transform([](int n){ return n * 2; })) {
    Serial.println(n);
  }
  Serial.println("Ranges in this environment might require specific setup to work.");
  Serial.println("--- 演示结束 ---");

  // YouTube 频道：轻松易学嵌入式
}

void loop() {
  // 主循环为空，因为所有演示都在 setup() 中完成
}