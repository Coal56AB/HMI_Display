tinf by Joergen Ibsen, https://github.com/jibsen/tinf

Downloaded 2026-09-07 from upstream `Src/tinflate.c` and `Src/tinf.h`.
The permissive zlib license is reproduced in the header of each source file.
Local change: two nonnegative pointer spans are cast to unsigned int to avoid
signedness warnings on 32-bit ARM. No algorithm changes.
