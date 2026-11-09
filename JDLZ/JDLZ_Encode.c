/*
 * JDLZ compressor - decompiled from SLES_535.58 (NFS Most Wanted, PS2, PAL)
 *
 * Original ELF addresses (stripped binary, MIPS R5900, little endian):
 *   0x0031c530  match_length()        compare two buffers, return common prefix length
 *   0x0031c5b0  ctx_init()            allocate match-finder context (nodes + hash heads)
 *   0x0031c618  ctx_free()
 *   0x0031c688  hash3()               3-byte hash
 *   0x0031c6b8  ctx_insert()          insert position into hash chain (evicting old node)
 *   0x0031c7a8  ctx_head()            first node of the chain for hash(ptr)
 *   0x0031c7e0  jdlz_compress()       <<< the compression function
 *   0x0031cb88  (decompressor, not covered here)
 *   0x0031fbf0  wrapper that calls jdlz_compress() at 0x31fc24 and keeps the
 *               smaller of the compressed / fallback result
 *
 * Original signature (MIPS regs a0,a1,a2):
 *     int jdlz_compress(const uint8_t *src, int src_len, uint8_t *dst);
 * Returns the compressed size (header included); also stored in header+0xC.
 *
 * Output format:
 *   +0x00  'J''D''L''Z'
 *   +0x04  u8  2
 *   +0x05  u8  0x10   (header size)
 *   +0x06  u16 0
 *   +0x08  u32 uncompressed size
 *   +0x0C  u32 compressed size
 *   +0x10  [flag1 byte][flag2 byte] then data; new flag bytes are placed inline
 *          in the stream at the position where the previous one filled up.
 *
 *   flag1 bit (LSB first): 1 = match, 0 = literal byte
 *   flag2 bit (only consumed on matches): 1 = short-distance form, 0 = long form
 *
 *   short match (2 bytes):  b0 = (((len-3)>>8)<<4) | (dist-1)      dist 1..16
 *                           b1 = (len-3) & 0xFF                    len 3..0x1002
 *   long  match (2 bytes):  b0 = (((dist-0x11)>>8)<<5) | (len-3)   len 3..0x22
 *                           b1 = (dist-0x11) & 0xFF                dist 0x11..0x810
 *
 * Notes on the original behaviour that are kept on purpose:
 *   - the main loop is "while (remaining >= 0)", so one extra literal is emitted
 *     after the last real byte (reads 1 byte past src_len; harmless for the decoder
 *     because it stops at the uncompressed size). Give src a few bytes of padding.
 *   - the hash/chain search reads up to 3 bytes beyond the current position.
 *   - the original has dead "progress" code: every 0x2000 tokens it performs a
 *     division by src_len (trap if zero) whose result is never used. Omitted here.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "JDLZ_Encode.h"

/* 0x31c688 */
static unsigned hash3(const uint8_t *p)
{
    unsigned h = ((unsigned)p[2] << 8) ^ ((unsigned)p[1] << 4) ^ p[0];
    return (h * 0x9E5F) & 0x1FFF;
}

/* 0x31c530 */
static int match_length(const uint8_t *a, const uint8_t *b, int max_len)
{
    int n = 0;
    while (n < max_len && a[n] == b[n])
        n++;
    return n;
}

/* 0x31c5b0 */
static int ctx_init(JdlzCtx *c, int node_count)
{
    c->node_count = node_count;
    c->nodes = (JdlzNode *)calloc((size_t)node_count, sizeof(JdlzNode));
    c->heads = (JdlzNode **)calloc(JDLZ_HASH_SIZE, sizeof(JdlzNode *));
    return c->nodes && c->heads;
}

/* 0x31c618 */
static void ctx_free(JdlzCtx *c)
{
    free(c->nodes);
    free(c->heads);
}

/* 0x31c7a8 */
static JdlzNode *ctx_head(JdlzCtx *c, const uint8_t *p)
{
    return c->heads[hash3(p)];
}

/* 0x31c6b8 */
static void ctx_insert(JdlzCtx *c, const uint8_t *ptr, int pos)
{
    JdlzNode *n = &c->nodes[pos % c->node_count];

    if (n->ptr) {                               /* slot in use: evict old entry */
        if (n->next) n->next->prev = n->prev;
        if (n->prev) n->prev->next = n->next;
        unsigned h = hash3(n->ptr);
        if (c->heads[h] == n)
            c->heads[h] = n->next;
    }

    n->pos = pos;
    n->ptr = ptr;

    unsigned h = hash3(ptr);
    n->prev = NULL;
    n->next = c->heads[h];
    c->heads[h] = n;
    if (n->next)
        n->next->prev = n;
}

/* 0x31c7e0 */
int JDLZ_Encode(const uint8_t *src, int src_len, uint8_t *dst)
{
    JdlzCtx ctx;
    if (!ctx_init(&ctx, JDLZ_WINDOW_NODES))
        return 0;

    /* header */
    dst[0] = 'J'; dst[1] = 'D'; dst[2] = 'L'; dst[3] = 'Z';
    dst[4] = 2;
    dst[5] = 0x10;
    dst[6] = 0; dst[7] = 0;
    *(uint32_t *)(dst + 8)  = (uint32_t)src_len;
    *(uint32_t *)(dst + 12) = 0;

    uint8_t *flag1_ptr = dst + 0x10;
    uint8_t *flag2_ptr = dst + 0x11;
    uint8_t *out       = dst + 0x12;

    unsigned flag1 = 0xFF00;
    unsigned flag2 = 0xFF00;

    int pos       = 0;
    int remaining = src_len;
    const uint8_t *in = src;

    while (remaining >= 0) {
        int max_len  = (remaining < JDLZ_MAX_LEN) ? remaining : JDLZ_MAX_LEN;
        int best_len = 2;
        JdlzNode *best = NULL;

        /* walk the hash chain, newest -> oldest */
        for (JdlzNode *n = ctx_head(&ctx, in); n && best_len < JDLZ_MAX_LEN; n = n->next) {
            int len = match_length(n->ptr, in, max_len);
            if (len > best_len) {
                int dist = pos - n->pos;
                if (len < 0x23 || dist < 0x10 || best_len < 0x23) {
                    best_len = len;
                    best     = n;
                }
            }
        }

        int flag1_full, flag2_full;

        if (best_len >= 3) {
            int dist = pos - best->pos;

            flag1 = (flag1 >> 1) & 0xFFFF;               /* bit = 1 : match */

            if (dist - 1 < 0x10) {
                /* short-distance form */
                out[1] = (uint8_t)(best_len - 3);
                out[0] = (uint8_t)((((best_len - 3) >> 8) << 4) | (dist - 1));
                flag2  = flag2 >> 1;                     /* bit = 1 : short */
            } else {
                /* long-distance form */
                if (best_len >= 0x23)
                    best_len = JDLZ_LONG_MAX_LEN;
                int d = dist - 0x11;
                out[1] = (uint8_t)d;
                out[0] = (uint8_t)(((d >> 8) << 5) | (best_len - 3));
                flag2  = (flag2 >> 1) & 0x7F7F;          /* bit = 0 : long  */
            }
            out += 2;
            remaining -= best_len;

            flag1_full = flag1 < 0x100;
            flag2_full = flag2 < 0x100;

            for (int k = best_len; k > 0; k--) {
                ctx_insert(&ctx, in, pos);
                in++;
                pos++;
            }
        } else {
            /* literal */
            ctx_insert(&ctx, in, pos);
            flag1 = (flag1 >> 1) & 0xFF7F;               /* bit = 0 : literal */
            *out++ = *in;
            in++;
            pos++;
            remaining--;

            flag1_full = flag1 < 0x100;
            flag2_full = flag2 < 0x100;
        }

        if (flag1_full) {
            *flag1_ptr = (uint8_t)flag1;
            flag1_ptr  = out++;
            flag1      = 0xFF00;
        }
        if (flag2_full) {
            *flag2_ptr = (uint8_t)flag2;
            flag2_ptr  = out++;
            flag2      = 0xFF00;
        }
    }

    /* flush the partially filled flag bytes (align bits to the LSB) */
    int total = (int)(out - dst);

    if (flag1 & 0xFF00) {
        do { flag1 >>= 1; } while (flag1 & 0xFF00);
    }
    *flag1_ptr = (uint8_t)flag1;

    if (flag2 & 0xFF00) {
        do { flag2 >>= 1; } while (flag2 & 0xFF00);
    }
    *flag2_ptr = (uint8_t)flag2;

    *(uint32_t *)(dst + 12) = (uint32_t)total;

    ctx_free(&ctx);
    return total;
}

