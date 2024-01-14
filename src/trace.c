#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "trace.h"
#include "z80info.h"

#ifndef EM_Z80
#define	EM_Z80		220
#endif

#define	TYPE_STEP	0
#define	TYPE_READ	1
#define	TYPE_WRITE	2
#define	TYPE_IN		3
#define	TYPE_OUT	4
#define	TYPE_DUMP	5
#define	TYPE_MAP	6
#define	TYPE_DEVICES	7
#define	TYPE_SET_I	8
#define	TYPE_SET_IM	9
#define	TYPE_SET_EI	10
#define	TYPE_IRQ	11
#define	TYPE_READ32	12
#define	TYPE_WRITE32	13

#define	DEV_PIO		0
#define	DEV_SIO		1
#define	DEV_CTC		2
#define	DEV_REG		3

typedef struct {
	u8	type;
	u8	codelen;
	u16	pc;
	u16	af;
	u16	bc;
	u16	de;
	u16	hl;
	u16	ix;
	u16	iy;
	u16	sp;
} STEP;

typedef struct {
	u32	addr;
	u32	len;
} DUMP;

typedef struct {
	u8	type;
	u8	namelen;
	u16	pad;
	u32	start;
	u32	len;
} MAP;

typedef struct {
	u8	type;
	u8	value;
	u16	addr;
} READWRITE;

typedef struct {
	u8	type;
	u8	value;
	u16	pad;
	u32	addr;
} READWRITE32;

typedef struct {
	u8	type;
	u8	value;
} SET;

typedef struct {
	u8	type;
	u8	count;
} DEVICES;

typedef struct {
	u8	type;
	u8	id;
	u8	pa_data;
	u8	pa_ctrl;
	u8	pb_data;
	u8	pb_ctrl;
} PIO;

typedef struct {
	u8	type;
	u8	id;
	u8	pa_data;
	u8	pa_ctrl;
	u8	pb_data;
	u8	pb_ctrl;
} SIO;

typedef struct {
	u8	type;
	u8	id;
	u8	port0;
	u8	port1;
	u8	port2;
	u8	port3;
} CTC;

static FILE* trcfile = NULL;
static BOOL trace = FALSE;

void TRON(void)
{
	trace = TRUE;
}

void TROFF(void)
{
	if(!trcfile) {
		return;
	}

	trace = FALSE;
	fflush(trcfile);
}

void TRCInit(const char* filename)
{
	trcfile = fopen(filename, "wb");
	if(!trcfile) {
		const char* msg = strerror(errno);
		printf("Error opening trace file: %s\n", msg);
		exit(1);
	}

	trace = TRUE;

	u16 cpu = U16B(EM_Z80);

	fwrite("XTRC", 4, 1, trcfile);
	fwrite(&cpu, 2, 1, trcfile);

	/* TRCMap(0, 1024, "ROM", 1); */
	/* TRCMap(1024, 65536 - 1024, "RAM", 0); */
	TRCMap(0, 128 * 1024, "MEM", 0);

	DEVICES devs = {
		.type = TYPE_DEVICES,
		.count = 3
	};

	fwrite(&devs, sizeof(DEVICES), 1, trcfile);

	PIO pio = {
		.type = DEV_PIO,
		.id = 1,
		.pa_data = 0x50,
		.pa_ctrl = 0x51,
		.pb_data = 0x52,
		.pb_ctrl = 0x53
	};
	fwrite(&pio, sizeof(PIO), 1, trcfile);

	SIO sio = {
		.type = DEV_SIO,
		.id = 2,
		.pa_data = 0x60,
		.pa_ctrl = 0x61,
		.pb_data = 0x62,
		.pb_ctrl = 0x63
	};
	fwrite(&sio, sizeof(SIO), 1, trcfile);

	CTC ctc = {
		.type = DEV_CTC,
		.id = 3,
		.port0 = 0x40,
		.port1 = 0x41,
		.port2 = 0x42,
		.port3 = 0x43
	};
	fwrite(&ctc, sizeof(CTC), 1, trcfile);
}

void TRCClose(void)
{
	if(!trace || !trcfile) {
		return;
	}
	fclose(trcfile);
	trace = FALSE;
}

void TRCMap(u32 addr, unsigned int len, const char* name, BOOL readonly)
{
	if(!trace || !trcfile) {
		return;
	}

	size_t namelen = strlen(name);

	MAP map = {
		.type = TYPE_MAP,
		.namelen = (u8) namelen | (readonly ? 0x80 : 0),
		.start = addr,
		.len = len
	};

	u8 buf[sizeof(MAP) + 64];
	memcpy(buf, &map, sizeof(MAP));
	memcpy(buf + sizeof(MAP), name, namelen);
	fwrite(buf, sizeof(MAP) + namelen, 1, trcfile);
}

void TRCRead(u32 addr, u8 value)
{
	if(!trace || !trcfile) {
		return;
	}

	if(addr < 0x10000) {
		READWRITE rw = {
			.type = TYPE_READ,
			.value = value,
			.addr = addr
		};

		fwrite(&rw, sizeof(READWRITE), 1, trcfile);
	} else {
		READWRITE32 rw = {
			.type = TYPE_READ32,
			.value = value,
			.addr = addr
		};

		fwrite(&rw, sizeof(READWRITE32), 1, trcfile);
	}
}

void TRCWrite(u32 addr, u8 value)
{
	if(!trcfile) {
		return;
	}

	if(addr < 0x10000) {
		READWRITE rw = {
			.type = TYPE_WRITE,
			.value = value,
			.addr = addr
		};

		fwrite(&rw, sizeof(READWRITE), 1, trcfile);
	} else {
		READWRITE32 rw = {
			.type = TYPE_WRITE32,
			.value = value,
			.addr = addr
		};

		fwrite(&rw, sizeof(READWRITE32), 1, trcfile);
	}
}

void TRCIn(u8 addr, u8 value)
{
	if(!trace || !trcfile) {
		return;
	}

	READWRITE rw = {
		.type = TYPE_IN,
		.value = value,
		.addr = addr
	};

	fwrite(&rw, sizeof(READWRITE), 1, trcfile);
}

void TRCOut(u8 addr, u8 value)
{
	if(!trace || !trcfile) {
		return;
	}

	READWRITE rw = {
		.type = TYPE_OUT,
		.value = value,
		.addr = addr
	};

	fwrite(&rw, sizeof(READWRITE), 1, trcfile);
}

void TRCDump(u32 addr, void* data, unsigned int len)
{
	if(!trace || !trcfile) {
		return;
	}

	DUMP dump = {
		.addr = addr,
		.len = len
	};

	u8 buf[sizeof(DUMP) + 1];
	buf[0] = TYPE_DUMP;
	memcpy(buf + 1, &dump, sizeof(DUMP));
	fwrite(buf, sizeof(buf), 1, trcfile);
	fwrite(data, len, 1, trcfile);
}

void TRCStep(Emulator* emu)
{
	if(!trace || !trcfile) {
		return;
	}

	Z80* ctx = emu->z80;

	u32 addr = getaddr(emu, ctx->state.pc);

	unsigned char* code = NULL;
	if(addr < 0x400) {
		code = &emu->rom[addr];
	} else if(addr < (128 * 1024)) {
		code = &emu->ram[addr];
	}

	int codelen = z80_codelen(code);
	if(codelen == 0) {
		printf("FAIL: invalid code detected\n");
		exit(1);
	}

	STEP step = {
		.type = TYPE_STEP,
		.codelen = codelen,
		.pc = ctx->state.pc,
		.af = ctx->state.af.value_uint16,
		.bc = ctx->state.bc.value_uint16,
		.de = ctx->state.de.value_uint16,
		.hl = ctx->state.hl.value_uint16,
		.ix = ctx->state.ix.value_uint16,
		.iy = ctx->state.iy.value_uint16,
		.sp = ctx->state.sp
	};

	u8 buf[sizeof(STEP) + 8];
	memcpy(buf, &step, sizeof(STEP));
	memcpy(buf + sizeof(STEP), code, codelen);
	fwrite(buf, sizeof(STEP) + codelen, 1, trcfile);
}

void TRCSetI(u8 value)
{
	if(!trace || !trcfile) {
		return;
	}

	SET set = {
		.type = TYPE_SET_I,
		.value = value
	};
	fwrite(&set, sizeof(SET), 1, trcfile);
}

void TRCSetIM(u8 value)
{
	if(!trace || !trcfile) {
		return;
	}

	SET set = {
		.type = TYPE_SET_IM,
		.value = value
	};
	fwrite(&set, sizeof(SET), 1, trcfile);
}

void TRCSetEI(u8 value)
{
	if(!trace || !trcfile) {
		return;
	}

	SET set = {
		.type = TYPE_SET_EI,
		.value = value
	};
	fwrite(&set, sizeof(SET), 1, trcfile);
}

void TRCIRQ(u8 value)
{
	if(!trace || !trcfile) {
		return;
	}

	SET set = {
		.type = TYPE_IRQ,
		.value = value
	};
	fwrite(&set, sizeof(SET), 1, trcfile);
}
