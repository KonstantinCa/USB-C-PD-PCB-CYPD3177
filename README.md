# USB-C Power Delivery PCB

This project focuses on the design and analysis of a USB-C Power Delivery sink system.  
The goal was to develop a custom PCB capable of requesting defined USB-PD voltage profiles from a USB-C Power Delivery charger.

The system is based on the Infineon/Cypress CYPD3177 USB-PD controller.  
An external microcontroller communicates with the controller via I2C in order to select different Power Delivery profiles and read status information such as the negotiated VBUS voltage.

## Project Overview

The project combines hardware design, embedded firmware and protocol analysis.  
The custom PCB was designed in KiCad and includes the required USB-C connector, USB-PD controller, power path circuitry, voltage regulation and communication interfaces.

## Features

- USB-C Power Delivery sink implementation
- CYPD3177 USB-PD controller integration
- Selection of different USB-PD voltage profiles
- I2C communication between microcontroller and USB-PD controller
- VBUS voltage measurement
- Interrupt-based response handling
- Finite State Machine firmware structure
- KiCad schematic and PCB layout
- Analysis of USB-PD, USB 2.0, I2C and UART communication

## Supported USB-PD Profiles

The firmware supports the following fixed supply PDO profiles:

| Profile | Voltage | Current |
|---|---:|---:|
| 1 | 5 V | 2 A |
| 2 | 9 V | 2 A |
| 3 | 12 V | 2.25 A |
| 4 | 15 V | 1.8 A |
| 5 | 20 V | 1.5 A |

## Firmware

The ESP32 firmware communicates with the CYPD3177 over I2C and allows the user to select a desired USB-PD profile via the serial monitor.

### Serial Commands

| Command | Function |
|---|---|
| `STATUS` | Reads the current status and response information |
| `5V2A` | Requests 5 V / 2 A |
| `9V2A` | Requests 9 V / 2 A |
| `12V2.25A` | Requests 12 V / 2.25 A |
| `15V1.8A` | Requests 15 V / 1.8 A |
| `20V1.5A` | Requests 20 V / 1.5 A |

### Firmware State Machine

The firmware is structured as a finite state machine:

```text
INIT -> IDLE -> SEND -> WAIT -> RECEIVE -> IDLE

The state machine waits for user input, checks whether a USB-PD contract is established, sends the selected PDO to the CYPD3177, waits for an interrupt, and then reads the response and VBUS voltage.

## Hardware Design

The PCB design contains the following main functional blocks:

- USB-C connector
- CYPD3177 USB Power Delivery controller
- Power path switching stage
- Buck converter for 3.3 V supply generation
- USB-to-UART interface for communication and debugging
- Microcontroller interface
- I2C communication lines
- UART communication lines
- Protection and configuration circuitry

The PCB was designed as a four-layer board with separate layers for signal routing, ground reference, and power distribution.

