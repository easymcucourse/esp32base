
#include <Adafruit_GFX.h> 
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include "efontEnableAll.h"
#include "efont.h"

#define TFT_CS          (10)  // CS
#define TFT_RST         (8)  // Reset 
#define TFT_DC          (9)  // DC
#define TFT_MOSI        (7) // MOSI
#define TFT_SCK         (6)  // SCK

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// =================================================================
//
// 核心函数：printEfont (自定义的中文显示函数)
//
// 功能：在屏幕的指定位置，以指定的大小和颜色，显示UTF-8编码的中文字符串。
//
// 参数说明：
//   - char *str:          要显示的字符串 (例如 "你好")。必须是UTF-8编码。
//   - int16_t textsize:   文字的放大倍数 (1表示原始大小16x16, 2表示32x32, 以此类推)。
//   - uint16_t textcolor:  文字的颜色 (例如 ST77XX_RED 红色)。
//   - uint16_t textbgcolor:文字的背景色 (例如 ST77XX_BLACK 黑色)。
//
// =================================================================
void printEfont(char *str, int16_t textsize, uint16_t textcolor, uint16_t textbgcolor) {
  // 从tft对象获取当前的绘图起始点（光标位置）
  int16_t posX = tft.getCursorX();
  int16_t posY = tft.getCursorY();

  // 定义一个32字节的缓冲区，用于临时存储一个汉字的点阵数据。
  // 一个16x16像素的汉字，每行16个点（2字节），共16行，所以需要 2 * 16 = 32字节。
  byte font[32]; 

  // --- 逐字处理循环 ---
  // 只要当前字符不是字符串的结束符 `\0`，就一直循环。
  while ( *str != '\0' ) {
    
    // 检查当前字符是否为换行符 `\n`
    if ( *str == '\n' ) {
      posY += 16 * textsize; // Y坐标移动到下一行（一个汉字的高度是16像素 * 放大倍数）
      posX = 0;             // X坐标回到屏幕最左边
      str++;                // 字符串指针指向下一个字符
      continue;             // 跳过本次循环的剩余部分，直接开始下一次循环
    }

    // --- 汉字编码转换与数据获取 ---
    uint16_t strUTF16;
    // 调用efont库函数，将UTF-8编码的字符转换为UTF-16编码。
    // 因为字库是基于UTF-16索引的。函数会返回指向下一个未处理字符的指针。
    str = efontUFT8toUTF16( &strUTF16, str );
    // 使用转换后的UTF-16码，从字库中查找并获取对应的点阵数据，存入font缓冲区。
    getefontData( font, strUTF16 );

    // --- 判断字符宽度（全角/半角） ---
    int16_t width;
    if ( strUTF16 < 0x0100 ) {
      // 如果编码小于 U+0100，通常是英文字母、数字等ASCII字符（半角）。
      // 半角字符的宽度是8像素。
      width = 8 * textsize;
    } else {
      // 否则是中文字符或其他符号（全角）。
      // 全角字符的宽度是16像素。
      width = 16 * textsize;
    }
    
    // --- 自动换行处理 ---
    // 检查如果绘制这个字符，会不会超出屏幕的右边界。
    if ( tft.width() < posX + width ) {
      posY += 16 * textsize; // 如果会超出，则换行
      posX = 0;             // X坐标回到行首
    }

    // --- 绘制背景和文字 ---
    // 用指定的背景色填充该字符将要占据的区域，这样可以覆盖掉之前的图像。
    tft.fillRect(posX, posY, width, 16 * textsize, textbgcolor);

    // --- 逐点绘制循环 ---
    // 外层循环：逐行扫描16x16的点阵。
    for (uint8_t row = 0; row < 16; row++) {
      // 从font缓冲区中读取一行的点阵数据（16个点 = 2个字节）。
      // 将两个8位的字节合并成一个16位的字。
      word fontdata = font[row * 2] * 256 + font[row * 2 + 1];
      
      // 内层循环：逐列（逐个像素点）扫描。
      for (uint8_t col = 0; col < 16; col++) {
        
        // 使用位运算检查当前点是否需要绘制。
        // `0x8000 >> col` 会生成一个只有一位是1的掩码，从左到右移动。
        // `& fontdata` 操作可以判断fontdata中对应的位是否为1。
        if ( (0x8000 >> col) & fontdata ) {
          // 如果对应的位是1，说明这个点需要用前景色绘制。
          int16_t drawX = posX + col * textsize;
          int16_t drawY = posY + row * textsize;
          
          if ( textsize == 1 ) {
            // 如果放大倍数是1，直接画一个像素点，效率最高。
            tft.drawPixel(drawX, drawY, textcolor);
          } else {
            // 如果需要放大，就画一个 `textsize x textsize` 大小的实心矩形来模拟一个放大的像素点。
            tft.fillRect(drawX, drawY, textsize, textsize, textcolor);
          }
        }
      }
    }

    // 更新X坐标，为绘制下一个字符做准备。
    posX += width;
  }

  // 整个字符串绘制完毕后，更新tft对象的内部光标位置。
  // 这样，下次调用Adafruit库的绘图函数时，就会从正确的位置开始。
  tft.setCursor(posX, posY);
}

void setup(void) 
{
  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);  // 初始化SPI通信，并指定自定义的引脚。
                                            
  tft.initR(INITR_BLACKTAB);        // 初始化ST7735屏幕驱动，'INITR_BLACKTAB'是屏幕类型的一种，
                                   // 如果显示不正常，可以尝试 'INITR_GREENTAB' 或 'INITR_REDTAB'。        
  
  tft.fillScreen(ST77XX_BLACK);    // 用纯黑色填充整个屏幕，作为初始背景。

  tft.setRotation(1);     // 设置屏幕旋转方向。1和3是横屏，0和2是竖屏。                   
  tft.setCursor(0, 20);     // 将绘图光标移动到坐标 (0, 20) 的位置。                         
  printEfont((char *)u8"Youtube频道:\n轻松易学嵌入式",
            1, // 文字大小
            ST77XX_GREEN, // 文字颜色
            ST77XX_BLACK); // 背景颜色
  
  tft.setCursor(0, 70);     // 将绘图光标移动到坐标 (0, 50) 的位置。
  printEfont((char *)u8"SPI演示",
            2, // 文字大小
            ST77XX_RED, // 文字颜色
            ST77XX_BLACK); // 背景颜色
  

}

void loop()
{
}

