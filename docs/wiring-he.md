# Connecting the Roomba to the module

![RooCore wiring](diagrams/wiring-he.svg)

[Open SVG](diagrams/wiring-he.svg) · [Open PNG](diagrams/wiring-he.png) · [Detailed wiring guide](wiring.md)

Use the 2N3906 PNP transistor from the BOJACK kit, an ESP-01S and a buck converter set to 3.3V.
The setup follows thehookup's project and draws power from the Roomba battery. The GPIO2 connection to BRC comes from its 600-series code.

| From | To |
|---|---|
| Roomba pin 1, battery power | Switch, then buck IN+ |
| Roomba pin 6 or 7 | Common ground: buck IN-/OUT-, module GND and transistor C |
| Buck OUT+ at 3.3V | Module VCC and EN |
| Roomba pin 4, TXD | Transistor B |
| Transistor E | Module RX / GPIO3 |
| Module TX / GPIO1 | Roomba pin 3, RXD |
| Module GPIO2 | Roomba pin 5, BRC |
| Roomba pin 2 | Leave disconnected and insulated |

Use one buck converter. The 5V rail and two signal converters from the earlier diagram are no longer needed.
Place 100nF and 100µF capacitors between VCC and ground near the module, in addition to any capacitors required by the buck manufacturer.
GPIO0, EN and RST must be high at startup. Some pull-up resistors are already fitted on the ESP-01S.
GPIO2 starts high, then drives BRC. Do not connect it directly to VCC.

## Transistor pins

The diagram shows pin functions, not their physical positions. Connect by E/B/C.
The onsemi 2N3906 datasheet assigns pin 1 to E, pin 2 to B and pin 3 to C. Check the physical orientation against the marking on the part you bought.

The original Fritzing file uses a generic PNP component. Its current part definition places the receive node on C and ground on E.
For the 2N3906, RooCore connects E to RX, B to the Roomba's TX and C to ground, using the transistor in its normal operating orientation.
This changes the connections shown by the original symbol. The firmware enables the ESP's internal RX pull-up.

TX and BRC retain the upstream project's direct connections. The normal TX pin also carries ROM messages when the ESP boots. Those messages are no longer routed to another pin as in the previous version. Our firmware does not print debug messages to this line.

## Power and programming

Choose a non-isolated buck converter whose input range covers the battery voltage during charging. An input rating of at least 24V is a starting requirement.
It needs an output capacity of at least 500mA at 3.3V; a 1A rating gives some margin. Set and measure the output before connecting the module.
The Roomba socket allows 200mA continuously and up to 500mA peak on the battery side, including startup current.
Turn the added circuit off at its switch after use. BRC pulses keep the robot awake, and this design has no automatic low-battery cutoff.

The socket numbering in the diagram is viewed into the holes on the robot. The solder side of the plug may be mirrored.
To flash the ESP, remove it from the robot circuit and insert it into the CH340 programmer. Do not connect both at once.

See the [sources and upstream comparison](wiring.md#sources). The software has been built and tested; the hardware has not been tested here.
