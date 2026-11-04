/* ftcalc.js -- port of FreeType 2.14.3 `src/base/ftcalc.c`, the FT_PIX_*
 * macros from `include/freetype/internal/ftobjs.h`, and FT_Vector_Transform
 * from `src/base/ftoutln.c`.
 *
 * REFERENCE BUILD (this decides which #ifdef branches are live):
 *   x86-64 Linux / gcc.  Therefore
 *     FT_SIZEOF_LONG == 8                         (config/integer-types.h)
 *       => FT_INT64 is defined (as `long`)        (integer-types.h:194)
 *       => FT_Long / FT_Fixed / FT_Pos are 64-bit signed
 *     FT_CONFIG_OPTION_INLINE_MULFIX is defined   (config/ftoption.h:168)
 *       => FT_MulFix is the inline FT_MulFix_64 from ftcalc.h, whose body is
 *          arithmetically identical to the FT_INT64 FT_MulFix in ftcalc.c.
 *   So the `#ifdef FT_INT64` half of ftcalc.c is what we port; the 32-bit
 *   ft_multo64 / ft_div64by32 half is dead code here and is NOT ported.
 *
 * C INTEGER SEMANTICS THAT NEEDED EXPLICIT EMULATION (see NOTES-W1.md):
 *   - 64-bit intermediate products.  a*b for two 16.16 values reaches 2^62,
 *     far past 2^53 where a JS double stops being exact.  Every such product
 *     is computed either by an exact limb split (fast path) or by BigInt.
 *   - C's truncating division, and `/` on unsigned 64-bit values.
 *   - Arithmetic right shift of a *signed 64-bit* value (`ab >> 63`,
 *     `ab >> 16`), which floors rather than truncates.
 *   - `& ~0xFFFF` / `& ~63` on a signed 64-bit value, which is floor-to-
 *     multiple, not a JS bitwise AND (JS `&` truncates to int32 and would
 *     silently destroy any value >= 2^31).
 */

/* FreeType only ever passes 32-bit quantities to these functions (FT_Fixed
 * scales, 26.6 coordinates, font units).  Inside this range the fast paths
 * below are provably exact; outside it we fall back to BigInt. */
const I32_MIN = -2147483648;
const I32_MAX = 2147483647;

const MAX_SAFE = Number.MAX_SAFE_INTEGER; /* 2^53 - 1 */


/* ---------------------------------------------------------------------- */
/* FT_RoundFix / FT_CeilFix / FT_FloorFix                                  */
/* ---------------------------------------------------------------------- */

/* ftcalc.c:75
 *   return ADD_LONG( a, 0x8000L - ( a < 0 ) ) & ~0xFFFFL;
 *
 * C emulation: `& ~0xFFFFL` on a signed 64-bit value is floor-to-multiple-of-
 * 65536 in two's complement, NOT a JS `&` (which truncates to int32).
 * ADD_LONG is a deliberately-wrapping 64-bit add; the wrap is unreachable for
 * any value within 32768 of LONG_MAX and is not modelled. */
export function FT_RoundFix( a ) {
  const v = a + 0x8000 - ( a < 0 ? 1 : 0 );
  return Math.floor( v / 65536 ) * 65536;
}

/* ftcalc.c:84   return ADD_LONG( a, 0xFFFFL ) & ~0xFFFFL; */
export function FT_CeilFix( a ) {
  return Math.floor( ( a + 0xFFFF ) / 65536 ) * 65536;
}

/* ftcalc.c:93   return a & ~0xFFFFL; */
export function FT_FloorFix( a ) {
  return Math.floor( a / 65536 ) * 65536;
}


/* ---------------------------------------------------------------------- */
/* FT_MulFix                                                               */
/* ---------------------------------------------------------------------- */

/* ftcalc.h:89 (FT_CONFIG_OPTION_INLINE_MULFIX && FT_INT64); arithmetically
 * identical to ftcalc.c:211 (FT_INT64):
 *
 *   FT_Int64  ab = MUL_INT64( a, b );
 *   ab = ADD_INT64( ab, 0x8000 + ( ab >> 63 ) );
 *   return (FT_Long)( ab >> 16 );
 *
 * `ab >> 63` is an arithmetic shift: -1 when ab < 0, else 0.  So the added
 * constant is 0x8000 for ab >= 0 and 0x7FFF for ab < 0, and `>> 16` floors.
 *
 * C emulation: the product reaches 2^62, so `a * b` in JS would round.  We
 * therefore never form it.  With P = |a|*|b| = Q*65536 + R,
 *
 *   ab >= 0:  floor( ( P + 32768) / 65536 ) = Q + (R >= 32768)
 *   ab <  0:  floor( (-P + 32767) / 65536 ) = -( Q + (R >= 32768) )
 *
 * so the result is sign(ab) * ( Q + (R >= 32768) ).  Q and R come from an
 * exact 16-bit limb split of |a|; both limb products stay below 2^47 and are
 * therefore exact doubles. */
export function FT_MulFix( a, b ) {
  if ( a >= I32_MIN && a <= I32_MAX && b >= I32_MIN && b <= I32_MAX ) {
    let s = 1;
    let A = a, B = b;

    if ( A < 0 ) { A = -A; s = -s; }
    if ( B < 0 ) { B = -B; s = -s; }

    const Ah = Math.floor( A / 65536 );   /* < 2^15 */
    const Al = A - Ah * 65536;            /* < 2^16 */

    const t  = Al * B;                    /* < 2^47, exact */
    const th = Math.floor( t / 65536 );
    const R  = t - th * 65536;            /* |a|*|b| mod 65536       */
    const Q  = Ah * B + th;               /* floor(|a|*|b| / 65536)  */

    const m = R >= 32768 ? Q + 1 : Q;

    return s < 0 ? -m : m;
  }

  /* Out-of-range operands: exact 64-bit arithmetic including the mod-2^64
   * wrap that C's FT_Int64 would perform. */
  let ab = BigInt.asIntN( 64, BigInt( a ) * BigInt( b ) );
  ab = BigInt.asIntN( 64, ab + 0x8000n + ( ab >> 63n ) );
  return Number( ab >> 16n );
}


/* ---------------------------------------------------------------------- */
/* FT_MulDiv / FT_MulDiv_No_Round                                          */
/* ---------------------------------------------------------------------- */

/* ftcalc.c:161 (FT_INT64)
 *
 *   FT_MOVE_SIGN( FT_UInt64, a_, a, s );   -- likewise for b_ and c_
 *   d = c > 0 ? ( a * b + ( c >> 1 ) ) / c : 0x7FFFFFFFUL;
 *   return s < 0 ? NEG_LONG( (FT_Long)d ) : (FT_Long)d;
 *
 * Note that a negative *divisor* flips the sign too, that the division runs
 * on unsigned magnitudes (so it floors), and that a zero divisor yields
 * 0x7FFFFFFF rather than trapping.
 *
 * C emulation: a*b again reaches 2^62.  The fast path runs only when the
 * product is provably an exact double -- Number.isSafeInteger(P) is true only
 * if the true product was <= 2^53-1 (a larger true product always rounds to a
 * double > 2^53-1); see NOTES-W1.md. */
export function FT_MulDiv( a_, b_, c_ ) {
  let s = 1;
  let a = a_, b = b_, c = c_;

  if ( a < 0 ) { a = -a; s = -s; }
  if ( b < 0 ) { b = -b; s = -s; }
  if ( c < 0 ) { c = -c; s = -s; }

  let d;

  if ( c > 0 ) {
    const half = Math.floor( c / 2 );     /* c >> 1 on an unsigned value */
    const P    = a * b;

    if ( Number.isSafeInteger( P ) && P <= MAX_SAFE - half )
      d = Math.floor( ( P + half ) / c );
    else
      d = Number( ( BigInt( a ) * BigInt( b ) + BigInt( half ) ) / BigInt( c ) );
  } else {
    d = 0x7FFFFFFF;
  }

  return s < 0 ? -d : d;
}


/* ftcalc.c:186 (FT_INT64) -- as FT_MulDiv but `d = c > 0 ? a * b / c : ...` */
export function FT_MulDiv_No_Round( a_, b_, c_ ) {
  let s = 1;
  let a = a_, b = b_, c = c_;

  if ( a < 0 ) { a = -a; s = -s; }
  if ( b < 0 ) { b = -b; s = -s; }
  if ( c < 0 ) { c = -c; s = -s; }

  let d;

  if ( c > 0 ) {
    const P = a * b;

    if ( Number.isSafeInteger( P ) )
      d = Math.floor( P / c );
    else
      d = Number( ( BigInt( a ) * BigInt( b ) ) / BigInt( c ) );
  } else {
    d = 0x7FFFFFFF;
  }

  return s < 0 ? -d : d;
}


/* ---------------------------------------------------------------------- */
/* FT_DivFix                                                               */
/* ---------------------------------------------------------------------- */

/* ftcalc.c:232 (FT_INT64)
 *
 *   FT_MOVE_SIGN( FT_UInt64, a_, a, s );   -- likewise for b_
 *   q = b > 0 ? ( ( a << 16 ) + ( b >> 1 ) ) / b : 0x7FFFFFFFUL;
 *   return s < 0 ? NEG_LONG( (FT_Long)q ) : (FT_Long)q;
 *
 * C emulation: `a << 16` is an unsigned 64-bit shift that wraps mod 2^64; the
 * fast path runs only while a*65536 + b/2 is an exact double. */
export function FT_DivFix( a_, b_ ) {
  let s = 1;
  let a = a_, b = b_;

  if ( a < 0 ) { a = -a; s = -s; }
  if ( b < 0 ) { b = -b; s = -s; }

  let q;

  if ( b > 0 ) {
    const half = Math.floor( b / 2 );
    const num  = a * 65536;

    if ( Number.isSafeInteger( num ) && num <= MAX_SAFE - half )
      q = Math.floor( ( num + half ) / b );
    else
      q = Number( ( BigInt.asUintN( 64, BigInt( a ) << 16n ) + BigInt( half ) )
                  / BigInt( b ) );
  } else {
    q = 0x7FFFFFFF;
  }

  return s < 0 ? -q : q;
}


/* ---------------------------------------------------------------------- */
/* FT_PIX_* -- include/freetype/internal/ftobjs.h:87..93                   */
/* ---------------------------------------------------------------------- */

/*   #define FT_PIX_FLOOR( x )  ( (x) & ~FT_TYPEOF( x )63 )
 *   #define FT_PIX_ROUND( x )  FT_PIX_FLOOR( (x) + 32 )
 *   #define FT_PIX_CEIL( x )   FT_PIX_FLOOR( (x) + 63 )
 *
 * C emulation: `x` is an FT_Pos (signed 64-bit), so `& ~63` is floor-to-
 * multiple-of-64 in two's complement -- for negative x it rounds *down*
 * (e.g. -1 & ~63 == -64).  A JS `x & ~63` would truncate to int32, so we use
 * Math.floor instead. */
export function FT_PIX_FLOOR( x ) {
  return Math.floor( x / 64 ) * 64;
}

export function FT_PIX_ROUND( x ) {
  return Math.floor( ( x + 32 ) / 64 ) * 64;
}

export function FT_PIX_CEIL( x ) {
  return Math.floor( ( x + 63 ) / 64 ) * 64;
}


/* ---------------------------------------------------------------------- */
/* FT_Vector_Transform -- src/base/ftoutln.c:696                           */
/* ---------------------------------------------------------------------- */

/*   xz = FT_MulFix( vector->x, matrix->xx ) + FT_MulFix( vector->y, matrix->xy );
 *   yz = FT_MulFix( vector->x, matrix->yx ) + FT_MulFix( vector->y, matrix->yy );
 *   vector->x = xz;  vector->y = yz;
 *
 * The two sums use a plain `+` in the C (not ADD_LONG), so signed overflow is
 * formally undefined there; in practice it wraps at 64 bits and the operands
 * are far too small to reach it.  Mutates `v` in place, like the C. */
export function FT_Vector_Transform( v, m ) {
  if ( !v || !m )
    return;

  const xz = FT_MulFix( v.x, m.xx ) + FT_MulFix( v.y, m.xy );
  const yz = FT_MulFix( v.x, m.yx ) + FT_MulFix( v.y, m.yy );

  v.x = xz;
  v.y = yz;
}
