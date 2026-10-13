# L2: interrupt-driven UART driver with ring buffers

The CPU no longer waits for the UART. The hardware raises an interrupt whenever a byte arrives or the transmitter can take another byte, and a short interrupt service routine (ISR) moves that one byte between the USART and a ring buffer in RAM. The application reads from and writes to the ring buffers whenever it likes.

## How it works

**Ring buffers**: two 256-byte circular arrays, one for RX and one for TX, each with a `head` (write position) and a `tail` (read position). When an index reaches the end it wraps back to 0. A buffer is empty when `head == tail` and full when advancing `head` would land on `tail`. One side only ever moves `head` and the other only ever moves `tail`, so the ISR and the application never need a lock.

**Receiving**: `RXNEIE` is enabled at init. Every received byte triggers `USART1_IRQHandler`, which reads `RDR` and stores the byte at `rx_head`. The application calls `uart_read()` to take bytes from `rx_tail`. If the application falls behind by more than 255 bytes, new bytes are dropped and counted in `stats.rx_dropped`.

**Sending**: `uart_write()` copies your data into the TX ring at `tx_head`, turns on `TXEIE`, and returns immediately. Each TXE interrupt moves one byte from `tx_tail` into `TDR`. When the ring is empty, the ISR turns `TXEIE` off again; otherwise the interrupt would fire continuously, because TXE stays set while the transmitter is idle.

**Errors**: the ISR clears overrun, framing and noise flags through `ICR` and counts them. Clearing them in the ISR is essential: an uncleared overrun keeps re-triggering the interrupt.

**NVIC**: the Cortex-M33 interrupt controller. `uart_init()` sets USART1's priority (IRQ 61) and enables it. The handler's name, `USART1_IRQHandler`, must match the vector table entry in the startup file.

## API

| Function | Behaviour |
|---|---|
| `uart_init(&cfg)` | Same setup as L1, plus RX interrupt and NVIC enable |
| `uart_write(data, len)` | Queues as many bytes as fit, returns how many; never waits |
| `uart_write_all(data, len)` | Queues everything, waiting only while the TX ring is full |
| `uart_write_string(str)` | `uart_write_all` for a C string |
| `uart_flush()` | Waits until all queued bytes have left the pin |
| `uart_available()` | Number of received bytes waiting |
| `uart_read(buf, max)` | Copies up to `max` received bytes, returns how many; never waits |
| `uart_get_stats(&s)` | Dropped bytes and overrun/framing/noise error counts |

## What improved over L1

- The CPU is free while bytes are on the wire; it only spends a few dozen cycles per byte inside the ISR.
- Received bytes are buffered, so bursts of up to 255 bytes survive even if the application is busy.
- Writes return immediately instead of blocking for the whole message.

## Remaining limitations

- **One interrupt per byte.** At 115200 baud that is roughly 11,500 interrupts per second in each direction at full load. At higher baud rates or with several UARTs, the per-byte interrupt overhead becomes significant.
- The CPU still copies every byte, twice in fact: once in the ISR and once in `uart_read()` or `uart_write()`.

L3 removes the per-byte interrupt and the per-byte copy by handing the transfer to the DMA controller.

## Test

Flash it, open the terminal, press reset. Type to see the echo, and press `?` to print statistics. The idle-loop count shows how many times the main loop ran with nothing to do: CPU time that L1 would have spent blocked. Try pasting a few lines of text: the bytes are buffered by the ISR, so they come back intact with `overrun` still at 0.
