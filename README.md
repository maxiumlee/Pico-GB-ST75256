# Pico-GB-ST75256

## 原件清单

- Raspberry Pi Pico ×1
- JLX160160G-948-PN LCD（ST75161，160×160）×1
- Micro SD 卡模块 ×1
- FAT32 格式的 Micro SD 卡 ×1
- MAX98357A I2S 功放模块 ×1
- 2 W / 8 Ω 扬声器 ×1
- 常开轻触按键 ×8
- 面包板或洞洞板、杜邦线及 USB 数据线

## 接线

所有模块必须与 Raspberry Pi Pico 共地。

### 按键

| 按键 | Pico | 另一端 |
|---|---|---|
| UP | GP2 | GND |
| DOWN | GP3 | GND |
| LEFT | GP4 | GND |
| RIGHT | GP5 | GND |
| A | GP6 | GND |
| B | GP7 | GND |
| SELECT | GP8 | GND |
| START | GP9 | GND |

### Micro SD 卡模块

| SD 模块 | Pico |
|---|---|
| VCC | 3V3(OUT) |
| GND | GND |
| MISO | GP12 |
| CS | GP13 |
| SCK/CLK | GP14 |
| MOSI | GP15 |

### JLX160160G-948-PN LCD

| LCD | Pico |
|---|---|
| VDD | 3V3(OUT) |
| VSS/GND | GND |
| CS | GP17 |
| CLK/SCL | GP18 |
| SDA/SDI/DIN | GP19 |
| RS/A0/DC | GP20 |
| RST/RES | GP21 |
| LED/BL | GP22 |

### MAX98357A

| MAX98357A | Pico / 扬声器 |
|---|---|
| VIN | 3V3(OUT) |
| GND | GND |
| DIN | GP26 |
| BCLK | GP27 |
| LRC/WS | GP28 |
| SPK+ / SPK− | 扬声器正负极 |

## 固件下载

[下载最新 RP2040_GB.uf2](https://github.com/maxiumlee/Pico-GB-ST75256/releases/latest/download/RP2040_GB.uf2)
