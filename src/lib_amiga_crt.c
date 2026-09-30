/*
 * lib_amiga_crt.c - minimal reentrant C runtime for webp.datatype
 *
 * The datatype is linked -nostdlib: libnix's startup never runs, and its
 * global state (malloc pools, stdio) would be shared by every task using
 * the library anyway. This file provides exactly what the libwebp decoder
 * needs, using only reentrant exec calls:
 *
 *   malloc/calloc/realloc/free -> AllocVec/FreeVec (exec tracks the size)
 *   memcpy/memmove/memset/memcmp/strlen
 *   abort
 *
 * MUST be compiled with -fno-builtin (set in CMakeLists.txt), else GCC
 * turns these loops into calls to the very functions being defined.
 * 64-bit arithmetic helpers (__udivdi3...) come from libgcc (libm020).
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <stddef.h>

/*------------------------------------------------------------------------*
 * Memory                                                                  *
 *------------------------------------------------------------------------*/

void *malloc(size_t size)
{
    if (!size) return NULL;
    return AllocVec(size, MEMF_ANY);
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total;
    if (!nmemb || !size) return NULL;
    total = nmemb * size;
    if (total / nmemb != size) return NULL;
    return AllocVec(total, MEMF_ANY | MEMF_CLEAR);
}

void free(void *ptr)
{
    if (ptr) FreeVec(ptr);
}

void *memcpy(void *dst, const void *src, size_t n);

void *realloc(void *ptr, size_t size)
{
    void *np;
    ULONG oldsize;
    if (!ptr) return malloc(size);
    if (!size) { free(ptr); return NULL; }
    /* AllocVec stores the allocation size (incl. its 4 byte header)
     * in the longword just before the returned pointer. */
    oldsize = ((ULONG *)ptr)[-1] - 4;
    np = AllocVec(size, MEMF_ANY);
    if (!np) return NULL;
    memcpy(np, ptr, oldsize < size ? oldsize : size);
    FreeVec(ptr);
    return np;
}

/*------------------------------------------------------------------------*
 * Memory operations (long-word fast path when aligned)                    *
 *------------------------------------------------------------------------*/

void *memcpy(void *dst, const void *src, size_t n)
{
    UBYTE *d = (UBYTE *)dst;
    const UBYTE *s = (const UBYTE *)src;

    if (n >= 16 && (((ULONG)d ^ (ULONG)s) & 1) == 0)
    {
        ULONG *dl;
        const ULONG *sl;
        if ((ULONG)d & 1) { *d++ = *s++; n--; }
        if ((ULONG)d & 2) { *(UWORD *)d = *(const UWORD *)s; d += 2; s += 2; n -= 2; }
        dl = (ULONG *)d;
        sl = (const ULONG *)s;
        while (n >= 16)
        {
            dl[0] = sl[0]; dl[1] = sl[1]; dl[2] = sl[2]; dl[3] = sl[3];
            dl += 4; sl += 4; n -= 16;
        }
        while (n >= 4) { *dl++ = *sl++; n -= 4; }
        d = (UBYTE *)dl;
        s = (const UBYTE *)sl;
    }
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    UBYTE *d = (UBYTE *)dst;
    const UBYTE *s = (const UBYTE *)src;
    if (d == s || !n) return dst;
    if (d < s || d >= s + n) return memcpy(dst, src, n);
    d += n; s += n;
    while (n--) *--d = *--s;
    return dst;
}

void *memset(void *s, int c, size_t n)
{
    UBYTE *p = (UBYTE *)s;
    UBYTE b = (UBYTE)c;

    if (n >= 16)
    {
        ULONG v = b;
        ULONG *pl;
        v |= v << 8;
        v |= v << 16;
        while ((ULONG)p & 3) { *p++ = b; n--; }
        pl = (ULONG *)p;
        while (n >= 16) { pl[0] = v; pl[1] = v; pl[2] = v; pl[3] = v; pl += 4; n -= 16; }
        while (n >= 4) { *pl++ = v; n -= 4; }
        p = (UBYTE *)pl;
    }
    while (n--) *p++ = b;
    return s;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const UBYTE *pa = (const UBYTE *)a;
    const UBYTE *pb = (const UBYTE *)b;
    while (n--)
    {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

/* __bcopz: bebbo gcc block-copy helper (bcopy argument order) */
void __bcopz(const void *src, void *dst, size_t n)
{
    memmove(dst, src, n);
}

/*------------------------------------------------------------------------*
 * Stdlib                                                                  *
 *------------------------------------------------------------------------*/

/* used by libwebp utils/palette.c */
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *))
{
    char *base2 = (char *)base;
    size_t i, a, b, c;
    while (nmemb > 1) {
        a = 0; b = nmemb - 1; c = (a + b) / 2;
        for (;;) {
            while (compar(&base2[size*c], &base2[size*a]) > 0) a++;
            while (compar(&base2[size*c], &base2[size*b]) < 0) b--;
            if (a >= b) break;
            for (i = 0; i < size; i++) {
                char tmp = base2[size*a+i];
                base2[size*a+i] = base2[size*b+i];
                base2[size*b+i] = tmp;
            }
            if (c == a) c = b; else if (c == b) c = a;
            a++; b--;
        }
        b++;
        if (b < nmemb - b) {
            qsort(base2, b, size, compar);
            base2 += size * b; nmemb -= b;
        } else {
            qsort(base2 + size * b, nmemb - b, size, compar);
            nmemb = b;
        }
    }
}

void abort(void)
{
    Alert(0x05000001UL);
    for (;;);
}
