/*
 * zpsha256.h - Minimal SHA-256 (FIPS 180-4), strict C89, header-only.
 *
 * Used by zplink to hash the ROM payload before RSA signing.  Uses unsigned
 * long masked to 32 bits so it is portable to any C89 host (and to the DOS
 * toolchain).
 */
#ifndef ZP_SHA256_H
#define ZP_SHA256_H

#include <string.h>

typedef struct {
    unsigned long h[8];
    unsigned long len;      /* message length in bytes (low 32 bits) */
    unsigned char buf[64];
    unsigned int  buflen;
} zp_sha256;

#define ZP_M32(x) ((x) & 0xFFFFFFFFUL)
#define ZP_ROR(x,n) ZP_M32((ZP_M32(x) >> (n)) | ZP_M32((x) << (32 - (n))))

static const unsigned long ZP_SHA_K[64] = {
0x428a2f98UL,0x71374491UL,0xb5c0fbcfUL,0xe9b5dba5UL,0x3956c25bUL,0x59f111f1UL,
0x923f82a4UL,0xab1c5ed5UL,0xd807aa98UL,0x12835b01UL,0x243185beUL,0x550c7dc3UL,
0x72be5d74UL,0x80deb1feUL,0x9bdc06a7UL,0xc19bf174UL,0xe49b69c1UL,0xefbe4786UL,
0x0fc19dc6UL,0x240ca1ccUL,0x2de92c6fUL,0x4a7484aaUL,0x5cb0a9dcUL,0x76f988daUL,
0x983e5152UL,0xa831c66dUL,0xb00327c8UL,0xbf597fc7UL,0xc6e00bf3UL,0xd5a79147UL,
0x06ca6351UL,0x14292967UL,0x27b70a85UL,0x2e1b2138UL,0x4d2c6dfcUL,0x53380d13UL,
0x650a7354UL,0x766a0abbUL,0x81c2c92eUL,0x92722c85UL,0xa2bfe8a1UL,0xa81a664bUL,
0xc24b8b70UL,0xc76c51a3UL,0xd192e819UL,0xd6990624UL,0xf40e3585UL,0x106aa070UL,
0x19a4c116UL,0x1e376c08UL,0x2748774cUL,0x34b0bcb5UL,0x391c0cb3UL,0x4ed8aa4aUL,
0x5b9cca4fUL,0x682e6ff3UL,0x748f82eeUL,0x78a5636fUL,0x84c87814UL,0x8cc70208UL,
0x90befffaUL,0xa4506cebUL,0xbef9a3f7UL,0xc67178f2UL };

static void zp_sha256_init(zp_sha256 *s)
{
    s->h[0]=0x6a09e667UL; s->h[1]=0xbb67ae85UL; s->h[2]=0x3c6ef372UL;
    s->h[3]=0xa54ff53aUL; s->h[4]=0x510e527fUL; s->h[5]=0x9b05688cUL;
    s->h[6]=0x1f83d9abUL; s->h[7]=0x5be0cd19UL;
    s->len=0; s->buflen=0;
}

static void zp_sha256_block(zp_sha256 *s, const unsigned char *p)
{
    unsigned long w[64], a,b,c,d,e,f,g,h,t1,t2;
    int i;
    for (i=0;i<16;i++)
        w[i]=((unsigned long)p[i*4]<<24)|((unsigned long)p[i*4+1]<<16)|
             ((unsigned long)p[i*4+2]<<8)|((unsigned long)p[i*4+3]);
    for (i=16;i<64;i++) {
        unsigned long s0=ZP_ROR(w[i-15],7)^ZP_ROR(w[i-15],18)^(ZP_M32(w[i-15])>>3);
        unsigned long s1=ZP_ROR(w[i-2],17)^ZP_ROR(w[i-2],19)^(ZP_M32(w[i-2])>>10);
        w[i]=ZP_M32(w[i-16]+s0+w[i-7]+s1);
    }
    a=s->h[0];b=s->h[1];c=s->h[2];d=s->h[3];
    e=s->h[4];f=s->h[5];g=s->h[6];h=s->h[7];
    for (i=0;i<64;i++) {
        unsigned long S1=ZP_ROR(e,6)^ZP_ROR(e,11)^ZP_ROR(e,25);
        unsigned long ch=(e&f)^((~e)&g);
        unsigned long S0=ZP_ROR(a,2)^ZP_ROR(a,13)^ZP_ROR(a,22);
        unsigned long maj=(a&b)^(a&c)^(b&c);
        t1=ZP_M32(h+S1+ch+ZP_SHA_K[i]+w[i]);
        t2=ZP_M32(S0+maj);
        h=g;g=f;f=e;e=ZP_M32(d+t1);d=c;c=b;b=a;a=ZP_M32(t1+t2);
    }
    s->h[0]=ZP_M32(s->h[0]+a); s->h[1]=ZP_M32(s->h[1]+b);
    s->h[2]=ZP_M32(s->h[2]+c); s->h[3]=ZP_M32(s->h[3]+d);
    s->h[4]=ZP_M32(s->h[4]+e); s->h[5]=ZP_M32(s->h[5]+f);
    s->h[6]=ZP_M32(s->h[6]+g); s->h[7]=ZP_M32(s->h[7]+h);
}

static void zp_sha256_update(zp_sha256 *s, const unsigned char *p, unsigned long n)
{
    s->len += n;
    while (n) {
        unsigned int take = 64 - s->buflen;
        if ((unsigned long)take > n) take = (unsigned int)n;
        memcpy(s->buf + s->buflen, p, take);
        s->buflen += take; p += take; n -= take;
        if (s->buflen == 64) { zp_sha256_block(s, s->buf); s->buflen = 0; }
    }
}

static void zp_sha256_final(zp_sha256 *s, unsigned char out[32])
{
    unsigned long bits = s->len * 8UL;
    unsigned char pad = 0x80;
    int i;
    zp_sha256_update(s, &pad, 1);
    pad = 0x00;
    while (s->buflen != 56) zp_sha256_update(s, &pad, 1);
    for (i=7;i>=0;i--) { unsigned char b=(unsigned char)((bits>>(i*8))&0xFF); zp_sha256_update(s,&b,1); }
    for (i=0;i<8;i++) {
        out[i*4]  =(unsigned char)((s->h[i]>>24)&0xFF);
        out[i*4+1]=(unsigned char)((s->h[i]>>16)&0xFF);
        out[i*4+2]=(unsigned char)((s->h[i]>>8)&0xFF);
        out[i*4+3]=(unsigned char)(s->h[i]&0xFF);
    }
}

static void zp_sha256_buf(const unsigned char *p, unsigned long n, unsigned char out[32])
{
    zp_sha256 s; zp_sha256_init(&s); zp_sha256_update(&s,p,n); zp_sha256_final(&s,out);
}

#endif /* ZP_SHA256_H */
