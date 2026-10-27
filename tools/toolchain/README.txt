Neon Tide's bundled compiler

These files come with the C++ compiler that Neon Tide carries for its updater (Neon Tide's README.md: "The bundled
compiler"). `build.bat bundle` cuts it down from w64devkit 2.9.1 for x64 (https://github.com/skeeto/w64devkit), which
it downloads from
  https://github.com/skeeto/w64devkit/releases/download/v2.9.1/w64devkit-x64-2.9.1.7z.exe
  (SHA-256 9208c19755cd4964b7915b9afcf02c66d493a4c870c4b3e83f6c538d9c1237a5)
to what Neon Tide's build.bat needs: the files are w64devkit's, unchanged; only the rest is left out.

What it holds, and the licenses:
- GCC 16.2.0 (gcc, g++, cc1, cc1plus, collect2; libgcc, libstdc++ and their headers): the GNU General Public License,
  version 3 or later (GPL-3.0.txt); the runtime libraries and headers also with the GCC Runtime Library Exception,
  version 3.1 (GCC-RUNTIME-EXCEPTION-3.1.txt), so programs built with them may use any license.
- GNU Binutils 2.47 (as, ld, windres): the GNU General Public License, version 3 or later (GPL-3.0.txt).
- GMP 6.3.0, MPFR 4.2.2 and MPC 1.4.1, built into cc1 and cc1plus: the GNU Lesser General Public License, version 3
  or later (LGPL-3.0.txt, which adds to GPL-3.0.txt).
- Mingw-w64 14.0.0 (the Windows headers, the C runtime and the import libraries): the licenses collected in
  COPYING.MinGW-w64-runtime.txt. Programs built with it contain parts of its runtime and should come with that file.
- w64devkit's own build recipe: public domain (the Unlicense).

The source code of all of it:
- w64devkit 2.9.1, the recipe that builds this compiler, with the exact versions, checksums and patches it uses:
  https://github.com/skeeto/w64devkit/tree/v2.9.1 (Dockerfile and src/)
- GCC 16.2.0: https://ftp.gnu.org/gnu/gcc/gcc-16.2.0/gcc-16.2.0.tar.xz
- GNU Binutils 2.47: https://ftp.gnu.org/gnu/binutils/binutils-2.47.tar.xz
- GMP 6.3.0: https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.xz
- MPFR 4.2.2: https://ftp.gnu.org/gnu/mpfr/mpfr-4.2.2.tar.xz
- MPC 1.4.1: https://ftp.gnu.org/gnu/mpc/mpc-1.4.1.tar.xz
- Mingw-w64 14.0.0: https://downloads.sourceforge.net/project/mingw-w64/mingw-w64/mingw-w64-release/mingw-w64-v14.0.0.tar.bz2
