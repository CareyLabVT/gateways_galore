# Node Setup
Before reading this, be sure to check out the [Gateway Setup](/gateway/gateway_setup.md) page so that you have a working gateway capable of receiving LoRa packets from the node. 

## Hardware Needed
- LoRaWAN Gateway, see [Gateway Setup](/gateway/gateway_setup.md)

- 915MHz LoRa-enabled microcontroller
    
    **NOTE:** I used the [Adafruit Feather RP2040 with RFM95](https://www.adafruit.com/product/5714). If you are not using this microcontroller, you need to use something with on-board memory. The Feather RP2040 has 8MB QSPI FLASH memory.

- 915MHz antenna
    
    **NOTE:** I soldered on a 3-inch length solid-core wire for testing purposes. I later removed it and attached a uFL to N antenna adapter with a larger outdoor 915MHz antenna

- USB-C cable with data lines

## Basic Setup

### Arduino IDE Configuration
For this project, I used the Arduino IDE because it provided all of the features I needed. However, you may want to consider installing and configuring the Platform.io add-on for Visual Studio Code. It's more feature-rich, allows for better debugging, and has all the fun features of VS Code. However, this page does not cover how to setup Platform.io.

Download the Arduino IDE from their [offical download page](https://www.arduino.cc/en/software/) and follow the installation instructions.

### Configuring IDE for RP2040 features

#### RP2040 support
To add support for the Feather RP2040 board, follow [Adafruit's guide on how to set up the Arduino IDE for this](https://learn.adafruit.com/feather-rp2040-rfm95/arduino-ide-setup).

#### RadioLib
1. In the Arduino IDE, select **Tools > Manage Libraries**. 
2. Install the **RadioLib** library by **Jan Gromes**.

#### Adafruit SPI libraries
[Here is an Adafruit guide](https://learn.adafruit.com/adafruit-feather-m0-express-designed-for-circuit-python-circuitpython/using-spi-flash) with screenshots on how to install the Adafruit SPIFlash and SdFat - Adafruit Fork libraries. 

#### Putting it all together
In order to use the RP2040 board's SPI FLASH and LoRa radio for this project, you'll need to perform the following steps. 

1. First, perform a "FLASH Nuke" on the RP2040. This step is important before formatting the QSPI FLASH chip, but is also very useful if the board ever becomes unresponsive or isn't detected by your machine during development.

    1. Plug the Feather RP2040 board into your computer. 

    2. Hold down the **BOOT** button and, while still holding down the **BOOT** button, press and release the **RESET** button and also release the **BOOT** button.

    3. The computer should detect the device as a filesystem shortly after. Copy the `flash_nuke.uf2` file onto the board. The board should disappear as a filesystem and appear disconnected after this. 

2. Now, format the RP2040 board's SPI FLASH chip. The board should be plugged in to your computer.

    1. Locate the `SdFat_flash` file through `File > Examples > Adafruit SPIFlash > SdFat_flash`. 

    2. Make sure the RP2040 board is selected in the dropdown next to the **Verify** and **Upload** buttons. 

    3. Locate the Flash Size settings under `Tools > Flash Size`. Set the FLASH size to the last setting: `8MB (Sketch: 1MB, FS: 7MB)`.

    4. Upload the sketch to the RP2040.

    5. Open the Serial Montior under `Tools > Serial Monitor`. Once the sketch is done uploading to the RP2040, you should have to type `OK` into the Serial Monitor prompt to initialize the FLASH formatting. It should only take a few moments. 

    6. It should report a success. You can close the `SdFat_flash` example sketch now.

3. If you are starting a new Arduino project, you'll need the `config.h` and `flash_config.h` files located in the **node** folder of this repository. Paste them into your new Arduino project folder. You should not have to modify `flash_config.h`, but you will need to modify `config.h` per device.
    1. See the instructions on [adding a new device to the Chirpstack network](/gateway/gateway_setup.md#adding-a-device-to-the-chirpstack-lorawan-network). You will need the **Device EUI**, **Application key**, and **Network key**.
    2. In `config.h`, locate the following section:
    ```C
    // — OTAA credentials —  
    #define RADIOLIB_LORAWAN_JOIN_EUI  0x0000000000000000ULL
    #define RADIOLIB_LORAWAN_DEV_EUI   0x9f806861604420e1ULL
    #define RADIOLIB_LORAWAN_APP_KEY   0xe4,0x7a,0x6e,0xe1,0xb9,0xb9,0xe3,0x48,0x3f,0x3d,0xe9,0x7d,0x66,0xda,0x59,0x86
    #define RADIOLIB_LORAWAN_NWK_KEY   0x5a,0xa6,0xa6,0xbd,0x71,0xc6,0x39,0x93,0x94,0x45,0x5a,0x3a,0xc9,0xd9,0x9e,0x86
    ```
    3. Replace the `RADIOLIB_LORAWAN_DEV_EUI` value with the **Device EUI** and make sure to add the `ULL` at the end of the hex string.

    4. Replace the `RADIOLIB_LORAWAN_APP_KEY` value with the **Application key**. I just copied the hex string in and manually added the `0x` and `,` characters in.

    5. Do the same thing with the `RADIOLIB_LORAWAN_NWK_KEY` and the **Network key**.

    **NOTE:** You can also copy `config.h` and `flash_config.h` from the RadioLib and Adafruit SPIFlash examples, respectively. However, you'll have to change some of the values to reflect the board's pinouts and unique characteristics.

With a functional program uploaded on the RP2040 (and your two configuration files), you should see the JoinRequest and JoinAccept sequence on the gateway.