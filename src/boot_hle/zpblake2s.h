/*
 * zpblake2s.h - Minimal BLAKE2s (RFC 7693, unkeyed, 32-byte digest), strict
 * C89, header-only.
 *
 * Used by zpbuild to hash the DATA half of a code/data-split signed ROM
 * (see docs/zpb-format.md's v2 trailer and ZPbootROM/def88186/blake2s.def,
 * which is the on-device half of this same primitive). Uses unsigned long
 * masked to 32 bits, same convention as zpsha256.h, so it is portable to
 * any C89 host including the DOS toolchain.
 */
#ifndef ZP_BLAKE2S_H
#define ZP_BLAKE2S_H

#include <string.h>

typedef struct {
    unsigned long h[8];
    unsigned long t;         /* bytes hashed so far, low 32 bits */
    unsigned char buf[64];
    unsigned int  buflen;
} zp_blake2s;

#define ZPB_M32(x) ((x) & 0xFFFFFFFFUL)
#define ZPB_ROR(x,n) ZPB_M32((ZPB_M32(x) >> (n)) | ZPB_M32((x) << (32 - (n))))

static const unsigned long ZPB_IV[8] = {
0x6a09e667UL,0xbb67ae85UL,0x3c6ef372UL,0xa54ff53aUL,
0x510e527fUL,0x9b05688cUL,0x1f83d9abUL,0x5be0cd19UL };

static const unsigned char ZPB_SIGMA[10][16] = {
{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
{14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3},
{11,8,12,0,5,2,15,13,10,14,3,6,7,1,9,4},
{7,9,3,1,13,12,11,14,2,6,5,10,4,0,15,8},
{9,0,5,7,2,4,10,15,14,1,11,12,6,8,3,13},
{2,12,6,10,0,11,8,3,4,13,7,5,15,14,1,9},
{12,5,1,15,14,13,4,10,0,7,6,3,9,2,8,11},
{13,11,7,14,12,1,3,9,5,0,15,4,8,6,2,10},
{6,15,14,9,11,3,0,8,12,2,13,7,1,4,10,5},
{10,2,8,4,7,6,1,5,15,11,9,14,3,12,13,0} };

static void zp_blake2s_init(zp_blake2s *s)
{
    int i;
    for (i=0;i<8;i++) s->h[i]=ZPB_IV[i];
    s->h[0] ^= 0x01010020UL; /* param block: no key, digest_length=32 */
    s->t=0; s->buflen=0;
}

static void zp_blake2s_compress(zp_blake2s *s, const unsigned char *p, int last)
{
    unsigned long v[16], m[16];
    int i, r;
    for (i=0;i<16;i++)
        m[i]=(unsigned long)p[i*4] | ((unsigned long)p[i*4+1]<<8) |
             ((unsigned long)p[i*4+2]<<16) | ((unsigned long)p[i*4+3]<<24);
    for (i=0;i<8;i++) v[i]=s->h[i];
    for (i=0;i<8;i++) v[8+i]=ZPB_IV[i];
    v[12] ^= s->t;
    v[13] ^= 0; /* high 32 bits of the counter - always 0, messages < 4GB */
    v[14] ^= last ? 0xFFFFFFFFUL : 0UL;
    /* v[15] unchanged - no last-node flag, this is never used as a tree leaf */

    for (r=0;r<10;r++) {
        const unsigned char *sg = ZPB_SIGMA[r];
#define ZPB_G(a,b,c,d,x,y) \
        v[a]=ZPB_M32(v[a]+v[b]+(x)); v[d]=ZPB_ROR(v[d]^v[a],16); \
        v[c]=ZPB_M32(v[c]+v[d]);     v[b]=ZPB_ROR(v[b]^v[c],12); \
        v[a]=ZPB_M32(v[a]+v[b]+(y)); v[d]=ZPB_ROR(v[d]^v[a],8);  \
        v[c]=ZPB_M32(v[c]+v[d]);     v[b]=ZPB_ROR(v[b]^v[c],7);
        ZPB_G(0,4,8,12, m[sg[0]], m[sg[1]])
        ZPB_G(1,5,9,13, m[sg[2]], m[sg[3]])
        ZPB_G(2,6,10,14, m[sg[4]], m[sg[5]])
        ZPB_G(3,7,11,15, m[sg[6]], m[sg[7]])
        ZPB_G(0,5,10,15, m[sg[8]], m[sg[9]])
        ZPB_G(1,6,11,12, m[sg[10]], m[sg[11]])
        ZPB_G(2,7,8,13, m[sg[12]], m[sg[13]])
        ZPB_G(3,4,9,14, m[sg[14]], m[sg[15]])
#undef ZPB_G
    }
    for (i=0;i<8;i++) s->h[i] ^= v[i] ^ v[8+i];
}

static void zp_blake2s_update(zp_blake2s *s, const unsigned char *p, unsigned long n)
{
    while (n) {
        unsigned int take = 64 - s->buflen;
        if ((unsigned long)take > n) take = (unsigned int)n;
        memcpy(s->buf + s->buflen, p, take);
        s->buflen += take; p += take; n -= take;
        /* only compress a full buffer if more input follows - the LAST
           64-byte block must go through zp_blake2s_final so it can be
           flagged as the final block, even when it lands exactly on a
           block boundary. */
        if (s->buflen == 64 && n) {
            s->t = ZPB_M32(s->t + 64);
            zp_blake2s_compress(s, s->buf, 0);
            s->buflen = 0;
        }
    }
}

static void zp_blake2s_final(zp_blake2s *s, unsigned char out[32])
{
    int i;
    s->t = ZPB_M32(s->t + s->buflen);
    memset(s->buf + s->buflen, 0, 64 - s->buflen);
    zp_blake2s_compress(s, s->buf, 1);
    for (i=0;i<8;i++) {
        out[i*4]  =(unsigned char)(s->h[i]&0xFF);
        out[i*4+1]=(unsigned char)((s->h[i]>>8)&0xFF);
        out[i*4+2]=(unsigned char)((s->h[i]>>16)&0xFF);
        out[i*4+3]=(unsigned char)((s->h[i]>>24)&0xFF);
    }
}

static void zp_blake2s_buf(const unsigned char *p, unsigned long n, unsigned char out[32])
{
    zp_blake2s s; zp_blake2s_init(&s); zp_blake2s_update(&s,p,n); zp_blake2s_final(&s,out);
}

#endif /* ZP_BLAKE2S_H */
