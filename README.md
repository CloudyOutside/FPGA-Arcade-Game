# FPGA-Based Real-Time Arcade Game
# Platform: Xilinx Zynq-7000 SoC (ARM Cortex-A9)

Tools: Vivado, Xilinx SDK (Vitis), Bare-Metal C

# Overview
This project is a high-performance, real-time arcade game developed for the Zybo Z7 FPGA platform. Unlike standard software games, this project interfaces directly with hardware peripherals via SPI and Memory-Mapped I/O, running on a bare-metal ARM Cortex-A9 processor.

It was designed to demonstrate efficient state machine management, hardware-software co-design, and low-latency user input processing.

# Hardware Integration
The system architecture bridges a high-level game engine with low-level physical sensors:

Processor: ARM Cortex-A9 (Hard Processor System).

Input: PmodJSTK2 (Two-axis analog joystick + trigger) via SPI.

Output: PmodOLED (128x32 monochrome display) via SPI.

Communication: Utilizes Xilinx IP cores to map hardware peripherals into the ARM processor's memory space.
