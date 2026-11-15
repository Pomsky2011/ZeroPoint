/*
 * zprsa_verify.h - RSA-2048 verify half of ZPdevtools/src/zprsa.h (vendored; no signing).
 *
 * Big integers are 128 little-endian 16-bit limbs (ZP_RSA_LIMBS).  Modular
 * exponentiation uses Montgomery multiplication (CIOS) with the precomputed
 * constants in zpkey.h, so no big-integer division is needed at runtime and
 * only 32-bit (unsigned long) intermediates are required - portable to any
 * C89 host and to the 16-bit DOS toolchain.
 *
 * Signature scheme: PKCS#1 v1.5 over SHA-256 (EMSA-PKCS1-v1_5, 256-byte EM).
 */
#ifndef ZP_RSA_H
#define ZP_RSA_H

#include "zpkey_pub.h"

#define ZP_L ZP_RSA_LIMBS   /* 128 */

typedef unsigned short zp_limb;

/* out = a*b mod n in Montgomery domain (a,b already Montgomery). CIOS. */
static void zp_montmul(zp_limb *out, const zp_limb *a, const zp_limb *b)
{
    unsigned long t[ZP_L + 2];
    int i, j;
    for (i = 0; i < ZP_L + 2; i++) t[i] = 0;
    for (i = 0; i < ZP_L; i++) {
        unsigned long C = 0, s, m;
        for (j = 0; j < ZP_L; j++) {
            s = t[j] + (unsigned long)a[j] * b[i] + C;
            t[j] = s & 0xFFFFUL;
            C = s >> 16;
        }
        s = t[ZP_L] + C; t[ZP_L] = s & 0xFFFFUL; t[ZP_L+1] += s >> 16;

        m = (t[0] * ZP_RSA_N0INV) & 0xFFFFUL;
        s = t[0] + m * ZP_RSA_N[0]; C = s >> 16;
        for (j = 1; j < ZP_L; j++) {
            s = t[j] + m * ZP_RSA_N[j] + C;
            t[j-1] = s & 0xFFFFUL;
            C = s >> 16;
        }
        s = t[ZP_L] + C; t[ZP_L-1] = s & 0xFFFFUL; C = s >> 16;
        t[ZP_L] = t[ZP_L+1] + C; t[ZP_L+1] = 0;
    }
    /* Conditional final subtraction: if t >= n, t -= n. */
    {
        int ge = 0;
        if (t[ZP_L]) ge = 1;
        else {
            for (i = ZP_L - 1; i >= 0; i--) {
                if (t[i] != ZP_RSA_N[i]) { ge = (t[i] > ZP_RSA_N[i]); break; }
            }
            if (i < 0) ge = 1; /* equal -> subtract */
        }
        if (ge) {
            long borrow = 0;
            for (i = 0; i < ZP_L; i++) {
                long v = (long)t[i] - (long)ZP_RSA_N[i] - borrow;
                if (v < 0) { v += 0x10000L; borrow = 1; } else borrow = 0;
                out[i] = (zp_limb)v;
            }
        } else {
            for (i = 0; i < ZP_L; i++) out[i] = (zp_limb)t[i];
        }
    }
}

/* out = base^exp mod n.  exp is a limb array of length explimbs (MSB-first). */
static void zp_modexp(zp_limb *out, const zp_limb *base,
                      const zp_limb *exp, int explimbs)
{
    zp_limb a[ZP_L], r[ZP_L], one[ZP_L], tmp[ZP_L];
    int i, bit, started = 0, k;
    for (k = 0; k < ZP_L; k++) one[k] = 0;
    one[0] = 1;
    zp_montmul(a, base, ZP_RSA_RR);      /* a = base * R mod n */
    for (k = 0; k < ZP_L; k++) r[k] = 0; /* r initialized on first 1 bit */
    for (i = explimbs - 1; i >= 0; i--) {
        for (bit = 15; bit >= 0; bit--) {
            if (started) {
                zp_montmul(tmp, r, r);           /* square */
                for (k = 0; k < ZP_L; k++) r[k] = tmp[k];
            }
            if ((exp[i] >> bit) & 1) {
                if (!started) {                  /* leading 1: r = a */
                    for (k = 0; k < ZP_L; k++) r[k] = a[k];
                    started = 1;
                } else {                         /* multiply */
                    zp_montmul(tmp, r, a);
                    for (k = 0; k < ZP_L; k++) r[k] = tmp[k];
                }
            }
        }
    }
    if (!started) {                              /* exp == 0 -> result 1 */
        for (k = 0; k < ZP_L; k++) out[k] = one[k];
        return;
    }
    zp_montmul(out, r, one);                      /* leave Montgomery domain */
}

/* Convert 256-byte big-endian EM into little-endian 16-bit limbs. */
static void zp_be_to_limbs(const unsigned char em[ZP_RSA_BYTES], zp_limb *x)
{
    int j;
    for (j = 0; j < ZP_L; j++) {
        int hi = ZP_RSA_BYTES - 1 - (2*j) - 1; /* high byte of limb j */
        int lo = ZP_RSA_BYTES - 1 - (2*j);     /* low byte of limb j  */
        x[j] = (zp_limb)(em[lo] | (em[hi] << 8));
    }
}

static void zp_limbs_to_be(const zp_limb *x, unsigned char em[ZP_RSA_BYTES])
{
    int j;
    for (j = 0; j < ZP_L; j++) {
        int hi = ZP_RSA_BYTES - 1 - (2*j) - 1;
        int lo = ZP_RSA_BYTES - 1 - (2*j);
        em[lo] = (unsigned char)(x[j] & 0xFF);
        em[hi] = (unsigned char)((x[j] >> 8) & 0xFF);
    }
}

/* Build EMSA-PKCS1-v1_5 encoding (256 bytes) of a SHA-256 digest. */
static void zp_pkcs1_sha256(const unsigned char hash[32], unsigned char em[ZP_RSA_BYTES])
{
    static const unsigned char der[19] = {
        0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,
        0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20 };
    int tlen = 19 + 32, pslen = ZP_RSA_BYTES - 3 - tlen, i, p;
    em[0] = 0x00; em[1] = 0x01;
    for (i = 0; i < pslen; i++) em[2+i] = 0xFF;
    p = 2 + pslen; em[p++] = 0x00;
    for (i = 0; i < 19; i++) em[p++] = der[i];
    for (i = 0; i < 32; i++) em[p++] = hash[i];
}

/* Verify: recompute EM = sig^e mod n and compare to PKCS1(hash). 1 = valid. */
static int zp_rsa_verify(const unsigned char hash[32], const unsigned char sig[ZP_RSA_BYTES])
{
    unsigned char em[ZP_RSA_BYTES], em2[ZP_RSA_BYTES];
    zp_limb s[ZP_L], m[ZP_L], e[ZP_L];
    int i;
    zp_pkcs1_sha256(hash, em);
    zp_be_to_limbs(sig, s);
    for (i = 0; i < ZP_L; i++) e[i] = 0;
    e[0] = (zp_limb)(ZP_RSA_E & 0xFFFF);
    e[1] = (zp_limb)((ZP_RSA_E >> 16) & 0xFFFF);
    zp_modexp(m, s, e, ZP_L);
    zp_limbs_to_be(m, em2);
    for (i = 0; i < ZP_RSA_BYTES; i++) if (em[i] != em2[i]) return 0;
    return 1;
}

#endif /* ZP_RSA_H */
