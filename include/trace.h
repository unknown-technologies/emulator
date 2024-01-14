#ifndef __TRACE_H__
#define __TRACE_H__

#include "types.h"
#include "emulator.h"

void	TRCInit(const char* filename);
void	TRCClose(void);
void	TRCMap(u32 addr, unsigned int len, const char* name, BOOL readonly);
void	TRCDump(u32 addr, void* data, unsigned int len);
void	TRCStep(Emulator* emu);
void	TRCRead(u32 addr, u8 value);
void	TRCWrite(u32 addr, u8 value);
void	TRCIn(u8 addr, u8 value);
void	TRCOut(u8 addr, u8 value);
void	TRCSetI(u8 value);
void	TRCSetIM(u8 value);
void	TRCSetEI(u8 value);
void	TRCIRQ(u8 value);

void	TRON(void);
void	TROFF(void);

#endif
