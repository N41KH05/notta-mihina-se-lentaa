// Board support for the Waveshare ESP32-S3-Touch-LCD-7.
// Pin numbers and timings are from Waveshare's own board definition
// (ESP32_Display_Panel: BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7).
#include "board.h"
#include <esp_sleep.h>
#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>
#include <driver/gpio.h>
#include <freertos/semphr.h>

namespace {

const int I2C_SDA = 8, I2C_SCL = 9, TP_INT = 4;
const int W = 800, H = 480;

// ---- CH422G I/O expander -------------------------------------------------------
// It has no normal register map: each "register" is its own I2C address.
const uint8_t CH422G_SET = 0x24;   // mode: bit0 = IO0-7 are outputs
const uint8_t CH422G_OUT = 0x38;   // output levels of IO0-7
// EXIO pins: 1 = touch reset, 2 = backlight, 3 = LCD reset, 4 = SD card CS, 5 = USB/CAN select
const uint8_t EXIO_TP_RST = 1 << 1, EXIO_BL = 1 << 2, EXIO_LCD_RST = 1 << 3, EXIO_SD_CS = 1 << 4;
uint8_t exio = EXIO_TP_RST | EXIO_LCD_RST | EXIO_SD_CS | 0x01;   // USB mode, backlight off
SemaphoreHandle_t i2cLock;

void exWrite(uint8_t addr, uint8_t v) {
  Wire.beginTransmission(addr);
  Wire.write(v);
  Wire.endTransmission();
}
void exSet(uint8_t bits, bool on) {
  exio = on ? (exio | bits) : (exio & ~bits);
  exWrite(CH422G_OUT, exio);
}

// ---- GT911 touch -------------------------------------------------------------------
uint8_t gtAddr = 0x5D;

bool gtRead(uint16_t reg, uint8_t* buf, int n) {
  Wire.beginTransmission(gtAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)gtAddr, n) != n) return false;
  for (int i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
void gtWrite(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(gtAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(v);
  Wire.endTransmission();
}

// ---- RGB LCD ---------------------------------------------------------------------
esp_lcd_panel_handle_t panel = nullptr;
uint16_t* fb[2] = {nullptr, nullptr};
int back = 1;                       // index of the buffer we draw into
SemaphoreHandle_t frameDone;

bool IRAM_ATTR onFrameDone(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(frameDone, &woken);
  return woken == pdTRUE;
}

}  // namespace

bool boardSdBegin() {
  // The card's chip select is on the expander, too slow to switch for every transfer.
  // The card is alone on its bus, so: wake it with CS high, then keep CS low for good
  // and give the SD library a pin number that doesn't exist (255) to "switch".
  const int SD_SCK = 12, SD_MISO = 13, SD_MOSI = 11;
  exSet(EXIO_SD_CS, true);
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, -1);
  SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 10; i++) SPI.transfer(0xFF);          // 80 clocks: the card starts up
  SPI.endTransaction();
  exSet(EXIO_SD_CS, false);
  return SD.begin(255, SPI, 10000000, "/sd", 4);
}

void boardHardRestart() {
  if (i2cLock) xSemaphoreTake(i2cLock, pdMS_TO_TICKS(200));
  exio &= ~(EXIO_BL | EXIO_LCD_RST);         // backlight off, panel in reset (the expander
  exWrite(CH422G_OUT, exio);                 // keeps it there while the chip sleeps)
  delay(50);
  esp_sleep_enable_timer_wakeup(1000000);    // wake in 1 s: a fresh start, like after a power cut
  esp_deep_sleep_start();
}

bool boardInit() {
  i2cLock = xSemaphoreCreateMutex();
  frameDone = xSemaphoreCreateBinary();
  Wire.begin(I2C_SDA, I2C_SCL, 400000);

  // Expander: outputs on, reset the LCD.
  exWrite(CH422G_SET, 0x01);
  exWrite(CH422G_OUT, exio);
  exSet(EXIO_LCD_RST, false);
  delay(50);
  exSet(EXIO_LCD_RST, true);
  delay(120);

  // Touch reset; holding INT low during reset selects I2C address 0x5D.
  pinMode(TP_INT, OUTPUT);
  digitalWrite(TP_INT, LOW);
  delay(10);
  exSet(EXIO_TP_RST, false);
  delay(100);
  exSet(EXIO_TP_RST, true);
  delay(200);
  pinMode(TP_INT, INPUT);
  uint8_t id[4] = {0};
  if (!gtRead(0x8140, id, 4)) { gtAddr = 0x14; gtRead(0x8140, id, 4); }
  Serial.printf("Touch controller GT%c%c%c at 0x%02X\n", id[0], id[1], id[2], gtAddr);

  // 800x480 RGB panel, 16-bit colour, two frame buffers in PSRAM + DRAM bounce buffers
  // (the bounce buffers stop the picture shaking while Wi-Fi is busy).
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = LCD_PCLK_MHZ * 1000 * 1000;
  cfg.timings.h_res = W;
  cfg.timings.v_res = H;
  cfg.timings.hsync_pulse_width = 4;
  cfg.timings.hsync_back_porch = 8;
  cfg.timings.hsync_front_porch = 8;
  cfg.timings.vsync_pulse_width = 4;
  cfg.timings.vsync_back_porch = 8;
  cfg.timings.vsync_front_porch = 8;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 2;
  // The LCD is fed from two small internal-RAM buffers that an interrupt refills from
  // PSRAM. Keep them small: the board package restarts the feed at every frame
  // (CONFIG_LCD_RGB_RESTART_IN_VSYNC) and refills BOTH in the short gap between frames,
  // so big buffers make the top of every frame late. 10 lines is Waveshare's own value.
  cfg.bounce_buffer_size_px = W * 10;     // 2 x 16 KB; must divide the frame (480 lines)
  cfg.dma_burst_size = 64;
  cfg.hsync_gpio_num = 46;
  cfg.vsync_gpio_num = 3;
  cfg.de_gpio_num = 5;
  cfg.pclk_gpio_num = 7;
  cfg.disp_gpio_num = -1;
  const int data[16] = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40};
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = data[i];
  cfg.flags.fb_in_psram = 1;
  if (esp_lcd_new_rgb_panel(&cfg, &panel) != ESP_OK) {
    Serial.println("!! LCD init failed (is PSRAM set to 'OPI PSRAM'?)");
    return false;
  }
  esp_lcd_rgb_panel_event_callbacks_t cbs = {};
  cbs.on_frame_buf_complete = onFrameDone;
  esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, nullptr);
  esp_lcd_panel_reset(panel);
  esp_lcd_panel_init(panel);
  void *a = nullptr, *b = nullptr;
  esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &a, &b);
  fb[0] = (uint16_t*)a;
  fb[1] = (uint16_t*)b;
  memset(fb[0], 0, W * H * 2);
  memset(fb[1], 0, W * H * 2);
  back = 1;
  return true;
}

uint16_t* boardBackBuffer() { return fb[back]; }
const uint16_t* boardFrontBuffer() { return fb[back ^ 1]; }

void boardPresent() {
  xSemaphoreTake(frameDone, 0);                        // forget older frames
  esp_lcd_panel_draw_bitmap(panel, 0, 0, W, H, fb[back]);   // switches, no copy
  // Wait until the old buffer has been sent out once more, then it is free to draw in.
  xSemaphoreTake(frameDone, pdMS_TO_TICKS(60));
  back ^= 1;
}

void boardBacklight(bool on) {
  xSemaphoreTake(i2cLock, portMAX_DELAY);
  exSet(EXIO_BL, on);
  xSemaphoreGive(i2cLock);
}

int boardTouch(TouchPt* pts, int max) {
  xSemaphoreTake(i2cLock, portMAX_DELAY);
  uint8_t status = 0;
  int n = 0;
  static int last = 0;
  static TouchPt lastPts[5];
  if (gtRead(0x814E, &status, 1) && (status & 0x80)) {
    n = status & 0x0F;
    if (n > 5) n = 5;
    uint8_t buf[40];
    if (n && gtRead(0x814F, buf, n * 8)) {
      for (int i = 0; i < n; i++) {
        lastPts[i].x = buf[i * 8 + 1] | (buf[i * 8 + 2] << 8);
        lastPts[i].y = buf[i * 8 + 3] | (buf[i * 8 + 4] << 8);
      }
    }
    gtWrite(0x814E, 0);                                // tell the chip we've read it
    last = n;
  }
  xSemaphoreGive(i2cLock);
  // No new report since the last poll: the fingers haven't changed.
  int count = last < max ? last : max;
  for (int i = 0; i < count; i++) pts[i] = lastPts[i];
  return count;
}
