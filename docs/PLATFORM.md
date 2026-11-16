# Platform adapters

The Windows executable accepts a BASIC filename at launch, or opens the
command interpreter with no filename. `RUN`, `LIST`, `NEW`, `LOAD`, `SAVE`
and `HELP` operate on its editable source. File input and output use the host
filesystem. EEPROM, GPIO, ADC and elapsed time are deterministic mocks.
The mock EEPROM exists for the lifetime of the process.

The ATmega1284P build exposes UART0 at 9600 baud, 8N1, with a 16 MHz CPU.
Ctrl+C interrupts execution. `LOAD` and `SAVE` with no path use EEPROM.
The storage header carries a magic number, version, length, CRC-16 and a
marker written last. An interrupted save can invalidate the previous image.
BASIC raw EEPROM byte addresses span 0–4095. They share the EEPROM with
the saved source. GPIO numbers 0–31 map across PA through PD, and PD0/PD1
are reserved for UART. ADC channels are 0–7. Milliseconds use Timer0.

The STM32 repository supplies a separate VT100 shell, MCU adapter and FatFs
filesystem. It links the same interpreter through the API and CMake module in
[EMBEDDING.md](EMBEDDING.md).
