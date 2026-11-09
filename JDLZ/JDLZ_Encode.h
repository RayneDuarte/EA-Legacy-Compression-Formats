#include <stdint.h>

#pragma once
#define JDLZ_WINDOW_NODES   0x810     /* 2064 nodes -> max distance 0x810        */
#define JDLZ_HASH_SIZE      0x2000    /* 8192 heads (0x8000 bytes in the ELF)    */
#define JDLZ_MAX_LEN        0x1002
#define JDLZ_LONG_MAX_LEN   0x22

typedef struct JdlzNode
{
    int               pos;    /* +0x00 absolute position in the input             */
    const uint8_t    *ptr;    /* +0x04 pointer to the data at that position       */
    struct JdlzNode  *prev;   /* +0x08 newer node in the same chain               */
    struct JdlzNode  *next;   /* +0x0C older node in the same chain               */
}
JdlzNode;

typedef struct JdlzCtx
{
    JdlzNode  *nodes;         /* ring buffer of JDLZ_WINDOW_NODES nodes           */
    JdlzNode **heads;         /* JDLZ_HASH_SIZE chain heads                       */
    int        node_count;
}
JdlzCtx;


int JDLZ_Encode(const uint8_t *src, int src_len, uint8_t *dst);
static void ctx_insert(JdlzCtx *c, const uint8_t *ptr, int pos);
static JdlzNode *ctx_head(JdlzCtx *c, const uint8_t *p);
static void ctx_free(JdlzCtx *c);
static int ctx_init(JdlzCtx *c, int node_count);
static int match_length(const uint8_t *a, const uint8_t *b, int max_len);
static unsigned hash3(const uint8_t *p);

