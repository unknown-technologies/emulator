#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "z80.h"
#include "emulator.h"
#include "trace.h"

/* EMULATOR OS MEMORY MAP (LATEST OS):
 * 0436: scan_row
 *
 * 078D: SCAN function, CP $09
 * 073F: SCAN function, figuring out key (LD B, $08)
 */
int main(int argc, char** argv)
{
	int keyid = -1;
	const char* fdd_image = NULL;
	const char* trc_file = NULL;
	const char* os_file = NULL;
	const char* rom_file = "roms/820816-0181.bin";
	BOOL auto_exit = FALSE;
	BOOL patch_serial = FALSE;

	Emulator* emulator = (Emulator*) malloc(sizeof(Emulator));
	Z80 ctx;

	for(unsigned int i = 1; i < argc; i++) {
		const char* arg = argv[i];
		if(arg[0] == '-') {
			switch(arg[1]) {
				case 'h':
					printf("Usage: %s [-k<key-id> | -m<midi-key-id>] [-t<trace.trc>] [floppy.img]\n", *argv);
					return 0;
				case 'k': {
					/* key input */
					keyid = atoi(&arg[2]);
					if(keyid < 0 || keyid >= 72) {
						printf("Invalid key %d\n", keyid);
						return 1;
					}
					break;
				}
				case 'm': {
					/* MIDI key input */
					int midi = atoi(&arg[2]);
					if(midi < 0 || midi >= 49) {
						printf("Invalid key %d\n", midi);
						return 1;
					}
					keyid = EMUKeyboardToKey(midi);
					break;
				}
				case 't':
					/* trace file */
					if(!arg[2]) {
						trc_file = "trace.trc";
					} else {
						trc_file = &arg[2];
					}
					break;
				case 'e':
					/* auto exit */
					auto_exit = TRUE;
					break;
				case 's':
					/* patch serial on the floppy */
					patch_serial = TRUE;
					break;
				case 'o':
					/* patch OS */
					os_file = &arg[2];
					break;
				case 'w':
					/* wildcard ROM */
					rom_file = "roms/wildcard.bin";
					break;
				case 'r':
					/* ROM file */
					rom_file = &arg[2];
					break;
				default:
					printf("Invalid option: '%s'\n", arg);
					return 1;
			}
		} else {
			if(fdd_image) {
				printf("Invalid positional argument: '%s'\n", arg);
				return 1;
			}
			fdd_image = arg;
		}
	}

	EMUInit(emulator, &ctx, rom_file);

	if(fdd_image) {
		EMULoadFloppy(emulator, fdd_image);
	} else {
		printf("No floppy image provided\n");
		return 1;
	}

	if(os_file) {
		FILE* os = fopen(os_file, "rb");
		if(!os) {
			printf("Error loading OS file %s: %s\n", os_file, strerror(errno));
			return 1;
		}
		fread(emulator->fdd.data, 2 * FDD_TRACK_SIZE, 1, os);
		fclose(os);
	}

	BOOL valid = FALSE;
	for(unsigned int i = 0; i < 0x100; i++) {
		if(emulator->fdd.data[i] != 0) {
			valid = TRUE;
			break;
		}
	}
	if(!valid) {
		printf("Invalid floppy image: no OS found\n");
		return 1;
	}

	if(patch_serial) {
		u8* floppy = emulator->fdd.data;
		floppy[3] = emulator->rom[0x5F];
		floppy[4] = emulator->rom[0x60];
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.context = (void*) emulator;
	ctx.read = z80read;
	ctx.write = z80write;
	ctx.in = z80in;
	ctx.out = z80out;
	ctx.int_data = z80int;

	if(trc_file) {
		printf("Opening trace file %s\n", trc_file);
		TRCInit(trc_file);
		TRCDump(0, emulator->rom, 1024);
	}

	printf("Starting Emulator...\n");

	z80_power(&ctx, TRUE);
	z80_reset(&ctx);

	/*for(unsigned int i = 0; i < 10000; i++) { */
	BOOL loc5 = FALSE;
	BOOL locBE = FALSE;
	BOOL loc78D = FALSE;

	BOOL triggered = FALSE;

	u8 old_i = 0;
	u8 old_im = 0;
	u8 old_ei = 0;

	unsigned int countdown = 0;
	unsigned int countdown_scan = 0;

	while(1) {
		/* printf("PC=%04X AF=%04X BC=%04X DE=%04X HL=%04X\n", ctx.state.pc, ctx.state.af.value_uint16, ctx.state.bc.value_uint16, ctx.state.de.value_uint16, ctx.state.hl.value_uint16); */
		TRCStep(emulator);
		if(old_i != ctx.state.i) {
			old_i = ctx.state.i;
			TRCSetI(ctx.state.i);
		}
		if(old_im != ctx.state.internal.im) {
			old_im = ctx.state.internal.im;
			TRCSetIM(old_im);
		}
		if(old_ei != ctx.state.internal.iff2) {
			old_ei = ctx.state.internal.iff2;
			TRCSetEI(old_ei);
		}
		z80_run(&ctx, 1);
		EMUStep(emulator);

		/* terminate on disk load error */
		if(ctx.state.pc == 5) {
			if(loc5) {
				break;
			} else {
				loc5 = TRUE;
			}
		}

		/* terminate in disk wait loop */
		if(ctx.state.pc == 0xBE) {
			if(locBE) {
				if(!countdown) {
					TROFF();
				} else {
					countdown--;
				}
			} else {
				locBE = TRUE;
				countdown = 10;
			}
		} else {
			TRON();
			locBE = FALSE;
		}

#define DLY 30
		/* scan function */
		if(ctx.state.pc == 0x078D) {
			if(loc78D) {
				if(!countdown_scan) {
					if(!triggered && keyid >= 0) {
						printf("PRESSING KEY %d\n", keyid);
						EMUPressKey(emulator, keyid);
						countdown_scan = DLY;
						triggered = TRUE;
					} else if(auto_exit) {
						printf("No action, stopping\n");
						break;
					}
				} else {
					countdown_scan--;
				}
			} else {
				loc78D = TRUE;
				countdown_scan = DLY;
			}
		}

		if(triggered && auto_exit) {
			/* check LED state */
			u16 leds = EMUGetLEDs(emulator);
			u8 seqleds = EMUGetSEQLEDs(emulator);
			if(leds != 0xFFFF || seqleds != 0xFF) {
				break;
			}
		}

		/* alloc_voice */
		if(ctx.state.pc == 0x1167) {
			/* argument is in register A */
			u8 a = ctx.state.af.value_uint16 >> 8;
			printf("alloc_voice(%u)\n", a);
		}
	}

	printf("Execution stopped\n");

	TRCClose();
	free(emulator);

	return 0;
}
