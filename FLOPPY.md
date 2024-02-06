Floppy Format
=============

The Emulator I floppy consists of 35 tracks / 3584 bytes per track. The general structure is as follows:
- 2 tracks "operating system"
- 16 tracks "lower bank"
- 16 tracks "upper bank"
- 1 track "sequencer data"


Operating System
----------------

The operating system is Z80 machine code which gets loaded to address 0x500 in
Emulator RAM. Once the first track of the OS code is loaded, the bootrom code
jumps to 0x500 and it is the responsibility of that code to load the remaining
OS code and potentially the sound data into RAM.


Bank format
-----------

The bank data is loaded to address `0x2000` (lower) and `0x12000` (upper). The
format for both banks is identical:

```c
struct Sample {
    u8   cutoff;
    u16* tuning;
    u16  startaddr;
    u16  startlen;
    u16  loopaddr;
    u16  looplen;
    u16  endaddr;
    u16  endlen;
    u8   size; /* only present in first sample */
};

struct SimpleBank {
    /* 00 */ u8  flags;
    /* 01 */ u8  cutoff;
    /* 02 */ u16 unused;
    /* 04 */ u16 start;
    /* 06 */ u16 startlen;
    /* 08 */ u16 loop;
    /* 0A */ u16 looplen;
    /* 0C */ u16 end;
    /* 0E */ u16 endlen;
};

struct MultisampleBank {
    /* 00 */ u8  flags;
    /* 01 */ u8  current_cutoff;
    /* 02 */ u16 unused;
    /* 04 */ u16 current_start;
    /* 06 */ u16 current_startlen;
    /* 08 */ u16 current_loop;
    /* 0A */ u16 current_looplen;
    /* 0C */ u16 current_end;
    /* 0E */ u16 current_endlen;
    /* 10 */ u8  samplecnt;
    /* 11 */ struct Sample[samplecnt] samples;
};

union Bank {
    struct SimpleBank simple;
    struct MultisampleBank multi;
};

/* 02000 in RAM is at 01C00 in the floppy image */
union Bank* LOWER = (union Bank*) 0x02000;
union Bank* UPPER = (union Bank*) 0x12000;
```

The most important bit in the `flags` field is bit `0x10`, which marks a bank
as "multisample" bank. If the bit is not set, only one sample is present in the
bank. Interestingly enough, the Emulator software copies the current sample
information to the `current_*` fields, as if the remaining part of the control
software only operates on simple banks.

Addresses are always pointers to Emulator RAM without the MSB (A16 line).


Sample Compression
------------------

According to the DAC datasheet, the compression is "Bell μ-225 logarithm law".
The datasheet gives a formula which can be translated to the following C code:

```c
s16 decode6072(s8 val) {
    int sign = val < 0;
    int abs = val & 0x7F;
    int c = abs >> 4;
    int s = val & 0x0F;
    int sgn = sign ? -1 : 1;
    return (s16) (sgn * ((1 << c) * (2 * s + 33) - 33));
}
```

The result has to be multiplied by 4 to get the full 16bit range.
