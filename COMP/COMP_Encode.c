/*
 * COMP (LZ77, 4 KiB window) compressor - reconstructed from the NFS: Hot Pursuit 2
 * (PS2, SLUS_203.62) function at 0x001B3DD0 ("..\indep\src\lzcompress.cpp").
 *
 * Stream layout
 *   +00  'C','O','M','P'
 *   +04  u8   version  = 1
 *   +05  u8   header size = 0x10
 *   +06  u16  flags    (1 = payload stored raw, 0 = LZ)
 *   +08  u32  uncompressed size
 *   +0C  u32  total file size (header + payload)
 *   +10  payload: groups of [u16 LE flag word][up to 16 tokens]
 *        flag bit i (LSB first): 0 = literal byte, 1 = match
 *        match = 2 bytes: b0 = ((dist>>4)&0xF0) | (len-3), b1 = dist&0xFF
 *                dist = 1..4095 (back-reference), len = 3..18
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "COMP_Encode.h"

#define HASH_BITS   15
#define WINDOW      0x1000
#define MAX_MATCH   18
#define MIN_MATCH   3

static inline uint32_t put32(uint8_t *p, uint32_t v)
{
	memcpy(p,&v,4);
	return v;
} /* LE host */

/* dst must have room for src_len + 0x40 bytes. Returns compressed size, 0 on bad args. */
uint32_t comp_compress(const uint8_t *src, uint32_t src_len, uint8_t *dst)
{
    if (!src || !src_len || !dst) return 0;

    const uint8_t **hash = calloc(1u << HASH_BITS, sizeof(*hash));   /* original: 0x8000 ptrs (0x20000 bytes) */
    const uint8_t *end   = src + src_len;
    const uint8_t *cur   = src;

    memcpy(dst, "COMP", 4);
    dst[4] = 1;
	dst[5] = 0x10;
	dst[6] = dst[7] = 0;
    put32(dst + 8,  src_len);
    put32(dst + 12, 0);

    uint8_t *out_limit = dst + src_len; /* if output grows past this -> store raw     */
    uint8_t *out  = dst + 0x12; /* first data byte (flag word is at dst+0x10) */
    uint8_t *flag = dst + 0x10; /* where the current flag word goes           */
    uint32_t bits = 0xFFFF0000u; /* sentinel-based 16-bit flag accumulator     */
    int      stored = 0;

    if (out_limit < out) stored = 1;

    while (!stored)
	{
        /* Re-evaluated after every group: 16 tokens while far from the end of input,
         * 1 token near the end, literals only in the last 18 bytes. */
        int batch, tail = 0;
        if (end - 0x120 >= cur)          batch = 16;     /* 16*18 = 288 = 0x120 bytes max   */
        else if (end - MAX_MATCH >= cur) batch = 1;
        else if (cur == end)             break;
        else                             batch = 1, tail = 1;

        for (int t = batch; t; t--)
		{
            if (tail)
			{  /* not enough lookahead to match  */
                bits &= 0xFFFEFFFFu;
				*out++ = *cur++;
            }
			else
			{
                uint32_t h = ((((uint32_t)cur[0] << 4) ^ cur[1]) << 4) ^ cur[2];
                h = (h * 0x9E5Fu) >> 4 & 0x7FFF;
                const uint8_t *old = hash[h];
                hash[h] = cur; /* only match/literal starts are hashed */
                uint32_t dist = (uint32_t)(cur - old);
                int len = 0;
                
				if (dist < WINDOW && old >= src && dist != 0)
				{
                    while (len < MAX_MATCH && old[len] == cur[len])
						len++;
                    if (len < MIN_MATCH) len = 0;
                }
                if (len)
				{
                    *out++ = (uint8_t)(((dist & 0xF00) >> 4) | (len - MIN_MATCH));
                    *out++ = (uint8_t)dist;
                    cur += len;  /* flag bit stays 1 */
                }
				else
				{
                    bits &= 0xFFFEFFFFu;
					*out++ = *cur++; /* literal: flag bit 0 */
                }
            }
            bits >>= 1;
        }
        if ((bits & 0xFFFF0000u) == 0)
		{                  /* 16 tokens collected: emit flag word */
            flag[0] = (uint8_t)bits;
			flag[1] = (uint8_t)(bits >> 8);
            flag = out;
			out += 2;
			bits = 0xFFFF0000u;
        }
        if (out_limit < out)
		{
			stored = 1;
			break;
		}  /* expanding -> fall back to raw copy */
    }

    uint32_t total;
    if (stored)
	{
        memcpy(dst + 0x10, src, src_len);
        dst[6] = 1;
        total = src_len + 0x10;
    }
	else
	{
        /* flush partial group: pad flag word out to 16 shifts */
        do bits >>= 1; while (bits & 0xFFFF0000u);
        flag[0] = (uint8_t)bits;
		flag[1] = (uint8_t)(bits >> 8);
        flag += 2;
        if (flag == out) out -= 2; /* last group was empty: drop reserved slot */
        total = (uint32_t)(out - dst);
    }
    put32(dst + 12, total);
    free(hash);
    return total;
}

