// 从机
#include <ESP32SPISlave.h>

ESP32SPISlave slave;

#define SPI_SCLK 5
#define SPI_MISO 6   // Master In, Slave Out
#define SPI_MOSI 7   // Master Out, Slave In
#define SPI_CS   21

uint8_t tx_buf[1] {0xA5};
uint8_t rx_buf[1] {0};

void setup() {
    Serial.begin(115200);
    pinMode(8, OUTPUT);
    digitalWrite(8, LOW); // 默认熄灭
    delay(2000);

    slave.setDataMode(SPI_MODE0);   // default: SPI_MODE0
    slave.begin(HSPI, SPI_SCLK, SPI_MISO, SPI_MOSI, SPI_CS);  // default: HSPI

    Serial.println("start spi slave");
}

void loop() {
    // 回复ACK
    tx_buf[0] = 0xA5;

    // start and wait to complete one BIG transaction (same data will be received from slave)
    const size_t received_bytes = slave.transfer(tx_buf, rx_buf, 1);

    // 根据命令控制LED
    digitalWrite(8, rx_buf[0]);
}