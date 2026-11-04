/* md5.js - from-scratch MD5, a transliteration of ../ftdemos/src/md5.c
 * (Alexander Peslyak's public-domain, OpenSSL-compatible MD5).
 *
 * ftlint's Checksum() runs MD5_Init / MD5_Update(buffer, rows*pitch) /
 * MD5_Final and prints the 16 result bytes with "%02X" -- UPPERCASE hex.
 *
 * C-semantics hazards handled here:
 *  - MD5_u32plus is `unsigned int' (32 bit).  Every `+' and `<<' in the C
 *    STEP macro therefore wraps mod 2^32; JS numbers do not, so each
 *    intermediate is forced back with `>>> 0'.
 *  - JS bitwise operators yield SIGNED 32-bit ints; `>>> 0' restores the
 *    unsigned value before it is used as a number.
 *  - ctx->lo is masked with 0x1fffffff and ctx->hi counts the overflow, so
 *    the trailing 64-bit length field is (lo<<3, hi).  We keep the same two
 *    word representation rather than using BigInt.
 *  - `ctx->hi += size >> 29' is an unsigned long shift: use floor division.
 */

/* The basic MD5 functions (the same optimized forms as md5.c). */
function F( x, y, z )  { return ( z ^ ( x & ( y ^ z ) ) ) >>> 0; }
function G( x, y, z )  { return ( y ^ ( z & ( x ^ y ) ) ) >>> 0; }
function H( x, y, z )  { return ( ( x ^ y ) ^ z ) >>> 0; }
function H2( x, y, z ) { return ( x ^ ( y ^ z ) ) >>> 0; }
function I( x, y, z )  { return ( y ^ ( x | ~z ) ) >>> 0; }

/* STEP(f, a, b, c, d, x, t, s), as a function.  All arithmetic mod 2^32. */
function STEP( f, a, b, c, d, x, t, s ) {
  a = ( a + f( b, c, d ) + x + t ) >>> 0;
  /* C: (a << s) | ((a & 0xffffffff) >> (32 - s))  -- a 32-bit rotate left. */
  a = ( ( a << s ) | ( a >>> ( 32 - s ) ) ) >>> 0;
  a = ( a + b ) >>> 0;
  return a;
}

/* OUT(dst, src): store src little-endian into dst[at..at+3]. */
function OUT( dst, at, src ) {
  dst[at]     =   src         & 0xff;
  dst[at + 1] = ( src >>> 8  ) & 0xff;
  dst[at + 2] = ( src >>> 16 ) & 0xff;
  dst[at + 3] = ( src >>> 24 ) & 0xff;
}

export class MD5 {
  constructor() {
    this.a  = 0x67452301;
    this.b  = 0xefcdab89;
    this.c  = 0x98badcfe;
    this.d  = 0x10325476;
    this.lo = 0;
    this.hi = 0;
    this.buffer = new Uint8Array( 64 );
    this.block  = new Uint32Array( 16 );
  }

  /* static const void *body(MD5_CTX *ctx, const void *data, unsigned long size) */
  _body( data, start, size ) {
    let ptr = start;
    let a = this.a, b = this.b, c = this.c, d = this.d;
    const k = this.block;

    do {
      const sa = a, sb = b, sc = c, sd = d;

      /* SET(n): read 4 bytes little-endian. */
      for ( let n = 0; n < 16; n++ ) {
        const o = ptr + n * 4;
        k[n] = ( data[o] | ( data[o + 1] << 8 ) |
                 ( data[o + 2] << 16 ) | ( data[o + 3] << 24 ) ) >>> 0;
      }

      /* Round 1 */
      a = STEP( F, a, b, c, d, k[ 0], 0xd76aa478,  7 );
      d = STEP( F, d, a, b, c, k[ 1], 0xe8c7b756, 12 );
      c = STEP( F, c, d, a, b, k[ 2], 0x242070db, 17 );
      b = STEP( F, b, c, d, a, k[ 3], 0xc1bdceee, 22 );
      a = STEP( F, a, b, c, d, k[ 4], 0xf57c0faf,  7 );
      d = STEP( F, d, a, b, c, k[ 5], 0x4787c62a, 12 );
      c = STEP( F, c, d, a, b, k[ 6], 0xa8304613, 17 );
      b = STEP( F, b, c, d, a, k[ 7], 0xfd469501, 22 );
      a = STEP( F, a, b, c, d, k[ 8], 0x698098d8,  7 );
      d = STEP( F, d, a, b, c, k[ 9], 0x8b44f7af, 12 );
      c = STEP( F, c, d, a, b, k[10], 0xffff5bb1, 17 );
      b = STEP( F, b, c, d, a, k[11], 0x895cd7be, 22 );
      a = STEP( F, a, b, c, d, k[12], 0x6b901122,  7 );
      d = STEP( F, d, a, b, c, k[13], 0xfd987193, 12 );
      c = STEP( F, c, d, a, b, k[14], 0xa679438e, 17 );
      b = STEP( F, b, c, d, a, k[15], 0x49b40821, 22 );

      /* Round 2 */
      a = STEP( G, a, b, c, d, k[ 1], 0xf61e2562,  5 );
      d = STEP( G, d, a, b, c, k[ 6], 0xc040b340,  9 );
      c = STEP( G, c, d, a, b, k[11], 0x265e5a51, 14 );
      b = STEP( G, b, c, d, a, k[ 0], 0xe9b6c7aa, 20 );
      a = STEP( G, a, b, c, d, k[ 5], 0xd62f105d,  5 );
      d = STEP( G, d, a, b, c, k[10], 0x02441453,  9 );
      c = STEP( G, c, d, a, b, k[15], 0xd8a1e681, 14 );
      b = STEP( G, b, c, d, a, k[ 4], 0xe7d3fbc8, 20 );
      a = STEP( G, a, b, c, d, k[ 9], 0x21e1cde6,  5 );
      d = STEP( G, d, a, b, c, k[14], 0xc33707d6,  9 );
      c = STEP( G, c, d, a, b, k[ 3], 0xf4d50d87, 14 );
      b = STEP( G, b, c, d, a, k[ 8], 0x455a14ed, 20 );
      a = STEP( G, a, b, c, d, k[13], 0xa9e3e905,  5 );
      d = STEP( G, d, a, b, c, k[ 2], 0xfcefa3f8,  9 );
      c = STEP( G, c, d, a, b, k[ 7], 0x676f02d9, 14 );
      b = STEP( G, b, c, d, a, k[12], 0x8d2a4c8a, 20 );

      /* Round 3 */
      a = STEP( H,  a, b, c, d, k[ 5], 0xfffa3942,  4 );
      d = STEP( H2, d, a, b, c, k[ 8], 0x8771f681, 11 );
      c = STEP( H,  c, d, a, b, k[11], 0x6d9d6122, 16 );
      b = STEP( H2, b, c, d, a, k[14], 0xfde5380c, 23 );
      a = STEP( H,  a, b, c, d, k[ 1], 0xa4beea44,  4 );
      d = STEP( H2, d, a, b, c, k[ 4], 0x4bdecfa9, 11 );
      c = STEP( H,  c, d, a, b, k[ 7], 0xf6bb4b60, 16 );
      b = STEP( H2, b, c, d, a, k[10], 0xbebfbc70, 23 );
      a = STEP( H,  a, b, c, d, k[13], 0x289b7ec6,  4 );
      d = STEP( H2, d, a, b, c, k[ 0], 0xeaa127fa, 11 );
      c = STEP( H,  c, d, a, b, k[ 3], 0xd4ef3085, 16 );
      b = STEP( H2, b, c, d, a, k[ 6], 0x04881d05, 23 );
      a = STEP( H,  a, b, c, d, k[ 9], 0xd9d4d039,  4 );
      d = STEP( H2, d, a, b, c, k[12], 0xe6db99e5, 11 );
      c = STEP( H,  c, d, a, b, k[15], 0x1fa27cf8, 16 );
      b = STEP( H2, b, c, d, a, k[ 2], 0xc4ac5665, 23 );

      /* Round 4 */
      a = STEP( I, a, b, c, d, k[ 0], 0xf4292244,  6 );
      d = STEP( I, d, a, b, c, k[ 7], 0x432aff97, 10 );
      c = STEP( I, c, d, a, b, k[14], 0xab9423a7, 15 );
      b = STEP( I, b, c, d, a, k[ 5], 0xfc93a039, 21 );
      a = STEP( I, a, b, c, d, k[12], 0x655b59c3,  6 );
      d = STEP( I, d, a, b, c, k[ 3], 0x8f0ccc92, 10 );
      c = STEP( I, c, d, a, b, k[10], 0xffeff47d, 15 );
      b = STEP( I, b, c, d, a, k[ 1], 0x85845dd1, 21 );
      a = STEP( I, a, b, c, d, k[ 8], 0x6fa87e4f,  6 );
      d = STEP( I, d, a, b, c, k[15], 0xfe2ce6e0, 10 );
      c = STEP( I, c, d, a, b, k[ 6], 0xa3014314, 15 );
      b = STEP( I, b, c, d, a, k[13], 0x4e0811a1, 21 );
      a = STEP( I, a, b, c, d, k[ 4], 0xf7537e82,  6 );
      d = STEP( I, d, a, b, c, k[11], 0xbd3af235, 10 );
      c = STEP( I, c, d, a, b, k[ 2], 0x2ad7d2bb, 15 );
      b = STEP( I, b, c, d, a, k[ 9], 0xeb86d391, 21 );

      a = ( a + sa ) >>> 0;
      b = ( b + sb ) >>> 0;
      c = ( c + sc ) >>> 0;
      d = ( d + sd ) >>> 0;

      ptr += 64;
    } while ( ( size -= 64 ) );   /* C: while (size -= 64) */

    this.a = a; this.b = b; this.c = c; this.d = d;
    return ptr;
  }

  /* void MD5_Update(MD5_CTX *ctx, const void *data, unsigned long size) */
  update( data, start = 0, size = data.length - start ) {
    const saved_lo = this.lo;

    /* C: if ((ctx->lo = (saved_lo + size) & 0x1fffffff) < saved_lo) ctx->hi++; */
    this.lo = ( saved_lo + size ) & 0x1fffffff;
    if ( this.lo < saved_lo )
      this.hi = ( this.hi + 1 ) >>> 0;
    /* C: ctx->hi += size >> 29  (unsigned long shift -> floor division) */
    this.hi = ( this.hi + Math.floor( size / 0x20000000 ) ) >>> 0;

    let used = saved_lo & 0x3f;
    let off  = start;

    if ( used ) {
      const available = 64 - used;

      if ( size < available ) {
        this.buffer.set( data.subarray( off, off + size ), used );
        return;
      }

      this.buffer.set( data.subarray( off, off + available ), used );
      off  += available;
      size -= available;
      this._body( this.buffer, 0, 64 );
    }

    if ( size >= 64 ) {
      off   = this._body( data, off, size & ~0x3f );
      size &= 0x3f;
    }

    this.buffer.set( data.subarray( off, off + size ), 0 );
  }

  /* void MD5_Final(unsigned char *result, MD5_CTX *ctx) */
  final() {
    let used = this.lo & 0x3f;

    this.buffer[used++] = 0x80;

    let available = 64 - used;

    if ( available < 8 ) {
      this.buffer.fill( 0, used, used + available );
      this._body( this.buffer, 0, 64 );
      used = 0;
      available = 64;
    }

    this.buffer.fill( 0, used, used + available - 8 );

    /* C: ctx->lo <<= 3; MD5_u32plus is 32-bit so this wraps mod 2^32. */
    const lo = ( this.lo << 3 ) >>> 0;

    OUT( this.buffer, 56, lo );
    OUT( this.buffer, 60, this.hi );

    this._body( this.buffer, 0, 64 );

    const out = new Uint8Array( 16 );
    OUT( out,  0, this.a );
    OUT( out,  4, this.b );
    OUT( out,  8, this.c );
    OUT( out, 12, this.d );
    return out;
  }
}

const HEX = '0123456789ABCDEF';

/** md5Hex(u8) -> 32 UPPERCASE hex chars, matching ftlint's "%02X" loop. */
export function md5Hex( u8 ) {
  const ctx = new MD5();
  if ( u8 && u8.length )
    ctx.update( u8, 0, u8.length );
  const md = ctx.final();
  let s = '';
  for ( let i = 0; i < 16; i++ )
    s += HEX[md[i] >> 4] + HEX[md[i] & 15];
  return s;
}

export default md5Hex;
