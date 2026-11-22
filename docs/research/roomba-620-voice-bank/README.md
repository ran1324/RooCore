# Inside a Roomba 620 voice bank

[עברית](README-he.md) · [Verified flash image](Winbond-W25Q16BV.bin) · [341 WAV files](Roomba-voice-bank.zip) · [English audio sample](Roomba-English-preview.wav) · [Slot index](index.csv)

On November 22, 2026, we read U24 from one Roomba 620 mainboard. Its 2 MiB SPI flash contains recorded voice prompts in 16 languages. The English sample plays slots 3–5 in order: move the robot and press Clean, clean the brushes, and charge the robot. The archive contains factory recordings recovered from this unit.

The directory accounts for essentially the whole programmed region. We found no separate executable firmware image here. The robot's main controller was outside the scope of this capture.

## Reading U24 on the bench

The chip marking and eight-lead package point to the Winbond W25Q16BV family. The final marking character was indistinct. The electrical identification returned **EF 40 15**, the expected manufacturer, memory type and capacity. The owner measured 3.3 V directly between pins 8 and 4. The table records the final bench wiring for the removed chip. [Winbond datasheet, §§4, 8, 11 and 14](https://media.digikey.com/pdf/data%20sheets/winbond%20pdfs/w25q16bv.pdf) · [Seeed XIAO ESP32-S3 pinout](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)

| U24 lead | Signal | Connection during the successful capture |
|---:|---|---|
| 1 | CS# | D3 / GPIO4 |
| 2 | DO / MISO | D9 / GPIO8 |
| 3 | WP# | Broken, reported disconnected |
| 4 | GND | GND |
| 5 | DI / MOSI | D10 / GPIO9 |
| 6 | CLK | D8 / GPIO7 |
| 7 | HOLD# | Broken lead; owner reported reconnecting a pull-up. The bench procedure called for 10 kΩ to 3V3. Final lead voltage was unmeasured. |
| 8 | VCC | 3V3, via a repaired lead |

Early reads with a weak pull-down on MISO yielded `00 00 00`, and reads with a weak pull-up yielded `FF FF FF`. The return line followed the reader's bias. We investigated damaged leads, and the owner checked DO continuity, repaired VCC, checked pins 5 and 6 for contact, and reconnected HOLD#. A later test still selected D1 although the CS wire was on D3. Once the reader selected **D3**, both pulls gave **EF 40 15**. Several conditions changed during troubleshooting, so the sequence does not isolate a single original fault.

## Two independent reads

The Mac connected to the XIAO's ESP32-S3 ROM loader over USB. A temporary reader ran from the ESP32's RAM and clocked the external SPI bus in mode 0. It issued only `0x9F` (identify) and `0x03` (read data with a 24-bit byte address) to U24. It rechecked the ID before each 4,096-byte block and sent the block address, bytes and CRC-32 to the host for validation.

Each pass covered 512 blocks, including the final erased region. The first traversed addresses upwards with MISO weakly pulled down. The second traversed them downwards with MISO weakly pulled up and placed each block at its original offset. Both independent 2,097,152-byte images matched byte for byte. The published BIN matches both. No program or erase command was sent to U24, and the XIAO reader ran without flashing its firmware.

**SHA-256** `334b74d0fb4790f45095d53297d774947b44f72662aefdf071f7e35b95457a67`

## How the extraction works

All offsets below are bytes. The end column is exclusive.

| Start | End | Observation |
|---|---|---|
| 0x000000 | 0x000003 | Header bytes `01 01 62`; bytes 1–2 give the big-endian count 354. |
| 0x000003 | 0x00084F | 354 entries × 6 bytes. Each contains two little-endian 24-bit offsets, start and end. Every end equals the next start. |
| 0x00084F | 0x000851 | Two bytes of unknown purpose, `74 B2`. |
| 0x000851 | 0x1F789A | Indexed payloads, including the four-byte slot 0 (`00 00 00 0A`). |
| 0x1F789A | 0x1F789C | Two bytes of unknown purpose, `30 A6`. |
| 0x1F789C | 0x200000 | 34,660 consecutive erased bytes (`FF`). |

The zero-based directory contains **341 audio slots, 12 empty slots and one four-byte record**. Slot 1 is a standalone recording. Slots 2–353 form 16 groups of 22 positions, including the empty slots. Decoding sample groups revealed English, French, German, Spanish, Italian, Dutch, Danish, Swedish, Norwegian, Finnish, Polish, Russian, Portuguese, Japanese, Korean and Chinese, in that order. The archive groups the WAVs by language code. The CSV includes every original slot, its source range and its exported filename.

The payloads decode as raw 4-bit IMA ADPCM. Start each clip with predictor 0 and step index 0. Consume the high nibble first, then the low nibble, giving two signed 16-bit PCM samples per byte. We compared nibble orders and checked the speech using local speech recognition. The export uses mono PCM at **8,000 Hz**; original hardware playback timing remains unverified. The preview joins three English clips with 200 ms between clips.

## Reproduce from the BIN

Run from this directory with Python 3 (standard library only). Choose a new destination directory. The extractor checks the image SHA-256 before applying this unit's slot map.

```sh
python3 extract.py Winbond-W25Q16BV.bin extracted
```

This produces the 341 WAV files and the same 354-row CSV shipped in the ZIP. To compare every exported file against the published archive:

```sh
unzip -q Roomba-voice-bank.zip -d reference
diff -rq reference extracted
```
