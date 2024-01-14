#ifndef __EMULATOR_H__
#define __EMULATOR_H__

#include "z80.h"

#ifndef LED
#define LED(x)		(1 << ((x) - 1))
#endif

#define	LED_TF		LED(1)
#define	LED_SUS_UPR	LED(2)
#define	LED_SUS_LWR	LED(3)
#define	LED_DYN		LED(8)
#define	LED_MOD_LWR	LED(9)
#define	LED_SWAP	LED(10)
#define	LED_PUT		LED(11)
#define	LED_GET_UPR	LED(12)
#define	LED_GET_LWR	LED(13)
#define	LED_MOD_UPR	LED(14)
#define	LED_SPLITE	LED(15)
#define	LED_OVLITE	LED(16)

#define	LED_STORE	LED(6)
#define	LED_RECALL	LED(7)
#define	LED_STOP	LED(8)
#define	LED_B		LED(5)
#define	LED_SEQ_2	LED(4)
#define	LED_A		LED(3)
#define	LED_GET_SEQ	LED(2)
#define	LED_SEQ_1	LED(1)

#define	KEY_SUS_LWR	56
#define	KEY_SUS_UPR	58
#define	KEY_TF		60
#define	KEY_GET_UPR	64
#define	KEY_GET_LWR	65
#define	KEY_SWAP	66
#define	KEY_PUT		67
#define	KEY_MOD_LWR	68
#define	KEY_DYN		69
#define	KEY_MOD_UPR	71

#define	CPU_CLOCK	2500000	/* 2.5MHz */

#define	SIO_IRQVEC_TXE_B	0
#define	SIO_IRQVEC_EXI_B	1
#define	SIO_IRQVEC_RXNE_B	2
#define	SIO_IRQVEC_SPECIAL_B	3
#define	SIO_IRQVEC_TXE_A	4
#define	SIO_IRQVEC_EXI_A	5
#define	SIO_IRQVEC_RXNE_A	6
#define	SIO_IRQVEC_SPECIAL_A	7

typedef struct {
	/* WR0 */
	u8	ptrlatch;

	u8	crc_reset_code;

	/* WR1 */
	u8	exi_enable;
	u8	tx_int_enable;
	u8	rx_int_mode;

	/* WR3 */
	u8	rx_enable;

	/* WR5 */
	u8	rts;
	u8	dtr;

	/* WR6, WR7 */
	u16	sync_bits;

	/* inputs */
	u8	last_cts;
	u8	last_dcd;

	/* data input */
	u8	rx_data;

	u8	exi_pending;
	u8	rx_pending;
	u8	tx_pending;
	u8	rxne;
} SIOCH;

typedef struct {
	u8	vector;
	u8	status_affects_vector;

	SIOCH	channel_a;
	SIOCH	channel_b;
} Z80SIO;

#define	PIO_STATE_NORMAL	0
#define	PIO_STATE_DIR		1
#define	PIO_STATE_MASK		2

typedef struct {
	u8	input_a;
	u8	input_b;

	u8	output_a;
	u8	output_b;

	u8	vector_a;
	u8	vector_b;

	u8	dir_a;
	u8	dir_b;

	u8	mode_a;
	u8	mode_b;

	u8	state;
} Z80PIO;

typedef struct {
	u64	cycle_counter;

	u8	reset;
	u8	trigger;
	u8	edge;
	u8	prescaler;
	u8	mode;
	u8	interrupt;

	u8	time_constant;
	u8	counter;
} CTCCH;

typedef struct {
	u8	vector;
	u8	latch;
	u8	pending_irq;
	u8	irq_timer;

	CTCCH	channel[4];
} Z80CTC;

#define	FDD_TRACKS		35
#define	FDD_TRACK_SIZE		3584
#define	FDD_SIZE		(FDD_TRACKS * FDD_TRACK_SIZE)
#define	FDD_ROTATION		(CPU_CLOCK / 5)	/* 300 RPM = 5Hz */

typedef struct {
	u8	timer;

	u8	dir;
	u8	sel_mtr;

	u8	track;

	u16	state;

	u32	rotation;

	u8*	data;
} FDD;

typedef struct {
	u16	addr;
	u16	wc;

	u8	ff;
	u8	req;
	u8	mask;
	u8	transfer;
	u8	autoinit;
	u8	addr_dec;
	u8	mode;

	u8	timer;
} DMACH;

typedef struct {
	/* command register */
	u8	mem2mem;
	u8	ch0addrhold;
	u8	disable;
	u8	timing;
	u8	priority;
	u8	write_sel;
	u8	dreq;
	u8	dack;

	u8	mode;
	u8	status;

	u8	tmp;

	DMACH	channel[4];
} DMA;

typedef struct {
	u8	rom[1024];
	u8	ram[128 * 1024];

	u8	channel_cfg_h[8];
	u8	channel_cfg_l[8];

	u8	led_reg[3];
	u8	last_leds[3];

	u8	cpua16;
	u8	forc16;

	Z80SIO	sio;
	Z80PIO	pio;
	Z80CTC	ctc;

	FDD	fdd;

	DMA	dma[5];

	u8	irq;
	u8	kbdmux;

	u64	keyboard;
	u8	keyboard2;

	Z80*	z80;

	u64	cycle;
} Emulator;

void	EMUInit(Emulator* ctx, Z80* z80, const char* rom_file);
void	EMULoadFloppy(Emulator* ctx, const char* filename);

void	EMUPressKey(Emulator* ctx, u8 key);
void	EMUReleaseKey(Emulator* ctx, u8 key);
u8	EMUKeyboardToKey(u8 midi);

u16	EMUGetLEDs(Emulator* ctx);
u16	EMUGetSEQLEDs(Emulator* ctx);
void	EMUPrintLEDs(u16 led_ic112, u8 led_ic115);
void	EMUUpdateLEDs(Emulator* ctx);

void	EMUWritePIO(Emulator* ctx, BOOL ab, BOOL cd, u8 data);
void	EMUWriteSIO(Emulator* ctx, BOOL ab, BOOL cd, u8 data);

void	EMUStep(Emulator* ctx);

void	EMUStepFDD(Emulator* ctx);

void	EMUSetFDDMotor(Emulator* ctx, BOOL sel_mtr);
void	EMUSetFDDDirection(Emulator* ctx, BOOL dir);
void	EMUSetFDDStep(Emulator* ctx, BOOL step);

u8	z80read(void* context, u16 addr);
void	z80write(void* context, u16 addr, u8 data);
u8	z80in(void* context, u16 addr);
void	z80out(void* context, u16 addr, u8 data);
u32	z80int(void* context);
void	z80halt(void* context, BOOL state);

u32	getaddr(Emulator* ctx, u16 addr);

#endif
