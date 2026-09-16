# Source provenance

Protocol and firmware-envelope behavior is adapted from [OpenPrinting/foo2zjs](https://github.com/OpenPrinting/foo2zjs), commit `80499ed5bf6caa2963ad337e37cfda78a80aab1e`, retrieved 2026-09-07. Reference files: `foo2zjs.c`, `zjs.h`, `arm2hpdl.c`, `hplj1000`, and `PPD/HP-LaserJet_1005.ppd`. The source credits Robert Szalai and Rick Richardson. This project is GPL-2.0-or-later; see LICENSE.

`vendor/jbig/` contains unchanged `jbig.c`, `jbig.h`, `jbig_ar.c`, and `jbig_ar.h` from that commit, JBIG-KIT 2.1, copyright Markus Kuhn, GPL-2.0-or-later. Original notices are retained.

The optional reference executables build from an ignored upstream checkout. CMake replaces GCC union-cast expressions with equivalent byte-pointer expressions and opens input files in binary mode. `tests/reference_main.c` supplies a small Windows entry point and option parser. Protocol logic is unchanged. Reference tools are for development, not included in the printer package.

HP firmware is **not distributed** in this repository. Hardware tests used a privately supplied original `sihp1005.img`, SHA-256 `638B9AD4A2CF6888208926EF7C7DE073DA8BA4CDED25D31000A585C4526A9CA1`. Generated firmware downloads stay under ignored `artifacts/`. No original HP Windows DLL, executable, SYS, or font is linked or installed. Windows inbox Unidrv components are used from the operating system.
