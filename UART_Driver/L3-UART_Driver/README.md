# L3: DMA-based UART driver

The CPU stops moving data altogether. The STM32U575's GPDMA (General Purpose DMA) controller copies bytes between USART1 and RAM directly, triggered by the USART's own DMA requests. The CPU only sets up each transfer and handles one interrupt when it finishes.

## How it works

The USART is configured with `DMAT` and `DMAR` in `CR3`, so instead of asking the CPU for attention it raises a DMA request every time TDR is empty or RDR holds a byte. Two GPDMA channels answer those requests:

**Channel 0, TX (memory → USART1 TDR)**: `uart_write(data, len)` writes the buffer address into `CSAR`, the length into `CBR1`, and enables the channel. GPDMA then moves one byte per USART request, incrementing the source address, until `len` bytes are done. It raises one interrupt (`GPDMA1_Channel0_IRQHandler`), which clears `tx_busy`. Request number 25 (`USART1_TX`) and `DREQ = 1` tell the channel that the USART is the destination.

**Channel 1, RX (USART1 RDR → `rx_buf`)**: configured once at init to fill the 256-byte `rx_buf`, incrementing the destination address. The CPU never touches individual bytes. The application finds out how far the DMA has got by reading the remaining count, `BNDT`, in `CBR1`: the write position is `256 - BNDT`. When the buffer fills, `GPDMA1_Channel1_IRQHandler` reloads the address and count and re-enables the channel, which makes the buffer circular. A byte arriving during that restart waits safely in RDR, since the restart takes a few microseconds and one byte takes 87 µs.

**Memory barrier**: before starting a TX transfer the driver executes `dmb`, which guarantees that the CPU's writes to the buffer have actually reached memory before the DMA starts reading it.

## API

| Function | Behaviour |
|---|---|
| `uart_init(&cfg)` | USART1 setup with DMA requests, GPDMA channels, NVIC |
| `uart_write(data, len)` | Starts a DMA transfer and returns immediately; `UART_ERR_BUSY` if one is running |
| `uart_write_string(str)` | Waits for any running transfer, then starts this one |
| `uart_tx_busy()` | `true` while a TX transfer is in progress |
| `uart_flush()` | Waits for the DMA to finish and the last byte to leave the pin |
| `uart_available()` | Number of received bytes waiting |
| `uart_read(buf, max)` | Copies up to `max` received bytes out of the DMA buffer |
| `uart_get_stats(&s)` | USART error counts and DMA error count |

## The one rule that matters: buffer lifetime

`uart_write()` does **not** copy your data. The DMA reads it directly from your buffer, in the background, after the function has returned. So the buffer must stay valid and unchanged until `uart_tx_busy()` returns `false`:

- Never pass a local array that goes out of scope before the transfer finishes.
- Never modify the buffer while a transfer is running.
- `static` buffers and string literals are safe.

That is why `main.c` keeps `rx_chunk`, `tx_chunk` and `stats_line` as `static` arrays and only refills `tx_chunk` when `uart_tx_busy()` is false. Getting this wrong is the most common DMA bug: the output appears corrupted, but only sometimes.

## What improved over L2

- **One interrupt per transfer** instead of one per byte. Sending 100 bytes costs one interrupt instead of 100.
- **No per-byte CPU work on RX**: the CPU only steps in once every 256 bytes to restart the channel.
- The CPU is free during the entire transfer, which also makes it possible to sleep while data streams.

## Limitations and next steps

- **Silent RX overwrite**: if the application doesn't read for more than 256 bytes, the DMA wraps around and overwrites unread data without an error. Production drivers detect this with the half-transfer interrupt or a larger buffer.
- **No "message finished" signal**: the application polls `uart_available()`. Enabling the USART's IDLE-line interrupt would tell you when the line goes quiet after a message, which is ideal for protocols with variable-length frames.
- **Restart instead of hardware circular mode**: the GPDMA has no simple "circular" bit; truly gapless circular transfers are built with its linked-list feature (a small descriptor in RAM that reloads the channel automatically). The restart approach used here is simpler and safe at UART speeds, and linked lists are a good next exercise.

## Test

Flash it, open the terminal, press reset. Type to see the echo, and press `?` for statistics. Notice that the idle-loop counter keeps climbing even while text is being sent: the main loop keeps running because the GPDMA, not the CPU, is moving the bytes.
