/*
 * EA Madden decompressor - adapted pointer-based implementation.
 * Assumes valid compressed input, as requested; input-end checks are omitted.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define EA_MADDEN_WINDOW       0x8000u
#define EA_MADDEN_MAIN_SYMBOLS 0x11Du
#define EA_MADDEN_DIST_SYMBOLS 0x1Eu
#define EA_MADDEN_MAX_CODE_BITS 15u
#define EA_FAST_BITS           8u
#define EA_FAST_SIZE           (1u << EA_FAST_BITS)

/*tables for the length codes and the distance codes
extracted directly from executable*/

static const uint8_t ea_length_extra[28] = 
{
    0,0,0,0,0,0,0,0, 1,1,1,1, 2,2,2,2,
    3,3,3,3, 4,4,4,4, 5,5,5,0
};

static const uint16_t ea_length_base[28] =
{
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,
    27,31,35,43,51,59,67,83,99,115,131,163,195,227
};

static const uint8_t ea_distance_extra[30] = 
{
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
    7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

static const uint16_t ea_distance_base[30] = 
{
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,
    193,257,385,513,769,1025,1537,2049,3073,
    4097,6145,8193,12289,16385,24577
};

/* Fill the bit buffer MSB-first. No source-end check: valid input assumed. */
#define EA_EnsureBits(qs, bitsleft, bitbuffer, count) \
do\
{ \
    while (bitsleft < count)\
	{ \
        uint32_t _a = *qs++; \
        bitbuffer |= _a << (24u - bitsleft); \
        bitsleft += 8u; \
    } \
} while (0)

#define GETBIT(qs, bitsleft, bitbuffer, bit) \
do \
{ \
    EA_EnsureBits(qs, bitsleft, bitbuffer, 1u); \
    bit = (bitbuffer >> 31) & 1u; \
    bitbuffer <<= 1; \
    --bitsleft; \
} while (0)

#define EA_GET_BITS_BUFFERED(qs, bitsleft, bitbuffer, count, value) \
do \
{ \
    unsigned _i; \
    value = 0; \
    for (_i = 0; _i < count; ++_i) \
	{ \
        EA_EnsureBits(qs, bitsleft, bitbuffer, 1u); \
        value = (value << 1) | (bitbuffer >> 31); \
        bitbuffer <<= 1; \
        --bitsleft; \
    } \
} while (0)

#define EA_READ_LENGTHS(qs, bitsleft, bitbuffer, qd, count_) \
do \
{ \
    unsigned _i; \
    uint8_t *_out = qd; \
    for (_i = 0; _i < count_; ++_i) \
	{ \
        uint32_t _v = 0; \
        EA_GET_BITS_BUFFERED(qs, bitsleft, bitbuffer, 4u, _v); \
        *_out++ = (uint8_t)_v; \
    } \
} while (0)

#define EA_DECODE_LENGTH(qs, bitsleft, bitbuffer, code, length) \
do \
{ \
    unsigned _extra_bits; \
    uint32_t _extra = 0; \
    if (code < 8u) \
	{ \
        length = code + 3u; \
    } \
	else \
	{ \
        _extra_bits = ea_length_extra[code]; \
        EA_GET_BITS_BUFFERED(qs, bitsleft, bitbuffer, _extra_bits, _extra); \
        if (_extra_bits == 0u) length = 0xE3u; \
        else length = (unsigned)ea_length_base[code] + (unsigned)_extra; \
    } \
} while (0)

#define EA_DECODE_DISTANCE(qs, bitsleft, bitbuffer, code, distance) \
do \
{ \
    unsigned _extra_bits; \
    uint32_t _extra = 0; \
    if (code < 4u) \
	{ \
        distance = code + 1u; \
    } \
	else \
	{ \
        _extra_bits = ea_distance_extra[code]; \
        EA_GET_BITS_BUFFERED(qs, bitsleft, bitbuffer, _extra_bits, _extra); \
        distance = (unsigned)ea_distance_base[code] + (unsigned)_extra; \
    } \
} while (0)

/*
 * Huffman node layout: [left child, right child, symbol]. A negative child
 * means absent. The H/D variables below are intentionally accessed directly.
 */
#define EA_HUFF_BUILD_MAIN(lengths) \
do \
{ \
    unsigned count[EA_MADDEN_MAX_CODE_BITS + 1u] = {0}; \
    uint16_t table[EA_MADDEN_MAX_CODE_BITS + 1u] = {0}; \
    uint16_t next[EA_MADDEN_MAX_CODE_BITS + 1u] = {0}; \
    uint32_t code = 0; \
    unsigned i; \
    Hsymbol_count = EA_MADDEN_MAIN_SYMBOLS; \
    Hnode_count = 1u; \
    Hmax_bits = 0u; \
    for (i = 0; i < EA_MADDEN_MAIN_SYMBOLS; ++i) \
	{ \
        Hlengths[i] = lengths[i]; \
        if (Hlengths[i] != 0u) ++count[Hlengths[i]]; \
        if (Hlengths[i] > Hmax_bits) Hmax_bits = Hlengths[i]; \
        Hcodes[i] = 0; \
    } \
    count[0] = 0; \
    for (i = 0; i < EA_MADDEN_MAX_CODE_BITS; ++i) \
	{ \
        code = (code + count[i]) << 1; \
        table[i + 1u] = (uint16_t)code; \
    } \
    memcpy(next, table, sizeof(next)); \
    for (i = 0; i < EA_MADDEN_MAIN_SYMBOLS; ++i) \
	{ \
        unsigned len = Hlengths[i]; \
        if (len != 0u) \
		{\
			Hcodes[i] = next[len]; \
			next[len] = (uint16_t)(next[len] + 1u); \
		}\
    } \
    Hnodes[0] = Hnodes[1] = Hnodes[2] = -1; \
    for (i = 0; i < EA_MADDEN_MAIN_SYMBOLS; ++i) \
	{ \
        unsigned len = Hlengths[i], bit_index; \
        int current_node; \
        if (len == 0u) continue; \
        current_node = 0; \
        for (bit_index = len; bit_index != 0u; --bit_index) \
		{ \
            unsigned bit = (Hcodes[i] >> (bit_index - 1u)) & 1u; \
            int next_node = Hnodes[current_node * 3 + (int)bit]; \
            if (next_node < 0) \
			{ \
                next_node = (int)Hnode_count++; \
                Hnodes[next_node * 3] = -1; \
                Hnodes[next_node * 3 + 1] = -1; \
                Hnodes[next_node * 3 + 2] = -1; \
                Hnodes[current_node * 3 + (int)bit] = next_node; \
            } \
            current_node = next_node; \
        } \
        Hnodes[current_node * 3 + 2] = (int)i; \
    } \
} while (0)

#define EA_HUFF_BUILD_DIST(lengths) \
do \
{ \
    unsigned count[EA_MADDEN_MAX_CODE_BITS + 1u] = {0}; \
    uint16_t table[EA_MADDEN_MAX_CODE_BITS + 1u] = {0}; \
    uint16_t next[EA_MADDEN_MAX_CODE_BITS + 1u] = {0}; \
    uint32_t code = 0; \
    unsigned i; \
    Dsymbol_count = EA_MADDEN_DIST_SYMBOLS; \
    Dnode_count = 1u; \
    Dmax_bits = 0u; \
    for (i = 0; i < EA_MADDEN_DIST_SYMBOLS; ++i) \
	{ \
        Dlengths[i] = lengths[i]; \
        if (Dlengths[i] != 0u) ++count[Dlengths[i]]; \
        if (Dlengths[i] > Dmax_bits) Dmax_bits = Dlengths[i]; \
        Dcodes[i] = 0; \
    } \
    count[0] = 0; \
    for (i = 0; i < EA_MADDEN_MAX_CODE_BITS; ++i) \
	{ \
        code = (code + count[i]) << 1; \
        table[i + 1u] = (uint16_t)code; \
    } \
    memcpy(next, table, sizeof(next)); \
    for (i = 0; i < EA_MADDEN_DIST_SYMBOLS; ++i) \
	{ \
        unsigned len = Dlengths[i]; \
        if (len != 0u) \
		{\
			Dcodes[i] = next[len]; \
			next[len] = (uint16_t)(next[len] + 1u); \
		}\
    } \
    Dnodes[0] = Dnodes[1] = Dnodes[2] = -1; \
    for (i = 0; i < EA_MADDEN_DIST_SYMBOLS; ++i) \
	{ \
        unsigned len = Dlengths[i], bit_index; \
        int current_node; \
        if (len == 0u) continue; \
        current_node = 0; \
        for (bit_index = len; bit_index != 0u; --bit_index) \
		{ \
            unsigned bit = (Dcodes[i] >> (bit_index - 1u)) & 1u; \
            int next_node = Dnodes[current_node * 3 + (int)bit]; \
            if (next_node < 0) \
			{ \
                next_node = (int)Dnode_count++; \
                Dnodes[next_node * 3] = -1; \
                Dnodes[next_node * 3 + 1] = -1; \
                Dnodes[next_node * 3 + 2] = -1; \
                Dnodes[current_node * 3 + (int)bit] = next_node; \
            } \
            current_node = next_node; \
        } \
        Dnodes[current_node * 3 + 2] = (int)i; \
    } \
} while (0)

/* Fast tables: quick_len == 0 means use the corresponding Huffman tree. */
#define EA_HUFF_FAST_DECODE_MAIN(Symbol) \
do \
{ \
    EA_EnsureBits(src, bitsleft, bitbuffer, EA_FAST_BITS); \
    unsigned _index = bitbuffer >> 24; \
    unsigned _used = main_quicklen[_index]; \
    if (_used != 0u)\
	{ \
        Symbol = main_fast[_index]; \
        bitbuffer <<= _used; \
        bitsleft -= _used; \
    } \
	else \
	{ \
        unsigned _depth; \
        int _node = 0; \
        for (_depth = 0; _depth < Hmax_bits; ++_depth) \
		{ \
            unsigned _bit; \
            EA_EnsureBits(src, bitsleft, bitbuffer, 1u); \
            _bit = bitbuffer >> 31; \
            bitbuffer <<= 1; \
            --bitsleft; \
            _node = Hnodes[_node * 3 + (int)_bit]; \
            if (Hnodes[_node * 3 + 2] >= 0) \
			{\
				Symbol = (uint16_t)Hnodes[_node * 3 + 2]; \
				break; \
			}\
        } \
    } \
} while (0)

#define EA_HUFF_FAST_DECODE_DIST(Symbol) \
do \
{ \
    EA_EnsureBits(src, bitsleft, bitbuffer, EA_FAST_BITS); \
    unsigned _index = bitbuffer >> 24; \
    unsigned _used = dist_quicklen[_index]; \
    if (_used != 0u) \
	{ \
        Symbol = dist_fast[_index]; \
        bitbuffer <<= _used; \
        bitsleft -= _used; \
    } \
	else \
	{ \
        unsigned _depth; \
        int _node = 0; \
        for (_depth = 0; _depth < Dmax_bits; ++_depth) \
		{ \
            unsigned _bit; \
            EA_EnsureBits(src, bitsleft, bitbuffer, 1u); \
            _bit = bitbuffer >> 31; \
            bitbuffer <<= 1; \
            --bitsleft; \
            _node = Dnodes[_node * 3 + (int)_bit]; \
            if (Dnodes[_node * 3 + 2] >= 0) \
			{\
				Symbol = (uint16_t)Dnodes[_node * 3 + 2]; \
				break; \
			}\
        } \
    } \
} while (0)

int EA_Madden_Decode(const void *input, size_t input_size,
                     void *output, size_t output_capacity)
{
    uint8_t main_lengths[EA_MADDEN_MAIN_SYMBOLS];
    uint8_t dist_lengths[EA_MADDEN_DIST_SYMBOLS];
    uint8_t window[EA_MADDEN_WINDOW];

    /* Main Huffman tree state */
    unsigned Hnode_count, Hsymbol_count, Hmax_bits;
    uint8_t Hlengths[EA_MADDEN_MAIN_SYMBOLS];
    uint16_t Hcodes[EA_MADDEN_MAIN_SYMBOLS];
    int Hnodes[EA_MADDEN_MAIN_SYMBOLS * 2u * 3u];

    /* Distance Huffman tree state */
    unsigned Dnode_count, Dsymbol_count, Dmax_bits;
    uint8_t Dlengths[EA_MADDEN_DIST_SYMBOLS];
    uint16_t Dcodes[EA_MADDEN_DIST_SYMBOLS];
    int Dnodes[EA_MADDEN_DIST_SYMBOLS * 2u * 3u];

	//Fast tables
    uint16_t main_fast[EA_FAST_SIZE];
    uint8_t main_quicklen[EA_FAST_SIZE];
    uint16_t dist_fast[EA_FAST_SIZE];
    uint8_t dist_quicklen[EA_FAST_SIZE];

    uint8_t *dst;
    uint8_t *dst_len;
    const uint8_t *src_const;
    const uint8_t *src_end;
    const uint8_t *src;
    size_t window_pos = 0;
    unsigned bitsleft = 0;
    uint32_t bitbuffer = 0;

    if (!input || !output) return -1;
    if (output_capacity == 0u) return 0;

    dst = (uint8_t *)output;
    dst_len = dst + output_capacity;
    src_const = (const uint8_t *)input;
    src_end = src_const + input_size;
    src = src_const;
    (void)src_end; /* Input bounds are intentionally not checked here. */
    memset(window, 0, sizeof(window));

    while (dst < dst_len)
	{
        uint32_t mode;
        GETBIT(src, bitsleft, bitbuffer, mode);
        if (mode != 0u) return -10;

        EA_READ_LENGTHS(src, bitsleft, bitbuffer, main_lengths, EA_MADDEN_MAIN_SYMBOLS);
        EA_READ_LENGTHS(src, bitsleft, bitbuffer, dist_lengths, EA_MADDEN_DIST_SYMBOLS);

        EA_HUFF_BUILD_MAIN(main_lengths);
        EA_HUFF_BUILD_DIST(dist_lengths);
        (void)Hsymbol_count;
        (void)Dsymbol_count;

        memset(main_quicklen, 0, sizeof(main_quicklen));
        memset(dist_quicklen, 0, sizeof(dist_quicklen));

		//Make fast 8-bit tables for the main Huff tree
        {
            unsigned symbol;
            for (symbol = 0; symbol < EA_MADDEN_MAIN_SYMBOLS; ++symbol)
			{
                unsigned len = Hlengths[symbol];
                unsigned prefix, repetitions, j;

                if (len == 0u || len > EA_FAST_BITS) continue;
                prefix = (unsigned)Hcodes[symbol] << (EA_FAST_BITS - len);
                repetitions = 1u << (EA_FAST_BITS - len);

                for (j = 0; j < repetitions; ++j)
				{
                    main_fast[prefix + j] = (uint16_t)symbol;
                    main_quicklen[prefix + j] = (uint8_t)len;
                }
            }
        }

		//Make fast 8-bit tables for the Distance Huff tree
        {
            unsigned symbol;
            for (symbol = 0; symbol < EA_MADDEN_DIST_SYMBOLS; ++symbol)
			{
                unsigned len = Dlengths[symbol];
                unsigned prefix, repetitions, j;

                if (len == 0u || len > EA_FAST_BITS) continue;
                prefix = (unsigned)Dcodes[symbol] << (EA_FAST_BITS - len);
                repetitions = 1u << (EA_FAST_BITS - len);

                for (j = 0; j < repetitions; ++j)
				{
                    dist_fast[prefix + j] = (uint16_t)symbol;
                    dist_quicklen[prefix + j] = (uint8_t)len;
                }
            }
        }

        for (;;)
		{
            uint16_t symbol = 0;
            EA_HUFF_FAST_DECODE_MAIN(symbol);

            if (symbol < 0x100u)
			{
                uint8_t b = (uint8_t)symbol;
                *dst++ = b;
                window[window_pos] = b;
                window_pos = (window_pos + 1u) & (EA_MADDEN_WINDOW - 1u);
                if (dst >= dst_len) return (int)(dst - (uint8_t *)output);
                continue;
            }

            if (symbol == 0x100u) break;

            {
                unsigned length_code = (unsigned)symbol - 0x101u;
                unsigned length, distance, distance_code = 0, i;
                EA_DECODE_LENGTH(src, bitsleft, bitbuffer, length_code, length);
                EA_HUFF_FAST_DECODE_DIST(distance_code);
                EA_DECODE_DISTANCE(src, bitsleft, bitbuffer, distance_code, distance);

                for (i = 0; i < length; ++i)
				{
                    uint8_t b;
                    if (dst >= dst_len) return (int)(dst - (uint8_t *)output);
                    b = window[(window_pos - distance) & (EA_MADDEN_WINDOW - 1u)];
                    *dst++ = b;
                    window[window_pos] = b;
                    window_pos = (window_pos + 1u) & (EA_MADDEN_WINDOW - 1u);
                }
                if (dst >= dst_len) return (int)(dst - (uint8_t *)output);
            }
        }
    }

    return (int)(dst - (uint8_t *)output);
}

