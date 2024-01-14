#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "types.h"
#include "emulator.h"
#include "trace.h"

/*
 * MEMORY MAP:
 *
 * 0000-03FF: ROM
 * 0400-FFFF: RAM
 *
 * I/O MAP:
 * A4-A6: CS lines with A7 for the enable signal
 *
 * IORD
 * --------------
 * A7 => KBDICS
 *
 * IORD
 * --------------
 * A[7..0]:
 *    80..FF: KBDICS
 *
 * (service manual p43)
 * IOWR
 * --------------
 * A[7..0]:
 *    00..0F: DMACS0
 *    10..1F: DMACS1
 *    20..2F: DMACS2
 *    30..3F: DMACS3
 *    40..4F: CTCCS
 *        40: CTC CH 0
 *        41: CTC CH 1
 *        42: CTC CH 2
 *        43: CTC CH 3
 *    50..5F: PIOCS
 *        50: PIO A:DATA
 *        51: PIO A:CTL
 *        52: PIO B:DATA
 *        53: PIO B:CTL
 *    60..6F: SIOCS
 *        60: SIO A:DATA
 *        61: SIO A:CTL
 *        62: SIO B:DATA
 *        63: SIO B:CTL
 *    70..7F: DMACS4
 *    80: CH0CSL
 *    81: CH0CSH
 *    82: CH1CSL
 *    83: CH1CSH
 *    84: CH2CSL
 *    85: CH2CSH
 *    86: CH3CSL
 *    87: CH3CSH
 *    88: CH4CSL
 *    89: CH4CSH
 *    8A: CH5CSL
 *    8B: CH5CSH
 *    8C: CH6CSL
 *    8D: CH6CSH
 *    8E: CH7CSL
 *    8F: CH7CSH
 *
 *    C0: LED0CS
 *    C1: LED1CS
 *    C2: RELCS
 *    C3: KBDCS
 *
 * PIO PORTS =>
 *    ASTB:    ADC READY
 *    A[7..0]: ADC INPUT
 *
 *    B[0]:    ~STEP
 *    B[1]:    ~DIR
 *    B[2]:    ~TK00
 *    B[3]:    MODLTB
 *    B[4]:    MODUTB
 *    B[5]:    CPUA16
 *    B[6]:    SIO/~PIO
 *    B[7]:    PDMA
 *
 * CHnCSH =>
 *    D[7..5]: VCF CTL (3bit)
 *    D[4]:    CHnA16
 *    D[3]:    GATEn
 *    D[2]:    ~RSTCHn
 *    D[1..0]: TIMER PRESET 9..8
 * CHnCSL =>
 *    D[7..0]: TIMER CONFIG 7..0
 *
 * KBDCS =>
 *    D[0]:    POTSEL
 *    D[1]:    TRIGPOT
 *    D[2]:    ~FORC16
 *    D[7..4]: KEYBOARD
 *
 * KBDICS =>
 *    D[7..0] = KBD RIBBON, pulled up by KEYBOARD[9]
 *
 * (service manual p50)
 * LED0CS: D[7..0]
 *    D[7] => LED1 (IC112)
 *    D[6] => LED2 (IC112)
 *    D[5] => LED4 (IC112), LED1 (IC115)
 *    D[4] => LED6 (IC112), LED3 (IC115)
 *    D[3] => LED7 (IC112), LED4 (IC115)
 *    D[2] => LED3 (IC112)
 *    D[1] => LED8 (IC112)
 *    D[0] => LED5 (IC112), LED2 (IC115)
 *
 * LED1CS: D[7..0]
 *    D[7] => LED15 (IC112)
 *    D[6] => LED16 (IC112)
 *    D[5] => LED14 (IC112)
 *    D[4] => LED12 (IC112)
 *    D[3] => LED9  (IC112)
 *    D[2] => LED13 (IC112)
 *    D[1] => LED10 (IC112)
 *    D[0] => LED11 (IC112)
 *
 * LED2CS: D[3..0]
 *    D[3] => LED6 (IC115)
 *    D[2] => LED7 (IC115)
 *    D[1] => LED9 (IC115)
 *    D[0] => LED5 (IC115)
 *
 * LED definition
 * --------------
 * IC112:
 *    LED1:  T/F
 *    LED2:  SUS UPR
 *    LED3:  SUS LWR
 *    LED8:  DYN
 *    LED9:  MOD LWR
 *    LED10: SWAP
 *    LED11: PUT
 *    LED12: GET UPR
 *    LED13: GET LWR
 *    LED14: MOD UPR
 *    LED15: SPLITE
 *    LED16: OVLITE
 *
 * IC115:
 *    LED1: SEQ 1
 *    LED2: GET SEQ
 *    LED3: A
 *    LED4: SEQ 2
 *    LED5: B
 *    LED6: STORE
 *    LED7: RECALL
 *    LED8: STOP
 *
 * IC123:
 *    LED1:  SEQ 1
 *    LED2:  GET SEQ
 *    LED3:  A
 *    LED4:  SEQ 2
 *    LED7:  B/STORE/RECALL/STOP (+)
 *    LED10: A/GET SEQ/SEQ 1/SEQ 2 (+)
 *    LED13: B/STOP (-)
 *    LED14: STROBE/ACCESSORY (-)
 *    LED15: RECALL/RELEASE (-)
 *
 */

/* descramble data bits:
 * 0 => 6
 * 1 => 2
 * 2 => 0
 * 3 => 1
 * 4 => 7
 * 5 => 5
 * 6 => 3
 * 7 => 4
 *
 * scramble address bits:
 * 0 => 4
 * 1 => 5
 * 2 => 0
 * 3 => 1
 * 4 => 2
 * 5 => 3
 * 6 => 9
 * 7 => 6
 * 8 => 8
 * 9 => 7
 */

/* #define DEBUG_FDD */
#define DEBUG_FDD_SEEK
/* #define DEBUG_PIO */
/* #define DEBUG_SIO */
/* #define DEBUG_CTC */
#define DEBUG_DMA
#define DEBUG_IGNORE_DMA4
/* #define DEBUG_DMA_TRANSFER */
/* #define DEBUG_LEDS */
/* #define DEBUG_KBD */
#define DEBUG_CH
/* #define DEBUG_IO */

static inline u8 EMUDescrambleData(u8 data)
{
	return ((data & 1) << 6)	/* 0 */
		| ((data & 2) << 1)	/* 1 */
		| ((data & 4) >> 2)	/* 2 */
		| ((data & 8) >> 2)	/* 3 */
		| ((data & 16) << 3)	/* 4 */
		| ((data & 32))		/* 5 */
		| ((data & 64) >> 3)	/* 6 */
		| ((data & 128) >> 3);	/* 7 */
}

static inline u16 EMUScrambleAddr(u16 addr)
{
	return ((addr & 1) << 4)	/* 0 */
		| ((addr & 2) << 4)	/* 1 */
		| ((addr & 4) >> 2)	/* 2 */
		| ((addr & 8) >> 2)	/* 3 */
		| ((addr & 16) >> 2)	/* 4 */
		| ((addr & 32) >> 2)	/* 5 */
		| ((addr & 64) << 3)	/* 6 */
		| ((addr & 128) >> 1)	/* 7 */
		| ((addr & 256))	/* 8 */
		| ((addr & 512) >> 2);	/* 9 */
}

void EMUInit(Emulator* ctx, Z80* z80, const char* rom_file)
{
	memset(ctx, 0, sizeof(Emulator));
	ctx->z80 = z80;

	ctx->led_reg[0] = 0xFF;
	ctx->led_reg[1] = 0xFF;
	ctx->led_reg[2] = 0xFF;
	ctx->last_leds[0] = 0xFF;
	ctx->last_leds[1] = 0xFF;
	ctx->last_leds[2] = 0xFF;
	ctx->forc16 = 0;

	u8* eprom = (u8*) malloc(1024);
	FILE* rom = fopen(rom_file, "rb");
	if(!rom) {
		printf("Error opening EPROM %s: %s\n", rom_file, strerror(errno));
		exit(1);
	}

	fread(eprom, 1024, 1, rom);
	fclose(rom);

	memset(ctx->rom, 0, 1024);

	for(int i = 0; i < 1024; i++) {
		u16 addr = EMUScrambleAddr(i);
		if(addr < 1024)
			ctx->rom[i] = EMUDescrambleData(eprom[addr]);
		else
			printf("FAIL: %04X => %04X\n", i, addr);
	}

	free(eprom);

	printf("EPROM serial: %02X%02X\n", ctx->rom[0x60], ctx->rom[0x5F]);

	memset(ctx->ram, 0, 128 * 1024);

	/* initialize PIO */
	ctx->pio.state = PIO_STATE_NORMAL;

	/* initialize FDD */
	ctx->fdd.rotation = 0;

	/* initialize DMA */
	for(unsigned int dmacs = 0; dmacs < 5; dmacs++) {
		DMA* dma = &ctx->dma[dmacs];
		for(unsigned int i = 0; i < 4; i++) {
			dma->channel[i].mask = 1;
		}
	}

	ctx->fdd.data = (u8*) malloc(FDD_SIZE);

#if 0
	/* report RELEASE and ACCESSORY from sequencer board */
	ctx->keyboard = _BV(48 + 5) | _BV(48 + 3);
#endif
}

void EMULoadFloppy(Emulator* ctx, const char* filename)
{
	printf("Loading floppy image from %s...\n", filename);

	FILE* fdd = fopen(filename, "rb");
	if(!fdd) {
		perror("fopen");
		exit(1);
	}

	fread(ctx->fdd.data, FDD_SIZE, 1, fdd);
	fclose(fdd);

	printf("Floppy serial: %02X%02X\n", ctx->fdd.data[4], ctx->fdd.data[3]);
}

void EMUPressKey(Emulator* ctx, u8 keyid)
{
	if(keyid < 64) {
		ctx->keyboard |= _BV(keyid);
	} else {
		ctx->keyboard2 |= _BV(keyid - 64);
	}
}

void EMUReleaseKey(Emulator* ctx, u8 keyid)
{
	if(keyid < 64) {
		ctx->keyboard &= ~_BV(keyid);
	} else {
		ctx->keyboard2 &= ~_BV(keyid - 64);
	}
}

u8 EMUKeyboardToKey(u8 midi)
{
	if(midi > 48) {
		return 0xFF;
	} else {
		return midi ^ 7;
	}
}

u16 EMUGetLEDs(Emulator* ctx)
{
	u16 led_ic112 = 0;
	if(ctx->led_reg[0] & _BV(7))
		led_ic112 |= LED(1);
	if(ctx->led_reg[0] & _BV(6))
		led_ic112 |= LED(2);
	if(ctx->led_reg[0] & _BV(5))
		led_ic112 |= LED(4);
	if(ctx->led_reg[0] & _BV(4))
		led_ic112 |= LED(6);
	if(ctx->led_reg[0] & _BV(3))
		led_ic112 |= LED(7);
	if(ctx->led_reg[0] & _BV(2))
		led_ic112 |= LED(3);
	if(ctx->led_reg[0] & _BV(1))
		led_ic112 |= LED(8);
	if(ctx->led_reg[0] & _BV(0))
		led_ic112 |= LED(5);

	if(ctx->led_reg[1] & _BV(7))
		led_ic112 |= LED(15);
	if(ctx->led_reg[1] & _BV(6))
		led_ic112 |= LED(16);
	if(ctx->led_reg[1] & _BV(5))
		led_ic112 |= LED(14);
	if(ctx->led_reg[1] & _BV(4))
		led_ic112 |= LED(12);
	if(ctx->led_reg[1] & _BV(3))
		led_ic112 |= LED(9);
	if(ctx->led_reg[1] & _BV(2))
		led_ic112 |= LED(13);
	if(ctx->led_reg[1] & _BV(1))
		led_ic112 |= LED(10);
	if(ctx->led_reg[1] & _BV(0))
		led_ic112 |= LED(11);

	return led_ic112;
}

u16 EMUGetSEQLEDs(Emulator* ctx)
{
	u8 led_ic115 = 0;
	if(ctx->led_reg[0] & _BV(0))
		led_ic115 |= LED(2);
	if(ctx->led_reg[0] & _BV(3))
		led_ic115 |= LED(4);
	if(ctx->led_reg[0] & _BV(4))
		led_ic115 |= LED(3);
	if(ctx->led_reg[0] & _BV(5))
		led_ic115 |= LED(1);
	if(ctx->led_reg[2] & _BV(0))
		led_ic115 |= LED(5);
	if(ctx->led_reg[2] & _BV(1))
		led_ic115 |= LED(8);
	if(ctx->led_reg[2] & _BV(2))
		led_ic115 |= LED(7);
	if(ctx->led_reg[2] & _BV(3))
		led_ic115 |= LED(6);

	return led_ic115;
}

void EMUPrintLEDs(u16 led_ic112, u8 led_ic115)
{
	printf("LEDs:");
	if(!(led_ic112 & LED_TF))
		printf(" T/F");
	if(!(led_ic112 & LED_SUS_UPR))
		printf(" SUS_UPR");
	if(!(led_ic112 & LED_SUS_LWR))
		printf(" SUS_LWR");
	if(!(led_ic112 & LED_DYN))
		printf(" DYN");
	if(!(led_ic112 & LED_MOD_LWR))
		printf(" MOD_LWR");
	if(!(led_ic112 & LED_SWAP))
		printf(" SWAP");
	if(!(led_ic112 & LED_PUT))
		printf(" PUT");
	if(!(led_ic112 & LED_GET_UPR))
		printf(" GET_UPR");
	if(!(led_ic112 & LED_GET_LWR))
		printf(" GET_LWR");
	if(!(led_ic112 & LED_MOD_UPR))
		printf(" MOD_UPR");
	if(!(led_ic112 & LED_SPLITE))
		printf(" SAMPLE");
	if(!(led_ic112 & LED_OVLITE))
		printf(" OVERLOAD");

	/* sequencer LEDs */
	if(!(led_ic115 & LED_STORE))
		printf(" STORE");
	if(!(led_ic115 & LED_RECALL))
		printf(" RECALL");
	if(!(led_ic115 & LED_STOP))
		printf(" STOP");
	if(!(led_ic115 & LED_B))
		printf(" B");
	if(!(led_ic115 & LED_SEQ_2))
		printf(" SEQ2");
	if(!(led_ic115 & LED_A))
		printf(" A");
	if(!(led_ic115 & LED_GET_SEQ))
		printf(" GET_SEQ");
	if(!(led_ic115 & LED_SEQ_1))
		printf(" SEQ1");
	printf("\n");
}

void EMUUpdateLEDs(Emulator* ctx)
{
	u16 led_ic112 = EMUGetLEDs(ctx);
	u8 led_ic115 = EMUGetSEQLEDs(ctx);
	EMUPrintLEDs(led_ic112, led_ic115);
}

void EMUUpdateState(Emulator* ctx)
{
	if(ctx->led_reg[0] != ctx->last_leds[0] || ctx->led_reg[1] != ctx->last_leds[1] || ctx->led_reg[2] != ctx->last_leds[2]) {
		ctx->last_leds[0] = ctx->led_reg[0];
		ctx->last_leds[1] = ctx->led_reg[1];
		ctx->last_leds[2] = ctx->led_reg[2];
		EMUUpdateLEDs(ctx);
	}
}

u8 EMUReceiveFDD(Emulator* ctx)
{
	FDD* fdd = &ctx->fdd;
#ifdef DEBUG_FDD
	printf("FDD RX: STATE=%X\n", fdd->state);
#endif

	switch(fdd->state) {
		case 0: /* track */
			fdd->state++;
			return fdd->track;
		case 1: /* first byte of CRC-16 */
			fdd->state++;
			return 0;
		case 2: /* second byte of CRC-16 */
			fdd->state++;
			return 0;
		case 3:
		case 4: /* gap */
			fdd->state++;
			return 0;
		default:
			u16 off = fdd->state - 5;
			if(off < FDD_TRACK_SIZE) {
				/* track data */
				fdd->state++;
				u8* trackdata = fdd->data + fdd->track * FDD_TRACK_SIZE;
				return trackdata[off];
			} else if(off + 2 < FDD_TRACK_SIZE) {
				/* CRC-16 */
				fdd->state++;
				off -= FDD_TRACK_SIZE;
				return 0xAA;
			} else {
				printf(":: FDD RX READING ZERO!\n");
			}
			return 0;
	}
}

void EMUStepFDDHead(Emulator* ctx, unsigned int dir)
{
	FDD* fdd = &ctx->fdd;

	if(dir) {
		fdd->track++;
	} else if(fdd->track > 0) {
		fdd->track--;
	}

	fdd->state = 0;

#if defined(DEBUG_FDD) || defined(DEBUG_FDD_SEEK)
	printf("FDD SEEK: TRK=%02d\n", fdd->track);
#endif
}

u8 EMUReadPIO(Emulator* ctx, BOOL ab, BOOL cd)
{
	Z80PIO* pio = &ctx->pio;
#ifdef DEBUG_PIO
	printf("PIO READ: %c/%c = ", !ab ? 'A' : 'B', cd ? 'C' : 'D');
#endif
	if(cd) {
		/* CTRL */
#ifdef DEBUG_PIO
		printf("00\n");
#endif
		return 0;
	} else {
		/* DATA */
		if(ab) {
			if(ctx->fdd.track == 0) {
				pio->input_b &= ~_BV(2);
			} else {
				pio->input_b |= _BV(2);
			}

			u8 result = (pio->input_b & pio->dir_b) | (pio->output_b & ~pio->dir_b);
#ifdef DEBUG_PIO
			if(!(pio->input_b & _BV(2))) {
				printf("%02X [TRK00]\n", result);
			} else {
				printf("%02X\n", result);
			}
#endif
			return result;
		} else {
#ifdef DEBUG_PIO
			printf("00\n");
#endif
			return 0;
		}
	}
}

void EMUWritePIO(Emulator* ctx, BOOL ab, BOOL cd, u8 data)
{
	Z80PIO* pio = &ctx->pio;
#ifdef DEBUG_PIO
	const char* modestr[4] = { "Output", "Input", "Bidirectional", "Bit Control" };
	printf("PIO WRITE: %c/%c = %02X\n", !ab ? 'A' : 'B', cd ? 'C' : 'D', data);
#endif
	if(cd) {
		/* CTRL */
		switch(pio->state) {
			case PIO_STATE_NORMAL:
				break;
			case PIO_STATE_DIR:
				if(ab) {
					pio->dir_b = data;
#ifdef DEBUG_PIO
					printf(":: PIO DIR B = %c%c%c%c%c%c%c%c\n",
							pio->dir_b & _BV(7) ? 'I' : 'O',
							pio->dir_b & _BV(6) ? 'I' : 'O',
							pio->dir_b & _BV(5) ? 'I' : 'O',
							pio->dir_b & _BV(4) ? 'I' : 'O',
							pio->dir_b & _BV(3) ? 'I' : 'O',
							pio->dir_b & _BV(2) ? 'I' : 'O',
							pio->dir_b & _BV(1) ? 'I' : 'O',
							pio->dir_b & _BV(0) ? 'I' : 'O');
#endif
				} else {
					pio->dir_a = data;
#ifdef DEBUG_PIO
					printf(":: PIO DIR A = %c%c%c%c%c%c%c%c\n",
							pio->dir_a & _BV(7) ? 'I' : 'O',
							pio->dir_a & _BV(6) ? 'I' : 'O',
							pio->dir_a & _BV(5) ? 'I' : 'O',
							pio->dir_a & _BV(4) ? 'I' : 'O',
							pio->dir_a & _BV(3) ? 'I' : 'O',
							pio->dir_a & _BV(2) ? 'I' : 'O',
							pio->dir_a & _BV(1) ? 'I' : 'O',
							pio->dir_a & _BV(0) ? 'I' : 'O');
#endif
				}
				pio->state = PIO_STATE_NORMAL;
				return;
			case PIO_STATE_MASK:
				pio->state = PIO_STATE_NORMAL;
				return;
			default:
				printf("FATAL: invalid PIO state %d\n", pio->state);
				break;
		}

		if(data & 1) {
			switch(data & 0x0F) {
				case 0x0F:
					/* set mode */
					if((data >> 6) == 3) {
						pio->state = PIO_STATE_DIR;
					}
					if(ab) {
						pio->mode_b = data >> 6;
#ifdef DEBUG_PIO
						printf(":: PIO MODE B = %X (%s)\n", pio->mode_b, modestr[pio->mode_b]);
#endif
					} else {
						pio->mode_a = data >> 6;
#ifdef DEBUG_PIO
						printf(":: PIO MODE A = %X (%s)\n", pio->mode_a, modestr[pio->mode_a]);
#endif

					}
					break;
				case 0x07:
					printf(":: PIO INTERRUPT CONTROL\n");
					break;
				case 0x03:
					printf(":: PIO INTERRUPT ENABLE\n");
					break;
			}
		} else {
			/* load interrupt vector */
			if(ab) {
				pio->vector_b = data;
#ifdef DEBUG_PIO
				printf(":: PIO VECTOR B: %02X\n", pio->vector_b);
#endif
			} else {
				pio->vector_a = data;
#ifdef DEBUG_PIO
				printf(":: PIO VECTOR A: %02X\n", pio->vector_a);
#endif
			}
		}
	} else {
		/* DATA */
		if(ab) {
			u8 old_out = pio->output_b;
			u8 dir = !(data & _BV(1));
			u8 step_pulse = ((data ^ old_out) & _BV(0)) && (old_out & _BV(0));
			pio->output_b = data;
			ctx->cpua16 = data & _BV(5);
#ifdef DEBUG_PIO
			printf(":: PIO OUTPUT B:");
			if(!(data & _BV(0))) {
				printf(" STEP");
			}
			if(!(data & _BV(1))) {
				printf(" DIR");
			}
			if(data & _BV(3)) {
				printf(" MODLTB");
			}
			if(data & _BV(4)) {
				printf(" MODUTB");
			}
			if(data & _BV(5)) {
				printf(" CPUA16");
			}
			if(data & _BV(6)) {
				printf(" SIO");
			} else {
				printf(" PIO");
			}
			if(data & _BV(7)) {
				printf(" PDMA");
			}
			printf("\n");
#endif

			if(step_pulse) {
				EMUStepFDDHead(ctx, dir);
			}

			EMUSetFDDStep(ctx, !(data & _BV(0)));
			EMUSetFDDDirection(ctx, !(data & _BV(1)));
		}
	}
}

u8 EMUReadSIO(Emulator* ctx, BOOL ab, BOOL cd)
{
	Z80SIO* sio = &ctx->sio;
	u8 result = 0;

#ifdef DEBUG_SIO
	char c = !ab ? 'A' : 'B';
#endif

	if(cd) {
		/* CTRL */
		SIOCH* ch = ab ? &sio->channel_b : &sio->channel_a;

		switch(ch->ptrlatch) {
			case 0:
				if(!ch->rxne && ch->rx_enable) {
					/* read from floppy */
					ch->rx_data = EMUReceiveFDD(ctx);
					ch->rxne = 1;
				}

				result = 0;
				if(ch->rxne) {
					result |= _BV(0);
				}
				if(ch->last_dcd) {
					result |= _BV(3);
				}
				if(ch->last_cts) {
					result |= _BV(5);
				}

				break;
			default:
				ch->ptrlatch = 0;
				break;
		}
	} else {
		/* DATA */
		SIOCH* ch = ab ? &sio->channel_b : &sio->channel_a;
		ch->rxne = 0;
		result = ch->rx_data;
	}

#ifdef DEBUG_SIO
	printf("SIO READ: %c/%c = %02X\n", c, cd ? 'C' : 'D', result);
#endif
	return result;
}

void EMUWriteSIO(Emulator* ctx, BOOL ab, BOOL cd, u8 data)
{
	Z80SIO* sio = &ctx->sio;
#ifdef DEBUG_SIO
	const char* CRC_RESET_CODE[4] = { "Null Code", "Reset RX CRC Checker", "Reset TX CRC Generator", "Reset TX Underrun/EOM latch" };
	const char* WR0_CMD[8] = { "NOP", "Send Abort", "Reset Ext/Status Interrupts", "Channel Reset", "Enable INT on next RX Char", "Reset TX INT Pending", "Error Reset", "Return from INT" };
	const char* RX_INT_MODE[4] = { "RX INT disable", "RX INT on first char", "INT on all RX chars (parity affects vector)", "INT on all RX chars (parity does not affect vector)" };
	const char* RX_SIZE[4] = { "5 Bits/Character", "7 Bits/Character", "6 Bits/Character", "8 Bits/Character" };
	const char* CLOCK_MODE[4] = { "X1", "X16", "X32", "X64" };
	const char* SYNC_MODE[4] = { "8 Bit Sync Char", "16 Bit Sync Char", "SDLC Mode", "External Sync Mode" };
	const char* STOP_MODE[4] = { "Sync modes enable", "1 stop bit/char", "1.5 stop bits/char", "2 stop bits/char" };
	const char* PARITY[2] = { "Odd", "Even" };
	const char* CRC_POLYNOMIAL[2] = { "SDLC", "CRC-16" };

	printf("SIO WRITE: %c/%c = %02X\n", !ab ? 'A' : 'B', cd ? 'C' : 'D', data);
#endif
	if(cd) {
		/* CTRL */
		SIOCH* ch = ab ? &sio->channel_b : &sio->channel_a;
#ifdef DEBUG_SIO
		char c = !ab ? 'A' : 'B';
#endif

		switch(ch->ptrlatch) {
			case 0: {
				u8 cmd = (data >> 3) & 0x07;
				switch(cmd) {
					/* TODO */
					case 0: /* null code */
						break;
					case 1: /* send abort */
						break;
					case 2: /* reset ext/status interrupts */
						ch->exi_pending = 0;
						break;
					case 3: /* channel reset */
						break;
					case 4: /* enable int on next rx char */
						break;
					case 5: /* reset tx int pending */
						break;
					case 6: /* error reset */
						break;
					case 7: /* return from int */
						break;
				}

				ch->ptrlatch = data & 0x07;
				ch->crc_reset_code = (data >> 6) & 0x03;
#ifdef DEBUG_SIO
				printf(":: SIO %c WR0: PTR=%X CMD=%X [%s] CRC_RST=%X [%s]\n", c, ch->ptrlatch, cmd, WR0_CMD[cmd], ch->crc_reset_code, CRC_RESET_CODE[ch->crc_reset_code]);
#endif
				break;
			}
			case 1:
				ch->ptrlatch = 0;
				ch->exi_enable = data & _BV(0);
				ch->tx_int_enable = data & _BV(1);
				if(ab) {
					sio->status_affects_vector = data & _BV(2);
				}
				ch->rx_int_mode = (data >> 3) & 0x03;
#ifdef DEBUG_SIO
				printf(":: SIO %c WR1: EXI_EN=%X TX_INT_EN=%X STATUS_AFFECTS_VECTOR=%X RX_INT_MODE=%X [%s]\n", c, !!ch->exi_enable, !!ch->tx_int_enable, !!sio->status_affects_vector, ch->rx_int_mode, RX_INT_MODE[ch->rx_int_mode]);
#endif
				break;
			case 2:
				if(ab) {
					sio->vector = data;
				}
				ch->ptrlatch = 0;
#ifdef DEBUG_SIO
				printf(":: SIO VECTOR = %02X\n", sio->vector);
#endif
				break;
			case 3:
				ch->ptrlatch = 0;
				ch->rx_enable = data & _BV(0);
#ifdef DEBUG_SIO
				printf(":: SIO %c WR3: RX=%X\n", c, !!ch->rx_enable);
#endif
				break;
			case 4:
				ch->ptrlatch = 0;
				break;
			case 5:
				ch->ptrlatch = 0;
				ch->rts = data & _BV(1);
				ch->dtr = data & _BV(7);
#ifdef DEBUG_SIO
				printf(":: SIO %c WR5: RTS=%X DTR=%X\n", c, !!ch->rts, !!ch->dtr);
#endif
				break;
			case 6:
				ch->ptrlatch = 0;
				ch->sync_bits = (ch->sync_bits & 0xFF00) | data;
#ifdef DEBUG_SIO
				printf(":: SIO %c WR6: SYNC[0..7]=%02X => SYNC=%04X\n", c, data, ch->sync_bits);
#endif
				break;
			case 7:
				ch->ptrlatch = 0;
				ch->sync_bits = (ch->sync_bits & 0x00FF) | ((u16) data << 8);
#ifdef DEBUG_SIO
				printf(":: SIO %c WR6: SYNC[8..15]=%02X => SYNC=%04X\n", c, data, ch->sync_bits);
#endif
				break;
		}
	} else {
		/* DATA */
	}
}

void EMUSIOEXI(Emulator* ctx, BOOL ab, BOOL cts, BOOL dcd)
{
	Z80SIO* sio = &ctx->sio;
	SIOCH* ch = ab ? &sio->channel_b : &sio->channel_a;

	if(ch->last_cts != cts || ch->last_dcd != dcd) {
#ifdef DEBUG_SIO
		/* printf("SIO EXI IRQ\n"); */
#endif
		ch->last_cts = cts;
		ch->last_dcd = dcd;

		if(ch->exi_enable) {
			if(sio->status_affects_vector) {
				u8 bits = ab ? SIO_IRQVEC_EXI_B : SIO_IRQVEC_EXI_A;
				ctx->irq = (sio->vector & 0xF1) | (bits << 1);
			} else {
				ctx->irq = sio->vector;
			}

#ifdef DEBUG_SIO
			printf("SIO SENDING IRQ: %02X\n", ctx->irq);
#endif
			z80_int(ctx->z80, TRUE);
		}
	}
}

void EMUReceiveSIO(Emulator* ctx, BOOL ab, u8 data)
{
	Z80SIO* sio = &ctx->sio;
	SIOCH* ch = ab ? &sio->channel_b : &sio->channel_a;
	if(ab) {
		/* channel B: ignore for now */
	} else {
		/* channel A: FDD */
		ch->rx_data = data;
		ch->rxne = 1;
	}
}


void EMUWriteCTC(Emulator* ctx, unsigned int channel, u8 data)
{
	Z80CTC* ctc = &ctx->ctc;
	CTCCH* ch = &ctc->channel[channel];
#ifdef DEBUG_CTC
	printf("CTC WRITE: %X = %02X\n", channel, data);
#endif

	if(ctc->latch) {
		ch->time_constant = data;
		ch->counter = data;
		ctc->latch = 0;
		ch->reset = 0;
#ifdef DEBUG_CTC
		printf(":: CTC %X TIME CONST = %02X\n", channel, ch->time_constant);
#endif
	} else if(!(data & 1)) {
		ctc->vector = data;
#ifdef DEBUG_CTC
		printf(":: CTC VECTOR = %02X\n", ctc->vector);
#endif
	} else {
		ctc->latch = data & _BV(2);

		ch->reset |= data & _BV(1);
		ch->trigger = data & _BV(3);
		ch->edge = data & _BV(4);
		ch->prescaler = data & _BV(5);
		ch->mode = data & _BV(6);
		ch->interrupt = data & _BV(7);

#ifdef DEBUG_CTC
		printf(":: CTC %X RESET=%X TRIG=%X [%s] EDGE=%X [%s] PRESCALER=%X [%d] MODE=%X [%s] INT=%X\n", channel, !!ch->reset, !!ch->trigger, ch->trigger ? "PULSE" : "AUTO", !!ch->edge, ch->edge ? "RISING" : "FALLING", !!ch->prescaler, ch->prescaler ? 256 : 16, !!ch->mode, ch->mode ? "COUNTER" : "TIMER", !!ch->interrupt);
#endif
	}
}

void EMUStepCTC(Emulator* ctx)
{
	Z80CTC* ctc = &ctx->ctc;
	u64 dcycles = ctx->z80->cycles;

	for(unsigned int i = 0; i < 4; i++) {
		CTCCH* ch = &ctc->channel[i];
		if(ch->reset) {
			continue;
		}

		if(!ch->trigger && !ch->mode && ch->time_constant) {
			/* timer is active */
			ch->cycle_counter += dcycles;

			u64 limit = ch->time_constant * (ch->prescaler ? 256 : 16) * 2;
			if(ch->cycle_counter >= limit) {
				ch->cycle_counter -= limit;
				/* IRQ */

				if(ch->interrupt) {
					ctc->pending_irq |= _BV(i);
				}
			}
		}
	}

	if(ctc->irq_timer) {
		ctc->irq_timer--;
		if(!ctc->irq_timer) {
			z80_int(ctx->z80, FALSE);
		}
	} else {
		for(unsigned int i = 0; i < 4; i++) {
			if(ctc->pending_irq & _BV(i)) {
				ctx->irq = (ctc->vector & 0xF1) | (i << 1);
#ifdef DEBUG_CTC
				printf("CTC SENDING IRQ: %02X\n", ctx->irq);
#endif
				z80_int(ctx->z80, TRUE);
				ctc->pending_irq &= ~_BV(i);
				ctc->irq_timer = 2;
				break;
			}
		}
	}
}

void EMUTriggerCTC(Emulator* ctx, unsigned int c)
{
	Z80CTC* ctc = &ctx->ctc;
	CTCCH* ch = &ctc->channel[c];

	if(ch->mode) {
		ch->counter--;
		if(!ch->counter) {
			ch->counter = ch->time_constant;
			if(ch->interrupt) {
				ctc->pending_irq |= _BV(c);
			}
		}
	}
}

void EMUWriteDMA(Emulator* ctx, unsigned int dmacs, u8 addr, u8 data)
{
	unsigned int c = (addr >> 1) & 0x03;
	DMA* dma = &ctx->dma[dmacs];
	DMACH* ch = &dma->channel[c];
#ifdef DEBUG_DMA
	const char* TRANSFER[4] = { "Verify transfer", "Write transfer", "Read transfer", "Illegal" };
	const char* MODE[4] = { "Demand", "Single", "Block", "Cascade" };
#ifdef DEBUG_IGNORE_DMA4
	if(dmacs != 4) {
#endif
	printf("DMA %X WRITE: %X = %02X\n", dmacs, addr, data);
#ifdef DEBUG_IGNORE_DMA4
	}
#endif
#endif

	switch(addr) {
		case 0x0:
		case 0x2:
		case 0x4:
		case 0x6: /* base & current address: ch0 */
			if(ch->ff) {
				ch->addr = (ch->addr & 0xFF) | ((u16) data << 8);
			} else {
				ch->addr = (ch->addr & 0xFF00) | data;
			}
			ch->ff = !ch->ff;
#ifdef DEBUG_DMA
#ifdef DEBUG_IGNORE_DMA4
			if(dmacs == 4) {
				break;
			}
#endif
			printf(":: DMA %X CH%X BASE=%04X [%02X]\n", dmacs, c, ch->addr, data);
#endif
			break;
		case 0x1:
		case 0x3:
		case 0x5:
		case 0x7: /* base & current word count */
			if(ch->ff) {
				ch->wc = (ch->wc & 0xFF) | ((u16) data << 8);
			} else {
				ch->wc = (ch->wc & 0xFF00) | data;
			}
			ch->ff = !ch->ff;
#ifdef DEBUG_DMA
#ifdef DEBUG_IGNORE_DMA4
			if(dmacs == 4) {
				break;
			}
#endif
			printf(":: DMA %X CH%X WC=%04X [%02X]\n", dmacs, c, ch->wc, data);
#endif
			break;
		case 0x8: /* write command register */
			dma->mem2mem = data & _BV(0);
			dma->ch0addrhold = data & _BV(1);
			dma->disable = data & _BV(2);
			dma->timing = data & _BV(3);
			dma->priority = data & _BV(4);
			dma->write_sel = data & _BV(5);
			dma->dreq = data & _BV(6);
			dma->dack = data & _BV(7);
#ifdef DEBUG_DMA
#ifdef DEBUG_IGNORE_DMA4
			if(dmacs == 4) {
				break;
			}
#endif
			printf(":: DMA %X MEM2MEM=%X CH0ADDRHOLD=%X DISABLE=%X TIMING=%X PRIO=%X WRSEL=%X DREQ=%X DACK=%X\n", dmacs, !!dma->mem2mem, !!dma->ch0addrhold, !!dma->disable, !!dma->timing, !!dma->priority, !!dma->write_sel, !!dma->dreq, !!dma->dack);
#endif
			break;
		case 0x9: /* write request register */
			c = data & 3;
			ch = &dma->channel[c];
			ch->req = data & _BV(2);
#ifdef DEBUG_DMA
#ifdef DEBUG_IGNORE_DMA4
			if(dmacs == 4) {
				break;
			}
#endif
			printf(":: DMA %X CH%X REQ=%X\n", dmacs, c, !!ch->req);
#endif
			break;
		case 0xA: /* write single mask register bit */
			c = data & 3;
			ch = &dma->channel[c];
			ch->mask = data & _BV(2);
			if(!ch->mask) {
				ch->timer = 0;
			}
#ifdef DEBUG_DMA
#ifdef DEBUG_IGNORE_DMA4
			if(dmacs == 4) {
				break;
			}
#endif
			printf(":: DMA %X CH%X MASK=%X\n", dmacs, c, !!ch->mask);
#endif
			break;
		case 0xB: /* write mode register */
			c = data & 3;
			ch = &dma->channel[c];
			ch->transfer = (data >> 2) & 0x03;
			ch->autoinit = data & _BV(4);
			ch->addr_dec = data & _BV(5);
			ch->mode = data >> 6;
#ifdef DEBUG_DMA
#ifdef DEBUG_IGNORE_DMA4
			if(dmacs == 4) {
				break;
			}
#endif
			printf(":: DMA %X CH%X TRANSFER=%X [%s] AUTOINIT=%X ADDRDEC=%X MODE=%X [%s]\n", dmacs, c, ch->transfer, TRANSFER[ch->transfer], !!ch->autoinit, !!ch->addr_dec, ch->mode, MODE[ch->mode]);
#endif
			break;
		case 0xC: /* clear byte pointer flip/flop */
			for(unsigned int i = 0; i < 4; i++) {
				dma->channel[i].ff = 0;
			}
#ifdef DEBUG_DMA
#ifdef DEBUG_IGNORE_DMA4
			if(dmacs == 4) {
				break;
			}
#endif
			printf(":: DMA %X CLEAR BYTE PTR FF\n", dmacs);
#endif
			break;
		case 0xD: /* master clear */
			memset(dma, 0, sizeof(DMA));
			for(unsigned int i = 0; i < 4; i++) {
				dma->channel[i].mask = 1;
			}
#ifdef DEBUG_DMA
#ifdef DEBUG_IGNORE_DMA4
			if(dmacs == 4) {
				break;
			}
#endif
			printf(":: DMA %X MASTER CLEAR\n", dmacs);
#endif
			break;
		case 0xF: /* write all mask register bits */
			for(unsigned int i = 0; i < 4; i++) {
				dma->channel[i].mask = data & _BV(i);
			}
#ifdef DEBUG_DMA
#ifdef DEBUG_IGNORE_DMA4
			if(dmacs == 4) {
				break;
			}
#endif
			printf(":: DMA %X MASK=%X\n", dmacs, data);
#endif
			break;
		default:
#ifdef DEBUG_DMA
			printf(":: DMA %X UNKNOWN CMD %X = %02X\n", dmacs, addr, data);
#endif
			break;
	}
}

void EMUStepDMA(Emulator* ctx)
{
	for(unsigned int dmacs = 0; dmacs < 5; dmacs++) {
		DMA* dma = &ctx->dma[dmacs];

		for(unsigned int i = 0; i < 4; i++) {
			DMACH* ch = &dma->channel[i];
			if(!ch->mask && ch->mode == 1) {
				if(ch->timer < 100) {
					ch->timer++;
				} else {
					/* perform transfer */
#if defined(DEBUG_DMA) && defined(DEBUG_DMA_TRANSFER)
					printf("DMA %X CH%X TRANSFER => %04X [WC=%04X]\n", dmacs, i, ch->addr, ch->wc);
#endif

					BOOL eop = ch->wc == 0;
					switch(dmacs << 4 | i) {
						case 0x00: { /* DMACS=0, CH=0 */
							u8 data = EMUReceiveFDD(ctx);

							u32 addr = ch->addr;
							if(dmacs == 0 && i == 0) {
								u32 chaa16 = (ctx->channel_cfg_h[0] & _BV(4)) ? 0x10000 : 0;
								addr |= chaa16;
							}

#if defined(DEBUG_DMA) && defined(DEBUG_DMA_TRANSFER)
							printf("DMA %X CH%X WR %05X = %02X\n", dmacs, i, addr, data);
#endif

							TRCWrite(addr, data);

							if(addr < 1024) {
								/* ignore write */
							} else {
								ctx->ram[addr] = data;
							}

							break;
						}
					}

					if(eop) {
						/* EOP */
						TRON();

						ch->mask = 1;
#if defined(DEBUG_DMA) && defined(DEBUG_DMA_TRANSFER)
						printf("DMA %X CH%X EOP\n", dmacs, i);
#endif

						if(dmacs == 0 && i == 0) {
							EMUTriggerCTC(ctx, 0);
						}
					} else {
						if(ch->addr_dec) {
							ch->addr--;
						} else {
							ch->addr++;
						}

						ch->wc--;

						ch->timer = 0;
					}
				}
			}
		}
	}
}

u32 getaddr(Emulator* ctx, u16 addr)
{
	u32 a = addr;

	if(addr < 1024) {
		return addr;
	}

	BOOL x = !((addr & 0xE000) && ctx->cpua16);
	BOOL y = !(x && ctx->forc16);

	if(y) {
		a |= 0x10000;
#if 0
		printf("==> A16 IS SET: ADDR=%05X [%04X] X=%X CPUA16=%X ~FORC16=%X\n", a, addr, !!x, !!ctx->cpua16, !!ctx->forc16);
#endif
	}

	return a;
}

u8 z80read(void* context, u16 addr)
{
	Emulator* ctx = (Emulator*) context;

	u32 a = getaddr(ctx, addr);
	u8 d = 0;
	if(a < 1024) {
		d = ctx->rom[a];
	} else {
		d = ctx->ram[a];
	}

	TRCRead(addr, d);
	return d;
}

void z80write(void* context, u16 addr, u8 data)
{
	Emulator* ctx = (Emulator*) context;
	u32 a = getaddr(ctx, addr);
	TRCWrite(a, data);

	if(a < 1024) {
		/* ignore write */
	} else {
		/* TODO: use the CPUA16 bit */
		ctx->ram[a] = data;
	}
}

u8 z80in(void* context, u16 addr)
{
	Emulator* ctx = (Emulator*) context;

	u8 result = 0;

	switch(addr & 0xFF) {
		case 0x50:
			result = EMUReadPIO(ctx, FALSE, FALSE);
			break;
		case 0x51:
			result = EMUReadPIO(ctx, FALSE, TRUE);
			break;
		case 0x52:
			result = EMUReadPIO(ctx, TRUE, FALSE);
			break;
		case 0x53:
			result = EMUReadPIO(ctx, TRUE, TRUE);
			break;
		case 0x60:
			result = EMUReadSIO(ctx, FALSE, FALSE);
			break;
		case 0x61:
			result = EMUReadSIO(ctx, FALSE, TRUE);
			break;
		case 0x62:
			result = EMUReadSIO(ctx, TRUE, FALSE);
			break;
		case 0x63:
			result = EMUReadSIO(ctx, TRUE, TRUE);
			break;
		case 0x80: /* KBDICS */
			if(ctx->kbdmux == 9) { /* which rows? */
				result = 0xFF; /* all keys ok */
			} else if(ctx->kbdmux == 8) {
				result = ctx->keyboard2;
			} else {
				result = (u8) (ctx->keyboard >> (ctx->kbdmux * 8));
			}
			break;
		default:
			result = 0;
#ifdef DEBUG_IO
			printf("UNKNOWN IN: %02X = %02X\n", addr & 0xFF, result);
#endif
			break;
	}

	TRCIn(addr, result);

	return result;
}

#ifdef DEBUG_CH
void EMUPrintCH(Emulator* ctx, unsigned int ch)
{
	u8 chncsl = ctx->channel_cfg_l[ch];
	u8 chncsh = ctx->channel_cfg_h[ch];
	u16 value = (((u16) chncsh) << 8) | chncsl;

	u16 timer = value & 0x3FF;
	u8 rst = chncsh & _BV(2);
	u8 gate = chncsh & _BV(3);
	u8 cha16 = chncsh & _BV(4);
	u8 fc = chncsh >> 5;

	double freq = 11.55e6 / (0xFFF - (0xC00 | timer));
	printf("TIMER=%03X [%8.2fHz] RST=%X GATE=%X CHnA16=%X FC=%X\n", timer, freq, !rst, !!gate, !!cha16, 7 - fc);
}
#endif

void z80out(void* context, u16 addr, u8 data)
{
	Emulator* ctx = (Emulator*) context;

	TRCOut(addr, data);

	switch(addr & 0xFF) {
		case 0x40:
			EMUWriteCTC(ctx, 0, data);
			break;
		case 0x41:
			EMUWriteCTC(ctx, 1, data);
			break;
		case 0x42:
			EMUWriteCTC(ctx, 2, data);
			break;
		case 0x43:
			EMUWriteCTC(ctx, 3, data);
			break;
		case 0x50:
			EMUWritePIO(ctx, FALSE, FALSE, data);
			break;
		case 0x51:
			EMUWritePIO(ctx, FALSE, TRUE, data);
			break;
		case 0x52:
			EMUWritePIO(ctx, TRUE, FALSE, data);
			break;
		case 0x53:
			EMUWritePIO(ctx, TRUE, TRUE, data);
			break;
		case 0x60:
			EMUWriteSIO(ctx, FALSE, FALSE, data);
			break;
		case 0x61:
			EMUWriteSIO(ctx, FALSE, TRUE, data);
			break;
		case 0x62:
			EMUWriteSIO(ctx, TRUE, FALSE, data);
			break;
		case 0x63:
			EMUWriteSIO(ctx, TRUE, TRUE, data);
			break;
		case 0x80:
			/* CH0CSL */
			ctx->channel_cfg_l[0] = data;
#ifdef DEBUG_CH
			printf("CH0CSL = %02X ", data);
			EMUPrintCH(ctx, 0);
#endif
			break;
		case 0x81:
			/* CH0CSH */
			ctx->channel_cfg_h[0] = data;
#ifdef DEBUG_CH
			printf("CH0CSH = %02X ", data);
			EMUPrintCH(ctx, 0);
#endif
			break;
		case 0x82:
			/* CH1CSL */
			ctx->channel_cfg_l[1] = data;
#ifdef DEBUG_CH
			printf("CH1CSL = %02X ", data);
			EMUPrintCH(ctx, 1);
#endif
			break;
		case 0x83:
			/* CH1CSH */
			ctx->channel_cfg_h[1] = data;
#ifdef DEBUG_CH
			printf("CH1CSH = %02X ", data);
			EMUPrintCH(ctx, 1);
#endif
			break;
		case 0x84:
			/* CH2CSL */
			ctx->channel_cfg_l[2] = data;
#ifdef DEBUG_CH
			printf("CH2CSL = %02X ", data);
			EMUPrintCH(ctx, 2);
#endif
			break;
		case 0x85:
			/* CH2CSH */
			ctx->channel_cfg_h[2] = data;
#ifdef DEBUG_CH
			printf("CH2CSH = %02X ", data);
			EMUPrintCH(ctx, 2);
#endif
			break;
		case 0x86:
			/* CH3CSL */
			ctx->channel_cfg_l[3] = data;
#ifdef DEBUG_CH
			printf("CH3CSL = %02X ", data);
			EMUPrintCH(ctx, 3);
#endif
			break;
		case 0x87:
			/* CH3CSH */
			ctx->channel_cfg_h[3] = data;
#ifdef DEBUG_CH
			printf("CH3CSH = %02X ", data);
			EMUPrintCH(ctx, 3);
#endif
			break;
		case 0x88:
			/* CH4CSL */
			ctx->channel_cfg_l[4] = data;
#ifdef DEBUG_CH
			printf("CH4CSL = %02X ", data);
			EMUPrintCH(ctx, 4);
#endif
			break;
		case 0x89:
			/* CH4CSH */
			ctx->channel_cfg_h[4] = data;
#ifdef DEBUG_CH
			printf("CH4CSH = %02X ", data);
			EMUPrintCH(ctx, 4);
#endif
			break;
		case 0x8A:
			/* CH5CSL */
			ctx->channel_cfg_l[5] = data;
#ifdef DEBUG_CH
			printf("CH5CSL = %02X ", data);
			EMUPrintCH(ctx, 5);
#endif
			break;
		case 0x8B:
			/* CH5CSH */
			ctx->channel_cfg_h[5] = data;
#ifdef DEBUG_CH
			printf("CH5CSH = %02X ", data);
			EMUPrintCH(ctx, 5);
#endif
			break;
		case 0x8C:
			/* CH6CSL */
			ctx->channel_cfg_l[6] = data;
#ifdef DEBUG_CH
			printf("CH6CSL = %02X ", data);
			EMUPrintCH(ctx, 6);
#endif
			break;
		case 0x8D:
			/* CH6CSH */
			ctx->channel_cfg_h[6] = data;
#ifdef DEBUG_CH
			printf("CH6CSH = %02X ", data);
			EMUPrintCH(ctx, 6);
#endif
			break;
		case 0x8E:
			/* CH7CSL */
			ctx->channel_cfg_l[7] = data;
#ifdef DEBUG_CH
			printf("CH7CSL = %02X ", data);
			EMUPrintCH(ctx, 7);
#endif
			break;
		case 0x8F:
			/* CH7CSH */
			ctx->channel_cfg_h[7] = data;
#ifdef DEBUG_CH
			printf("CH7CSH = %02X ", data);
			EMUPrintCH(ctx, 7);
#endif
			break;
		case 0xC0: /* LED0CS */
#ifdef DEBUG_LEDS
			printf("LED0CS = %02X\n", data);
#endif
			ctx->led_reg[0] = data;
			EMUUpdateState(ctx);
			/* EMUUpdateLEDs(ctx); */
			break;
		case 0xC1: /* LED1CS */
#ifdef DEBUG_LEDS
			printf("LED1CS = %02X\n", data);
#endif
			ctx->led_reg[1] = data;
			EMUUpdateState(ctx);
			/* EMUUpdateLEDs(ctx); */
			break;
		case 0xC2: /* RELCS */
#ifdef DEBUG_KBD
			printf("RELCS = %02X\n", data);
#endif
			break;
		case 0xC3: /* KBDCS */
			ctx->forc16 = data & _BV(5);
			ctx->kbdmux = data & 0x0F;
#ifdef DEBUG_KBD
			printf("KBDCS = %02X [FORC16=%X KBDMUX=%X]\n", data, !!ctx->forc16, ctx->kbdmux);
#endif
			break;
		default:
			if((addr & 0xFF) < 0x40 || (addr & 0xF0) == 0x70) {
				u8 dmacs = (addr & 0xFF) >> 4;
				if(dmacs == 7) {
					dmacs = 4;
				}
				EMUWriteDMA(ctx, dmacs, addr & 0x0F, data);
			} else {
#ifdef DEBUG_IO
				printf("UNKNOWN OUT: %02X = %02X\n", addr & 0xFF, data);
#endif
			}
			break;
	}
}

u32 z80int(void* context)
{
	Emulator* ctx = (Emulator*) context;

	u8 irq = ctx->irq;
	ctx->irq = 0;

#ifdef DEBUG_Z80
	printf("INT: %02X\n", irq);
#endif
	TRON();
	TRCIRQ(irq);

	z80_int(ctx->z80, FALSE);

	return irq;
}

void z80halt(void* context, BOOL state)
{
	printf("HALT!\n");
	exit(0);
}

void EMUStepFDD(Emulator* ctx)
{
	u64 dcycles = ctx->z80->cycles;
	FDD* fdd = &ctx->fdd;
	Z80SIO* sio = &ctx->sio;

	EMUSetFDDMotor(ctx, sio->channel_a.dtr);

	if(fdd->sel_mtr) {
		if(fdd->rotation > FDD_ROTATION) {
#if 0
			printf("FDD: ROTATION [%f]\n", ctx->cycle / (double) CPU_CLOCK);
#endif
			EMUSIOEXI(ctx, FALSE, FALSE, TRUE);
			fdd->rotation -= FDD_ROTATION;
			fdd->timer = 100;
		} else {
			fdd->rotation += dcycles;
		}

		if(fdd->timer) {
			fdd->timer--;
			if(!fdd->timer) {
#if 0
				printf("FDD: INDEX RESET\n");
#endif
				EMUSIOEXI(ctx, FALSE, FALSE, FALSE);
			}
		}
	}

}

void EMUSetFDDMotor(Emulator* ctx, BOOL sel_mtr)
{
	FDD* fdd = &ctx->fdd;

	fdd->sel_mtr = sel_mtr;
}

void EMUSetFDDDirection(Emulator* ctx, BOOL dir)
{
	FDD* fdd = &ctx->fdd;

	fdd->dir = dir;
}

void EMUSetFDDStep(Emulator* ctx, BOOL step)
{
	(void) ctx;
	(void) step;
}

void EMUStep(Emulator* ctx)
{
	ctx->cycle += ctx->z80->cycles;
	EMUStepFDD(ctx);
	EMUStepCTC(ctx);
	EMUStepDMA(ctx);
}
