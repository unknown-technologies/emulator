#ifndef __LOGIC_H__
#define __LOGIC_H__

static inline BOOL BIT(u16 bus, u8 bit)
{
	return bus & (1L << bit);
}

static inline BOOL IC74LS00(BOOL a, BOOL b)
{
	return !(a && b);
}

static inline BOOL IC74LS02(BOOL a, BOOL b)
{
	return !(a || b);
}

static inline BOOL IC74LS11(BOOL a, BOOL b, BOOL c)
{
	return a && b && c;
}

static inline BOOL IC74LS32(BOOL a, BOOL b)
{
	return a || b;
}

static inline void IC74LS126(BOOL a, BOOL c, BOOL* y)
{
	if(c) {
		*y = a;
	}
}

static inline u8 IC74LS138(BOOL a, BOOL b, BOOL c, BOOL g1, BOOL _g2a, BOOL _g2b)
{
	BOOL g = !_g2a && !_g2b && g1;
	if(g) {
		u8 i = (a ? 1 : 0) | (b ? 2 : 0) | (c ? 4 : 0);
		return ~(1 << i);
	} else {
		return 0xFF;
	}
}

static inline void IC74LS139(BOOL a, BOOL b, BOOL _g, BOOL* y0, BOOL* y1, BOOL* y2, BOOL* y3)
{
	if(!_g) {
		u8 bits = (a ? 1 : 0) | (b ? 2 : 0);
		*y0 = bits != 0;
		*y1 = bits != 1;
		*y2 = bits != 2;
		*y3 = bits != 3;
	} else {
		*y0 = TRUE;
		*y1 = TRUE;
		*y2 = TRUE;
		*y3 = TRUE;
	}
}

static inline BOOL IC74LS260(BOOL a, BOOL b, BOOL c, BOOL d, BOOL e)
{
	return !(a || b || c || d || e);
}

static inline void IC74LS273(u8 d, BOOL cs, u8* q)
{
	if(!cs) {
		*q = d;
	}
}

static inline void IC74LS367(u8 a, BOOL _oe, u8* y)
{
	if(!_oe) {
		*y = a;
	}
}

static inline void WIRE8(u8 wire, BOOL* w0, BOOL* w1, BOOL* w2, BOOL* w3, BOOL* w4, BOOL* w5, BOOL* w6, BOOL* w7)
{
	*w0 = wire & 1;
	*w1 = wire & 2;
	*w2 = wire & 4;
	*w3 = wire & 8;
	*w4 = wire & 16;
	*w5 = wire & 32;
	*w6 = wire & 64;
	*w7 = wire & 128;
}

#endif
