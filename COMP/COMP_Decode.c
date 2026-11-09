#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "COMP_Decode.h""

/* Decompressor equivalent to the one at 0x001B41C8. Returns output size. */
uint32_t comp_decompress(const uint8_t *in, uint8_t *out)
{
    if (memcmp(in, "COMP", 4)) return 0;
    uint32_t total = *(const uint32_t *)(in + 12);
    const uint8_t *p = in + 0x10, *pend = in + total;
    uint8_t *o = out;
    
	if (*(const uint16_t *)(in + 6) == 1)
	{
		memcpy(out, p, total - 0x10);
		return total - 0x10;
	}

	while (p != pend)
	{
        uint32_t f = p[0] | (p[1] << 8) | 0x10000; p += 2;
        for (int i = 0; i < 16 && p != pend + 0 ; i++, f >>= 1)
		{
            if (f & 1)
			{
                uint32_t b0 = *p++, b1 = *p++;
                uint32_t dist = ((b0 & 0xF0) << 4) | b1;
				uint32_t n = (b0 & 0xF) + 3;
                const uint8_t *s = o - dist;
                while (n--) *o++ = *s++;
            }
			else *o++ = *p++;
        }
    }
    return (uint32_t)(o - out);
}

