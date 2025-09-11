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

### Arduino IDE Installation
For this project, I used the Arduino IDE because it provided all of the features I needed. However, you may want to consider installing and configuring the Platform.io add-on for Visual Studio Code. It's more feature-rich, allows for better debugging, and has all the fun features of VS Code. However, this page does not cover how to setup Platform.io.

## Installing and configuring Radiolib

## Configuring the SQPI FLASH

## Program Overview

## Troubleshooting
