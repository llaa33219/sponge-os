# high_phys_probe — QEMU-verifiable gate for the row-13/14 high-phys
# IO_MEM machinery (docs/11 rows 13/14). See main.cc for the full
# rationale (synthetic within-coverage and beyond-coverage requests,
# because QEMU 11 cannot place a PCI BAR in the [RAM-top, 512 GiB)
# range — 64-bit BARs are fixed at 54 TiB there).

TARGET   := high_phys_probe
SRC_CC   := main.cc
LIBS     := base
