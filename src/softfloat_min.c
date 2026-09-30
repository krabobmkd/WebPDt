/*
 * softfloat_min.c - the few IEEE single-precision helpers libwebp needs.
 *
 * With -msoft-float, GCC turns float operations into calls to libgcc
 * helpers. bebbo's toolchain only ships them in libnix, implemented on
 * top of mathieeesingbas.library whose base is opened by libnix's startup
 * code, which our -nostartfiles library never runs. So here are plain
 * integer implementations of what the decoder references
 * (utils/random_utils.c VP8InitRandom(): compare, multiply, float->uint).
 *
 * Round-to-nearest-even, subnormals handled, NaN/Inf propagated.
 * Must be compiled with -fno-builtin (no float arithmetic is used here).
 */
#include <exec/types.h>

typedef union { float f; ULONG u; } sf_bits;

#define SF_SIGN 0x80000000UL
#define SF_ABS  0x7FFFFFFFUL
#define SF_INF  0x7F800000UL
#define SF_QNAN 0x7FC00000UL

static int sf_isnan(ULONG u)
{
    return (u & SF_ABS) > SF_INF;
}

/* -1 / 0 / 1, operands not NaN */
static LONG sf_cmp(ULONG a, ULONG b)
{
    LONG ka, kb;
    if (((a | b) & SF_ABS) == 0) return 0;          /* +0 == -0 */
    ka = (a & SF_SIGN) ? -(LONG)(a & SF_ABS) : (LONG)a;
    kb = (b & SF_SIGN) ? -(LONG)(b & SF_ABS) : (LONG)b;
    return (ka < kb) ? -1 : (ka > kb) ? 1 : 0;
}

/* a < b  <=>  result < 0 */
LONG __ltsf2(float a, float b)
{
    sf_bits x, y;
    x.f = a; y.f = b;
    if (sf_isnan(x.u) || sf_isnan(y.u)) return 1;
    return sf_cmp(x.u, y.u);
}

/* a > b  <=>  result > 0 */
LONG __gtsf2(float a, float b)
{
    sf_bits x, y;
    x.f = a; y.f = b;
    if (sf_isnan(x.u) || sf_isnan(y.u)) return -1;
    return sf_cmp(x.u, y.u);
}

ULONG __fixunssfsi(float a)
{
    sf_bits x;
    LONG e;
    ULONG m;
    x.f = a;
    if ((x.u & SF_SIGN) || sf_isnan(x.u)) return 0;
    e = (LONG)((x.u >> 23) & 0xFF) - 127;
    if (e < 0) return 0;
    if (e >= 32) return 0xFFFFFFFFUL;
    m = (x.u & 0x7FFFFF) | 0x800000;
    return (e >= 23) ? (m << (e - 23)) : (m >> (23 - e));
}

float __mulsf3(float a, float b)
{
    sf_bits x, y, r;
    ULONG sign, ma, mb, m, round, sticky;
    LONG ea, eb, e, sh;
    unsigned long long p, q;

    x.f = a; y.f = b;
    sign = (x.u ^ y.u) & SF_SIGN;
    ea = (x.u >> 23) & 0xFF;  ma = x.u & 0x7FFFFF;
    eb = (y.u >> 23) & 0xFF;  mb = y.u & 0x7FFFFF;

    /* NaN / Inf */
    if (sf_isnan(x.u)) { r.u = x.u | 0x400000; return r.f; }
    if (sf_isnan(y.u)) { r.u = y.u | 0x400000; return r.f; }
    if (ea == 0xFF || eb == 0xFF)
    {
        if (((ea == 0xFF ? y.u : x.u) & SF_ABS) == 0) r.u = SF_QNAN; /* Inf * 0 */
        else r.u = sign | SF_INF;
        return r.f;
    }

    /* zero / subnormal inputs */
    if (ea == 0)
    {
        if (!ma) { r.u = sign; return r.f; }
        ea = 1;
        while (!(ma & 0x800000)) { ma <<= 1; ea--; }
    }
    else ma |= 0x800000;
    if (eb == 0)
    {
        if (!mb) { r.u = sign; return r.f; }
        eb = 1;
        while (!(mb & 0x800000)) { mb <<= 1; eb--; }
    }
    else mb |= 0x800000;

    /* 24x24 -> 48 bit product in [2^46, 2^48) */
    p = (unsigned long long)ma * mb;
    e = ea + eb - 127;
    if (p & (1ULL << 47)) e++;
    else p <<= 1;
    /* now p in [2^47, 2^48): 24 mantissa bits + 24 rounding bits */

    if (e >= 0xFF) { r.u = sign | SF_INF; return r.f; }

    sh = 0;
    if (e <= 0)
    {
        sh = 1 - e;              /* subnormal result */
        e = 0;
        if (sh > 30) { r.u = sign; return r.f; }
    }
    q = p >> sh;
    sticky = (p & ((1ULL << sh) - 1)) != 0;
    m = (ULONG)(q >> 24);
    round = (ULONG)(q >> 23) & 1;
    sticky |= (q & 0x7FFFFF) != 0;

    if (round && (sticky || (m & 1)))
    {
        m++;
        if (m == 0x1000000) { m >>= 1; e++; }
        else if (e == 0 && m == 0x800000) e = 1;   /* subnormal -> normal */
        if (e >= 0xFF) { r.u = sign | SF_INF; return r.f; }
    }

    r.u = sign | ((ULONG)e << 23) | (m & 0x7FFFFF);
    return r.f;
}
