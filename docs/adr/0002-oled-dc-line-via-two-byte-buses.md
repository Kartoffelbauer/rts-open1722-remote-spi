# The OLED's D/C line is selected through two Byte Buses

The SPI SSD1306 needs a Data/Command GPIO, but GBB only carries bytes. We expose the OLED as two Byte Buses that share the same SPI chip select: the **OLED Command Bus** (Responder drives D/C low) and the **OLED Data Bus** (D/C high). The Responder sets D/C from the `byte_bus_id` before the transfer, so it still doesn't interpret the bytes (see [ADR 0001](0001-responder-is-a-dumb-byte-bus.md)). As a result, one Peripheral can have several Byte Buses.

## Considered Options

- **3-wire SPI with 9-bit words** (D/C as the 9th bit): needs jumper changes on the module, and 9-bit frames are awkward with the STM32 SPI in Zephyr.
- **A GPIO side channel outside GBB** (or abusing the `evt` bits): this breaks "everything on the remote bus goes through GBB", and `evt` is meant for upper-level events, not for driving pins.

## Consequences

- An SSD1306 update is at least two GBB Requests: addressing commands on the OLED Command Bus, then pixel bytes on the OLED Data Bus.
- The OLED's RES line is outside the protocol. The Responder pulses it once at boot.
