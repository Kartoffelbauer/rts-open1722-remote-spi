# RTS Open1722 Remote SPI

One board drives SPI peripherals that are physically wired to another board by sending IEEE 1722 Generic Byte Bus messages over the network.

## Language

### Roles

**Requester**:
The board that decides what happens on the remote bus: it holds all device knowledge (register maps, command sets, rendering) and sends GBB Requests.
_Avoid_: Board A, client, sender

**Responder**:
The board that has the peripherals wired to it and executes received GBB Requests as raw byte transfers on its local bus, without knowing what the bytes mean.
_Avoid_: Board B, SPI master, server, receiver

**Peripheral**:
A device on the Responder's SPI bus (the BME280 or the SSD1306 OLED).
_Avoid_: SPI slave, device

### Protocol

**GBB (Generic Byte Bus)**:
The IEEE 1722 message format used to carry byte-bus transfers between Requester and Responder, transported over UDP.
_Avoid_: SPI packet, remote SPI message

**GBB Request**:
A GBB message from the Requester asking the Responder to perform one transfer on a given Byte Bus.
_Avoid_: command (reserved for SSD1306 commands)

**Write Request**:
A GBB Request whose payload is clocked out on the Byte Bus in one bus transaction.

**Read Request**:
A GBB Request whose payload (e.g. a register address) is clocked out, followed by reading a requested number of bytes, all in one bus transaction.
_Avoid_: write-then-read, transceive

**GBB Response**:
The GBB message the Responder returns for exactly one GBB Request, carrying its outcome and any bytes read.

**Byte Bus**:
One addressable target on the Responder, selected by its Byte Bus ID, that a GBB Request is executed against. A Peripheral may be reachable through several Byte Buses; the OLED has an OLED Command Bus and an OLED Data Bus.
_Avoid_: channel, port

**OLED Command Bus**:
The Byte Bus whose bytes the OLED interprets as SSD1306 commands.

**OLED Data Bus**:
The Byte Bus whose bytes the OLED interprets as display RAM contents.
_Avoid_: pixel bus, framebuffer bus

**Failed Transaction**:
A GBB Request that ended in a GBB Response with the error flag set, or got no matching response in time.
_Avoid_: lost packet, NACK

### Demo

**Link Stats**:
The Requester's counters of sent requests, timeouts and error responses, plus min/avg/max round-trip time.

**Display Page**:
One of the screens the Requester cycles through on the OLED with the button (e.g. temperature, pressure, humidity, link stats).
_Avoid_: screen, view (and not to be confused with SSD1306 RAM pages)
