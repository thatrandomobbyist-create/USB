# UnoGPIO protocol

The serial link is 8-N-1 at 115200 baud. Multi-byte integers are little-endian.
Every frame is:

| Byte(s) | Field |
|---|---|
| 0 | Start `0xA5` |
| 1 | Protocol version `0x01` |
| 2 | Command/response |
| 3 | Payload length (0-8) |
| 4 | Sequence number (modulo 256) |
| 5.. | Payload |
| after payload | CRC-16-CCITT, low byte then high byte |

The CRC is initialized to `0xFFFF`, polynomial `0x1021`, and covers bytes 1
through the end of the payload (not the start byte or CRC).

## Requests

`0x01 WRITE_ALL` has four payload bytes: output mask low/high, then value mask
low/high. Bit 0 is Arduino pin 2 and bit 11 is pin 13. Bits 12-15 must be zero.
The command atomically updates the direction/value state from the protocol's
perspective.

`0x02 READ_ALL` has an empty payload. The response payload is the sampled
GPIO state as a little-endian 16-bit mask using the same bit mapping.

## Responses

`0x80 ACK` has an empty payload.

`0x81 READ_ALL response` has a two-byte input/state mask.

`0xE0 ERROR` has one payload byte: `1` invalid command, `2` invalid packet.
Malformed frames and bad checksums are discarded without execution. The
Windows library reports a response timeout, malformed response, or checksum
failure rather than treating it as success.

The sequence number is echoed by the Arduino so the host can reject stale or
out-of-order responses. The host sends one request at a time.
