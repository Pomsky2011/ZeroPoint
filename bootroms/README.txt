ZeroPoint Boot ROM and APU BIOS (binaries only; the source is not published)
Built from ZPbootROM 319637c for ZeroPoint core revision 14.
License: public domain (CC0 1.0), see LICENSE.txt. The binaries only: the source is not part of that.

In short:
  bootrom.bin           is the Boot ROM for the retail system.
  dev/bootrom_dev.bin   is the Boot ROM for the dev system.
  bios_full.bin         is the APU BIOS.

  bootrom.bin          RETAIL Boot ROM, 115968 bytes (bank $E0 code, $E1 PPU splash + boot jingle).
                       Verifies the cartridge's RSA-2048 signature (v1/v2/v3 trailers) and halts on any failure.
                       No setting can skip the check. Use this one for normal play and for movies.
  dev/bootrom_dev.bin  DEV Boot ROM. Identical, except that on a dev unit (zeropoint_sdl --dev-unit, a host-only latch)
                       it skips signature verification, its exception entries ask the host for a handler image over
                       the debug UART, and holding all four shoulder inputs of player 1 while it boots erases the
                       cartridge's save RAM (docs/cart-save.md). With the latch clear it behaves like retail. Do not use
                       it as "the" Boot ROM: a movie pins the Boot ROM by SHA-256.
  bios_full.bin        the APU BIOS, 4096 bytes: the ZPA sequencer engine the Boot ROM uses to play the boot jingle.

Run:   zeropoint_sdl game.zpb --boot bootroms/bootrom.bin --apu-bios bootroms/bios_full.bin
       (zeropoint_qt: set the Boot ROM path in the settings dialog)
Without --boot / --apu-bios the emulator uses its built-in HLE Boot ROM and APU BIOS (it also verifies signatures). A cartridge
sees a different APU state at hand-off depending on which is in use (docs/boot-handoff.md): reset-halt the APU before loading
your own APU program.

SHA256SUMS.txt lists the hashes (sha256sum -c SHA256SUMS.txt).
