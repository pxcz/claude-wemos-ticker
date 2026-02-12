# HW:
##  3-wire SPI OLED → Wemos D1 mini (ESP8266)

| OLED pin | Wemos pin | GPIO |
|---|---:|---:|
| **D0** (SCK/CLK) | **D5** | GPIO14 |
| **D1** (MOSI/DIN) | **D7** | GPIO13 |
| **DC** | **D2** | GPIO4 |
| **RST/RES** | **D1** | GPIO5 |
| **CS** | **D0** | GPIO16 |
| **VCC** | **3V3** |
| **GND** | **GND** |

##  0.96" OLED 
Wemos OLED, 128x64 = https://www.aliexpress.com/item/1005009923777658.html

## 3D printed enclosure 
TBD

## Run backend:
Works only on OSX because of stored accesstoken in system keychain; token is NEVER sent anywhere

`npm install`

`node index.js`


## Preview:

![result](raw.jpg)
