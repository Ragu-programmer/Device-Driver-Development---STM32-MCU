# UART drivers for the NUCLEO-U575ZI-Q: three levels

Three bare-metal UART drivers for the STM32U575, written directly against the registers with no HAL. All three drive **USART1 on PA9 (TX) / PA10 (RX)**, which the on-board ST-LINK exposes as a virtual COM port over USB, so they can be tested with nothing but the USB cable and a serial terminal.

Each level solves the main weakness of the one before it:

| Level | Folder | Data moved by | What the CPU does |
|---|---|---|---|
| L1 | `L1_polling/` | CPU | Waits in a loop for every byte, then copies it |
| L2 | `L2_interrupt/` | CPU, inside an interrupt | Works on other things; runs a short ISR per byte |
| L3 | `L3_dma/` | GPDMA controller | Starts a transfer, gets one interrupt when it's done |

Each folder is a complete, self-contained project: `stm32u5_regs.h`, `uart.h`, `uart.c`, `main.c`, and its own README.

## Comparison

| | L1 polling | L2 interrupt | L3 DMA |
|---|---|---|---|
| **TX path** | CPU waits for TXE, writes each byte to TDR | `uart_write()` copies bytes into a 256-byte ring buffer and returns; the TXE interrupt feeds TDR one byte at a time | `uart_write()` points GPDMA channel 0 at your buffer and returns; GPDMA feeds TDR on each USART request |
| **RX path** | CPU waits for RXNE, reads RDR | RXNE interrupt copies each byte into a 256-byte ring buffer | GPDMA channel 1 copies each byte from RDR into a 256-byte RAM buffer; the CPU only restarts it every 256 bytes |
| **CPU busy-waiting** | For the whole transfer | Only if the TX ring is full, or in `uart_flush()` | Only in `uart_flush()` or `uart_write_string()` while a transfer is running |
| **CPU work per byte** | Waits a full byte time (about 87 µs at 115200 baud) plus the copy | One ISR entry/exit plus the copy (a few dozen cycles) | Effectively none |
| **Interrupts per 100 bytes sent** | 0 | about 100 | 1 |
| **Received data lost if the app is busy** | After 1 byte time | After 255 bytes are waiting (counted in `rx_dropped`) | After 256 bytes are waiting (overwritten silently) |
| **Hardware used** | USART1, GPIOA | + NVIC | + NVIC, GPDMA1 channels 0 and 1 |
| **TX buffer ownership** | Not an issue (blocking) | Driver copies your data | Your buffer must stay untouched until `uart_tx_busy()` is false |
| **Complexity** | Lowest | Medium: ISR, ring buffers, shared state | Highest: DMA setup, buffer lifetime, restart logic |
| **Typical use** | Bring-up, debug prints, simple tools | Consoles, command parsers, most protocols | High throughput, large frames, GPS/modem streams, low power |

## A note on "CPU time = zero" in L3

DMA removes the per-byte work, but not all CPU involvement. The CPU still programs each transfer, runs one completion interrupt per transfer (and one per 256 received bytes), and of course still has to process the data once it is in RAM. The DMA also shares the bus matrix with the CPU, so very heavy DMA traffic can slightly slow CPU memory accesses. For a UART at 115200 baud this is negligible, which is why DMA wins so clearly at higher data rates.

## How to build and run any level

1. In STM32CubeIDE: **File → New → STM32 Project**, select **NUCLEO-U575ZI-Q**, choose **Targeted Project Type: Empty**, and keep **TrustZone disabled**.
2. Replace the generated `Src/main.c` with the level's `main.c`, and add the other three files to the project.
3. Build, then **Run** to flash.
4. Open a terminal on the **STLink Virtual COM Port** at **115200, 8N1** (on macOS: `screen /dev/tty.usbmodemXXXX 115200`) and press the board's reset button.
5. Type to see your input echoed. In L2 and L3, press `?` to print driver statistics, including an idle-loop counter that shows how much the main loop was free to do other work.

The interrupt handler names (`USART1_IRQHandler`, `GPDMA1_Channel0_IRQHandler`, `GPDMA1_Channel1_IRQHandler`) match the vector table in CubeIDE's startup file for this chip, so nothing else needs registering.

If you use a CubeMX/HAL project instead, keep USART1 and GPDMA1 **disabled** in CubeMX (otherwise it generates its own handlers with the same names), copy only the three driver files, and set `periph_clk_hz` from `HAL_RCC_GetPCLK2Freq()`.

## Before trusting it on hardware

The code compiles cleanly and every struct offset is guarded by a `_Static_assert`, but it has not been run on a board here. Register addresses, bit positions, IRQ numbers (USART1 = 61, GPDMA1 channel 0 = 29, channel 1 = 30) and GPDMA request numbers (USART1 RX = 24, TX = 25) were taken from ST's documentation and HAL headers. Verifying them against **RM0456** (STM32U5 reference manual) is a good exercise and the right habit for any driver.

## Reference documents

- RM0456: STM32U5 reference manual (registers for RCC, GPIO, USART, GPDMA)
- STM32U575 datasheet: alternate function table (PA9/PA10 = AF7 USART1)
- UM2861: NUCLEO-U575ZI-Q board manual (virtual COM port wiring)
- PM0264: Cortex-M33 programming manual (NVIC)
