/****************************************************************************
 *
 * cff.js  --  W3
 *
 *   Pure-JavaScript port of FreeType 2.14.3's CFF (Type 2) glyph loading
 *   path, i.e. of
 *
 *     src/cff/cffload.c    INDEX / DICT / charset / encoding / FDSelect / FDArray
 *     src/cff/cffparse.c   the DICT token parser (numbers, reals, FontMatrix)
 *     src/cff/cffobjs.c    the font-matrix / units_per_EM normalisation
 *     src/cff/cffgload.c   cff_slot_load (the glyph loading entry point)
 *     src/psaux/psintrp.c  cf2_interpT2CharString -- the charstring interpreter
 *                          FreeType actually uses (Adobe engine, the default)
 *     src/psaux/psft.c     the FreeType<->Adobe glue and the outline callbacks
 *     src/psaux/psobjs.c   PS_Builder (how points and contours are emitted)
 *     src/psaux/pshints.c  CF2_GlyphPath (the path queue), unhinted subset
 *
 *   Exported entry point (see spike/CONTRACT.md):
 *
 *     loadCffOutline( face, gid ) -> { outline, advance, ... }
 *
 *   `outline' is in FONT UNITS with cubic control points tagged third-order.
 *
 *   ------------------------------------------------------------------
 *   WHERE DOES FREETYPE SCALE A CFF OUTLINE?
 *   ------------------------------------------------------------------
 *   Both, in a way that matters -- see NOTES-W3.md for the long version.
 *
 *   The Adobe interpreter always runs *scaled*: psft.c's
 *   `cf2_getScaleAndHintFlag' hands it a transform, and for an UNHINTED load
 *   (which is what `ftlint -f 2' asks for, FT_LOAD_NO_HINTING) that transform
 *   is the hardcoded
 *
 *       transform.a = transform.d = 0x0400      <- 1/64 in 16.16
 *
 *   with the comment "for unhinted outlines, `cff_slot_load' does the
 *   scaling, thus render at `unity' scale".  Every emitted point therefore
 *   goes through
 *
 *       device = FT_MulFix( charspace_16_16, 0x400 )        (pshints.c)
 *       point  = device >> 10                               (psobjs.c)
 *
 *   which is the identity for integral font units but is NOT a no-op for
 *   fractional ones (255-prefixed operands, div, mul, ...).  So FreeType
 *   produces the outline in font units *with a specific rounding applied*,
 *   and `cff_slot_load' afterwards does the real scaling with
 *   FT_MulFix( vec, x_scale ).  We reproduce the rounding here and leave the
 *   FT_MulFix( vec, x_scale ) step to W2's ftscale.js, exactly like FreeType.
 *
 *   Fidelity to the C source beats elegance (CONTRACT.md).  JavaScript has no
 *   integers; every site that relies on 32-bit wrap, on a 64-bit intermediate
 *   or on C's truncating division carries a comment saying so.
 *
 ****************************************************************************/


/* ------------------------------------------------------------------ */
/* S0  ftcalc bridge                                                   */
/* ------------------------------------------------------------------ */

/* W1 owns ftcalc.js.  Use it when it exists so that we cannot drift from
 * the shared arithmetic; fall back to the local ports below (which are
 * straight transcriptions of the same C) when W1 has not landed yet.  The
 * flag is exported so the selftest can report which one it measured.      */
let _ftcalc = null;
try {
  _ftcalc = await import( './ftcalc.js' );
} catch ( e ) {
  _ftcalc = null;
}

export const usingW1Ftcalc = !!( _ftcalc &&
                                 typeof _ftcalc.FT_MulFix === 'function' &&
                                 typeof _ftcalc.FT_DivFix === 'function' &&
                                 typeof _ftcalc.FT_MulDiv === 'function' );


/* ------------------------------------------------------------------ */
/* S1  32-bit and fixed-point primitives                               */
/*     ports of ftcalc.h, ftcalc.c and psaux/psfixed.h                 */
/* ------------------------------------------------------------------ */

/* C: #define ADD_INT32( a, b ) (FT_Int32)( (FT_UInt32)(a) + (FT_UInt32)(b) )
 * JS `|0' is exactly the C cast-to-int32 of the mod-2^32 sum, and the
 * intermediate double sum of two int32s is always exact.                  */
const ADD_INT32 = ( a, b ) => ( a + b ) | 0;
const SUB_INT32 = ( a, b ) => ( a - b ) | 0;
const NEG_INT32 = ( a )    => ( -a ) | 0;
/* C: (FT_Int32)( (FT_UInt32)(a) * (FT_UInt32)(b) ) -- 32-bit wrapping mul. */
const MUL_INT32 = ( a, b ) => Math.imul( a, b );

/* ftcalc.h ADD_LONG: 64-bit wrapping add.  Values stay far below 2^53 for
 * any real font, so a plain add is exact here.                            */
const ADD_LONG = ( a, b ) => a + b;

const TWO53 = 9007199254740992;   /* 2^53 */

/* ftcalc.c, FT_INT64 build (x86-64 Linux, FT_INT64 is defined):
 *
 *   FT_Int64 ab = (FT_Int64)a_ * b_;
 *   return (FT_Long)( ( ab + 0x8000L + ( ab >> 63 ) ) >> 16 );
 *
 * `ab >> 63' is -1 for negative ab and 0 otherwise, and `>> 16' is an
 * arithmetic shift.  The product of two int32s needs up to 62 bits, so we
 * use exact double arithmetic when the product provably fits in 2^53 and
 * BigInt otherwise.                                                       */
function localMulFix( a, b ) {
  a = Number( a );
  b = Number( b );
  if ( Math.abs( a ) * Math.abs( b ) < TWO53 ) {
    /* product is exactly representable, so `a * b' is the true product */
    const ab = a * b;
    const t  = ab + 0x8000 + ( ab < 0 ? -1 : 0 );
    return Math.floor( t / 65536 );          /* arithmetic >> 16 */
  }
  const ab = BigInt( a ) * BigInt( b );
  const t  = ab + 0x8000n + ( ab < 0n ? -1n : 0n );
  return Number( t >> 16n );                 /* arithmetic >> 16 */
}

/* ftcalc.c, FT_INT64 build:
 *
 *   FT_MOVE_SIGN( FT_UInt64, a_, a, s );  ...
 *   q = b > 0 ? ( ( a << 16 ) + ( b >> 1 ) ) / b : 0x7FFFFFFFUL;
 *   return s < 0 ? NEG_LONG( q_ ) : q_;
 *
 * The division is an unsigned 64-bit TRUNCATING division; JS `/' is
 * floating point, so we floor and then correct.                           */
function localDivFix( a_, b_ ) {
  let s = 1, a, b;
  if ( a_ < 0 ) { a = -a_; s = -s; } else a = a_;
  if ( b_ < 0 ) { b = -b_; s = -s; } else b = b_;

  let q;
  if ( b > 0 ) {
    const num = a * 65536 + Math.floor( b / 2 );
    if ( num < TWO53 ) {
      q = Math.floor( num / b );
      /* correct the floating-point quotient to the exact integer one */
      while ( q * b > num ) q--;
      while ( ( q + 1 ) * b <= num ) q++;
    } else {
      q = Number( ( BigInt( a ) * 65536n + ( BigInt( b ) >> 1n ) ) / BigInt( b ) );
    }
  } else {
    q = 0x7FFFFFFF;
  }
  return s < 0 ? -q : q;
}

/* ftcalc.c, FT_INT64 build:
 *   d = c > 0 ? ( a * b + ( c >> 1 ) ) / c : 0x7FFFFFFFUL;   (unsigned 64-bit)
 */
function localMulDiv( a_, b_, c_ ) {
  let s = 1, a, b, c;
  if ( a_ < 0 ) { a = -a_; s = -s; } else a = a_;
  if ( b_ < 0 ) { b = -b_; s = -s; } else b = b_;
  if ( c_ < 0 ) { c = -c_; s = -s; } else c = c_;

  let d;
  if ( c > 0 ) {
    const prod = a * b;
    if ( prod < TWO53 ) {
      const num = prod + Math.floor( c / 2 );
      if ( num < TWO53 ) {
        d = Math.floor( num / c );
        while ( d * c > num ) d--;
        while ( ( d + 1 ) * c <= num ) d++;
      } else {
        d = Number( ( BigInt( a ) * BigInt( b ) + ( BigInt( c ) >> 1n ) ) / BigInt( c ) );
      }
    } else {
      d = Number( ( BigInt( a ) * BigInt( b ) + ( BigInt( c ) >> 1n ) ) / BigInt( c ) );
    }
  } else {
    d = 0x7FFFFFFF;
  }
  return s < 0 ? -d : d;
}

const FT_MulFix = usingW1Ftcalc ? _ftcalc.FT_MulFix : localMulFix;
const FT_DivFix = usingW1Ftcalc ? _ftcalc.FT_DivFix : localDivFix;
const FT_MulDiv = usingW1Ftcalc ? _ftcalc.FT_MulDiv : localMulDiv;

/* ftcalc.h: #define FT_MSB( x ) ( 31 - __builtin_clz( x ) ) -- x is FT_UInt32 */
function FT_MSB( x ) {
  x = x >>> 0;
  return x === 0 ? 0 : 31 - Math.clz32( x );
}

/* ftcalc.c FT_SqrtFixed, FT_INT64 build.  Used by the `sqrt' operator. */
function FT_SqrtFixed( v ) {
  v = v >>> 0;
  if ( v === 0 ) return 0;

  /* FT_UInt64 r = ( (FT_UInt64)v << 16 ) - 1;  fits in a double (< 2^48) */
  const r = v * 65536 - 1;
  let q = ( 1 << ( ( 17 + FT_MSB( v ) ) >> 1 ) ) >>> 0;
  let t;

  /* Babylonian method with rounded-up division; `(FT_UInt32)( r / t )'
   * truncates the 64-bit quotient to 32 bits.                            */
  do {
    t = q;
    q = ( ( t + ( ( Math.floor( r / t ) ) >>> 0 ) + 1 ) >>> 1 ) >>> 0;
  } while ( q !== t );

  return q;
}

/* psobjs.c cff_random -- a 32-bit xorshift.  All shifts are on FT_UInt32. */
export function cff_random( r ) {
  r = r >>> 0;
  r = ( r ^ ( r << 13 ) ) >>> 0;
  r = ( r ^ ( r >>> 17 ) ) >>> 0;
  r = ( r ^ ( r << 5 ) ) >>> 0;
  return r >>> 0;
}

/* psfixed.h */
/* cf2_intToFixed( i ) = (CF2_Fixed)( (FT_UInt32)(i) << 16 ) -- wraps. */
const cf2_intToFixed   = ( i ) => ( i << 16 ) | 0;
/* cf2_fixedToInt( x ) = (FT_Short)( ( (FT_UInt32)(x) + 0x8000U ) >> 16 )
 * NOTE the FT_Short cast: the result is truncated to 16 bits SIGNED.     */
const cf2_fixedToInt   = ( x ) => ( ( ( ( ( x >>> 0 ) + 0x8000 ) >>> 0 ) >>> 16 ) << 16 ) >> 16;
/* cf2_fracToFixed( x ) = ( (x) + 0x2000 - ( (x) < 0 ) ) >> 14 */
const cf2_fracToFixed  = ( x ) => ( x + 0x2000 - ( x < 0 ? 1 : 0 ) ) >> 14;
/* cf2_fixedAbs( x ) = ( (x) < 0 ? NEG_INT32( x ) : (x) ) */
const cf2_fixedAbs     = ( x ) => ( x < 0 ? NEG_INT32( x ) : x );
const CF2_FIXED_MAX    = 0x7FFFFFFF;

/* ftimage.h */
export const FT_OUTLINE_REVERSE_FILL   = 0x4;
export const FT_OUTLINE_HIGH_PRECISION = 0x100;
const FT_CURVE_TAG_ON    = 1;
const FT_CURVE_TAG_CUBIC = 2;

/* psfont.h / pshints.h */
const CF2_OPERAND_STACK_SIZE = 48;
const CF2_MAX_SUBR           = 16;
const CF2_STORAGE_SIZE       = 32;
const CF2_MAX_HINTS          = 96;
/* cffparse.h */
const CFF2_MAX_STACK         = 513;
const CFF2_DEFAULT_STACK     = 513;
const CFF_MAX_STACK_DEPTH    = 96;

/* cfferrs.h / fterrors.h -- we only need to distinguish "an error happened". */
const Err_Ok                   = 0;
const Err_Invalid_Glyph_Format = 1;
const Err_Syntax_Error         = 2;
const Err_Stack_Underflow      = 3;
const Err_Stack_Overflow       = 4;
const Err_Invalid_Stream_Op    = 5;
const Err_Invalid_File_Format  = 6;

/* pserror.h: CF2_SET_ERROR keeps the FIRST error seen. */
function setError( ctx, e ) {
  if ( !ctx.error ) ctx.error = e;
}

/* Optional test instrumentation (W3, additive): when a Map is installed via
 * setOpCensus() the interpreter records which Type 2 operators it actually
 * executed.  One branch per operator; changes no behaviour.              */
let _opCensus = null;
export function setOpCensus( m ) { _opCensus = m; }
function census( k ) {
  if ( _opCensus ) _opCensus.set( k, ( _opCensus.get( k ) || 0 ) + 1 );
}


/* ------------------------------------------------------------------ */
/* S2  the CFF container -- cffload.c + cffparse.c                     */
/* ------------------------------------------------------------------ */

function u8 ( d, p ) { return d[p]; }
function u16( d, p ) { return ( d[p] << 8 ) | d[p + 1]; }
function u24( d, p ) { return ( d[p] << 16 ) | ( d[p + 1] << 8 ) | d[p + 2]; }
function u32( d, p ) { return ( ( d[p] << 24 ) | ( d[p + 1] << 16 ) |
                                ( d[p + 2] << 8 ) | d[p + 3] ) >>> 0; }

function readOffsetAt( d, p, offSize ) {
  switch ( offSize ) {
    case 1:  return u8 ( d, p );
    case 2:  return u16( d, p );
    case 3:  return u24( d, p );
    default: return u32( d, p );
  }
}

/* cff_index_init().  `start' is an absolute offset into the font file.
 *
 * Note the empty-INDEX case: when count == 0 FreeType has only consumed the
 * 2-byte count (the offSize byte is never read), so the next structure
 * begins at start + 2 even though hdr_size was set to 3.                 */
function cffIndexInit( d, start, cff2 ) {
  const idx = {
    start, hdrSize: cff2 ? 5 : 3, count: 0, offSize: 0,
    dataOffset: 0, dataSize: 0, end: 0, ok: true
  };

  if ( cff2 ) {
    idx.count = u32( d, start );
    idx.hdrSize = 5;
  } else {
    idx.count = u16( d, start );
    idx.hdrSize = 3;
  }

  if ( idx.count > 0 ) {
    const offsize = d[ start + ( cff2 ? 4 : 2 ) ];
    if ( offsize < 1 || offsize > 4 ) {
      idx.ok = false; idx.count = 0;
      idx.end = start + ( cff2 ? 4 : 2 );
      return idx;
    }

    idx.offSize = offsize;
    const size = ( idx.count + 1 ) * offsize;
    idx.dataOffset = start + idx.hdrSize + size;

    /* size = last offset; data_size = --size */
    const last = readOffsetAt( d, start + idx.hdrSize + idx.count * offsize, offsize );
    if ( last === 0 ) {
      idx.ok = false; idx.count = 0; idx.end = idx.dataOffset;
      return idx;
    }
    idx.dataSize = last - 1;
    idx.end = idx.dataOffset + idx.dataSize;
  } else {
    idx.end = start + ( cff2 ? 4 : 2 );
  }
  return idx;
}

function cffIndexOffset( d, idx, element ) {
  return readOffsetAt( d, idx.start + idx.hdrSize + element * idx.offSize,
                       idx.offSize );
}

/* cff_index_access_element().  Used for the CharStrings and Top DICT
 * indexes (which FreeType loads with `load = 0' and never converts to a
 * pointer table).  Reproduces the zero-offset skipping and the clamp of
 * off2 against the *stream* size (the whole font file).                   */
function cffIndexAccessElement( cff, idx, element ) {
  const d = cff.data;
  if ( !( idx && idx.count > element ) )
    return null;                                     /* Invalid_Argument */

  const off1 = cffIndexOffset( d, idx, element );
  let off2 = 0;
  if ( off1 !== 0 ) {
    let e = element;
    do {
      e++;
      off2 = cffIndexOffset( d, idx, e );
    } while ( off2 === 0 && e < idx.count );
  }

  const streamSize = cff.streamSize;
  if ( off2 > streamSize + 1 ||
       idx.dataOffset > streamSize - off2 + 1 )
    off2 = streamSize - idx.dataOffset + 1;

  if ( off1 && off2 > off1 )
    return { start: idx.dataOffset + off1 - 1,
             end:   idx.dataOffset + off2 - 1 };

  return { start: 0, end: 0 };                       /* empty element */
}

/* cff_index_get_pointers( pool = NULL ).  Used for the local and global
 * subroutine indexes.  Different clamping from access_element: the first
 * offset is forced to 0, offsets are made monotonic and clamped to
 * data_size.  Result is a table of count+1 absolute file offsets.         */
function cffIndexGetPointers( cff, idx ) {
  const d = cff.data;
  if ( !idx || idx.count === 0 ) return null;

  const tbl = new Array( idx.count + 1 );
  let cur = cffIndexOffset( d, idx, 0 ) - 1;
  if ( cur !== 0 ) cur = 0;                          /* sanity check */

  tbl[0] = idx.dataOffset + cur;
  for ( let n = 1; n <= idx.count; n++ ) {
    let next = cffIndexOffset( d, idx, n ) - 1;
    if ( next < cur ) next = cur;
    else if ( next > idx.dataSize ) next = idx.dataSize;
    tbl[n] = idx.dataOffset + next;
    cur = next;
  }
  return tbl;
}

/* cffdecode.c cff_compute_bias() */
function cffComputeBias( charstringType, numSubrs ) {
  if ( charstringType === 1 ) return 0;
  if ( numSubrs < 1240 )      return 107;
  if ( numSubrs < 33900 )     return 1131;
  return 32768;
}


/* ---- DICT number decoding (cffparse.c) ---- */

const POWER_TENS = [ 1, 10, 100, 1000, 10000, 100000, 1000000,
                     10000000, 100000000, 1000000000 ];
/* FT_LONG_MAX / 10^n on LP64 */
const FT_LONG_MAX = 9223372036854775807;
const POWER_TEN_LIMITS = POWER_TENS.map( ( p ) => Math.floor( FT_LONG_MAX / p ) );

/* cff_parse_integer() */
function cffParseInteger( d, p, limit ) {
  const v = d[p++];
  if ( v === 28 ) {
    if ( p + 2 > limit && limit >= p ) return 0;
    return ( ( ( d[p] << 8 ) | d[p + 1] ) << 16 ) >> 16;      /* FT_Short */
  }
  if ( v === 29 ) {
    if ( p + 4 > limit && limit >= p ) return 0;
    return ( ( d[p] << 24 ) | ( d[p + 1] << 16 ) |
             ( d[p + 2] << 8 ) | d[p + 3] ) | 0;              /* FT_Long   */
  }
  if ( v < 247 ) return v - 139;
  if ( v < 251 ) {
    if ( p + 1 > limit && limit >= p ) return 0;
    return ( v - 247 ) * 256 + d[p] + 108;
  }
  if ( p + 1 > limit && limit >= p ) return 0;
  return -( v - 251 ) * 256 - d[p] - 108;
}

/* cff_parse_real().  `out.scaling' is returned when requested (the
 * cff_parse_fixed_dynamic path used by FontMatrix).                       */
function cffParseReal( d, start, limit, powerTen, out ) {
  let p = start;
  let nib, phase = 4;
  let result = 0, number = 0, exponent = 0;
  let sign = 0, exponentSign = 0, haveOverflow = 0;
  let exponentAdd = 0, integerLength = 0, fractionLength = 0;

  if ( out ) out.scaling = 0;

  for (;;) {
    if ( phase ) {
      p++;
      if ( p + 1 > limit && limit >= p ) { if ( out ) out.scaling = 0; return 0; }
    }
    nib = ( d[p] >> phase ) & 0xF;
    phase = 4 - phase;

    if ( nib === 0xE ) sign = 1;
    else if ( nib > 9 ) break;
    else {
      if ( number >= 0xCCCCCCC ) exponentAdd++;
      else if ( nib || number ) { integerLength++; number = number * 10 + nib; }
    }
  }

  if ( nib === 0xA ) {
    for (;;) {
      if ( phase ) {
        p++;
        if ( p + 1 > limit && limit >= p ) { if ( out ) out.scaling = 0; return 0; }
      }
      nib = ( d[p] >> phase ) & 0xF;
      phase = 4 - phase;
      if ( nib >= 10 ) break;

      if ( !nib && !number ) exponentAdd--;
      else if ( number < 0xCCCCCCC && fractionLength < 9 ) {
        fractionLength++; number = number * 10 + nib;
      }
    }
  }

  if ( nib === 12 ) { exponentSign = 1; nib = 11; }

  if ( nib === 11 ) {
    for (;;) {
      if ( phase ) {
        p++;
        if ( p + 1 > limit && limit >= p ) { if ( out ) out.scaling = 0; return 0; }
      }
      nib = ( d[p] >> phase ) & 0xF;
      phase = 4 - phase;
      if ( nib >= 10 ) break;
      if ( exponent > 1000 ) haveOverflow = 1;
      else exponent = exponent * 10 + nib;
    }
    if ( exponentSign ) exponent = -exponent;
  }

  if ( !number ) return sign ? -result : result;

  if ( haveOverflow )
    return exponentSign ? 0 : ( sign ? -0x7FFFFFFF : 0x7FFFFFFF );

  exponent += powerTen + exponentAdd;

  if ( out ) {
    fractionLength += integerLength;
    exponent       += integerLength;

    if ( fractionLength <= 5 ) {
      if ( number > 0x7FFF ) {
        result = FT_DivFix( number, 10 );
        out.scaling = exponent - fractionLength + 1;
      } else {
        if ( exponent > 0 ) {
          const newFractionLength = Math.min( exponent, 5 );
          const shift = newFractionLength - fractionLength;
          if ( shift > 0 ) {
            exponent -= newFractionLength;
            number   *= POWER_TENS[shift];
            if ( number > 0x7FFF ) { number = Math.floor( number / 10 ); exponent += 1; }
          } else exponent -= fractionLength;
        } else exponent -= fractionLength;

        /* C: result = (FT_Long)( (FT_ULong)number << 16 ) */
        result = number * 65536;
        out.scaling = exponent;
      }
    } else {
      if ( Math.floor( number / POWER_TENS[fractionLength - 5] ) > 0x7FFF ) {
        result = FT_DivFix( number, POWER_TENS[fractionLength - 4] );
        out.scaling = exponent - 4;
      } else {
        result = FT_DivFix( number, POWER_TENS[fractionLength - 5] );
        out.scaling = exponent - 5;
      }
    }
  } else {
    integerLength  += exponent;
    fractionLength -= exponent;

    if ( integerLength > 5 )  return sign ? -0x7FFFFFFF : 0x7FFFFFFF;
    if ( integerLength < -5 ) return 0;

    if ( integerLength < 0 ) {
      number = Math.floor( number / POWER_TENS[-integerLength] );
      fractionLength += integerLength;
    }
    if ( fractionLength === 10 ) { number = Math.floor( number / 10 ); fractionLength -= 1; }

    if ( fractionLength > 0 ) {
      if ( Math.floor( number / POWER_TENS[fractionLength] ) > 0x7FFF )
        return sign ? -result : result;
      result = FT_DivFix( number, POWER_TENS[fractionLength] );
    } else {
      number *= POWER_TENS[-fractionLength];
      if ( number > 0x7FFF ) return sign ? -0x7FFFFFFF : 0x7FFFFFFF;
      result = number * 65536;
    }
  }

  return sign ? -result : result;
}

/* cff_parse_num(): "binary-coded decimal is truncated to integer" */
function cffParseNum( d, p, limit ) {
  if ( d[p] === 30 )
    return Math.floor( cffParseReal( d, p, limit, 0, null ) / 65536 );
  if ( d[p] === 255 ) {
    /* internal CFF2 blend encoding; (FT_Short) of a rounded 8.8 */
    const v = ( ( ( d[p + 1] << 16 ) | ( d[p + 2] << 8 ) | d[p + 3] ) + 0x80 ) >> 8;
    return ( v << 16 ) >> 16;
  }
  return cffParseInteger( d, p, limit );
}

/* do_fixed() */
function cffDoFixed( d, p, limit, scaling ) {
  if ( d[p] === 30 ) return cffParseReal( d, p, limit, scaling, null );
  if ( d[p] === 255 ) {
    let val = ( ( d[p + 1] << 24 ) | ( d[p + 2] << 16 ) |
                ( d[p + 3] << 8 ) | d[p + 4] ) | 0;
    if ( scaling ) {
      if ( Math.abs( val ) > POWER_TEN_LIMITS[scaling] )
        return val > 0 ? 0x7FFFFFFF : -0x7FFFFFFF;
      val *= POWER_TENS[scaling];
    }
    return val;
  }
  let val = cffParseInteger( d, p, limit );
  if ( scaling ) {
    /* C: ( FT_ABS( val ) << 16 ) -- FT_Long, 64-bit, no wrap */
    if ( Math.abs( val ) * 65536 > POWER_TEN_LIMITS[scaling] )
      return val > 0 ? 0x7FFFFFFF : -0x7FFFFFFF;
    val *= POWER_TENS[scaling];
  }
  if ( val > 0x7FFF )  return 0x7FFFFFFF;
  if ( val < -0x7FFF ) return -0x7FFFFFFF;
  return val * 65536;                     /* (FT_ULong)val << 16 */
}

const cffParseFixed = ( d, p, limit ) => cffDoFixed( d, p, limit, 0 );

/* cff_parse_fixed_dynamic() */
function cffParseFixedDynamic( d, p, limit, out ) {
  if ( d[p] === 30 ) return cffParseReal( d, p, limit, 0, out );

  const number = cffParseInteger( d, p, limit );
  if ( number > 0x7FFF ) {
    let integerLength;
    for ( integerLength = 5; integerLength < 10; integerLength++ )
      if ( number < POWER_TENS[integerLength] ) break;

    if ( Math.floor( number / POWER_TENS[integerLength - 5] ) > 0x7FFF ) {
      out.scaling = integerLength - 4;
      return FT_DivFix( number, POWER_TENS[integerLength - 4] );
    }
    out.scaling = integerLength - 5;
    return FT_DivFix( number, POWER_TENS[integerLength - 5] );
  }
  out.scaling = 0;
  return number * 65536;
}

/* ftcalc.c FT_Matrix_Check() */
function ftMatrixCheck( m ) {
  let xx = m.xx, xy = m.xy, yx = m.yx, yy = m.yy;
  const val = ( Math.abs( xx ) | Math.abs( xy ) | Math.abs( yx ) | Math.abs( yy ) ) >>> 0;
  if ( !val || val > 0x7FFFFFFF ) return 0;

  const shift = FT_MSB( val ) - 12;
  if ( shift > 0 ) { xx >>= shift; xy >>= shift; yx >>= shift; yy >>= shift; }

  /* C does these products in FT_Fixed (FT_Long, 64-bit here) */
  const temp1 = 32 * Math.abs( xx * yy - xy * yx );
  const temp2 = xx * xx + xy * xy + yx * yx + yy * yy;
  return temp1 <= temp2 ? 0 : 1;
}

/* ftcalc.c FT_Matrix_Multiply_Scaled(): b = a * b, scaled */
function ftMatrixMultiplyScaled( a, b, scaling ) {
  const val = 0x10000 * scaling;
  const xx = ADD_LONG( FT_MulDiv( a.xx, b.xx, val ), FT_MulDiv( a.xy, b.yx, val ) );
  const xy = ADD_LONG( FT_MulDiv( a.xx, b.xy, val ), FT_MulDiv( a.xy, b.yy, val ) );
  const yx = ADD_LONG( FT_MulDiv( a.yx, b.xx, val ), FT_MulDiv( a.yy, b.yx, val ) );
  const yy = ADD_LONG( FT_MulDiv( a.yx, b.xy, val ), FT_MulDiv( a.yy, b.yy, val ) );
  b.xx = xx; b.xy = xy; b.yx = yx; b.yy = yy;
}

/* ftcalc.c FT_Vector_Transform_Scaled() */
function ftVectorTransformScaled( v, m, scaling ) {
  const val = 0x10000 * scaling;
  const x = ADD_LONG( FT_MulDiv( v.x, m.xx, val ), FT_MulDiv( v.y, m.xy, val ) );
  const y = ADD_LONG( FT_MulDiv( v.x, m.yx, val ), FT_MulDiv( v.y, m.yy, val ) );
  v.x = x; v.y = y;
}

/* ftoutln.c FT_Vector_Transform() / FT_Outline_Transform() */
function ftVectorTransform( vx, vy, m ) {
  const x = ADD_LONG( FT_MulFix( vx, m.xx ), FT_MulFix( vy, m.xy ) );
  const y = ADD_LONG( FT_MulFix( vx, m.yx ), FT_MulFix( vy, m.yy ) );
  return [ x, y ];
}


/* ---- DICT running (cff_parser_run) ---- */

/* Only the operators FreeType actually stores are handled; everything else
 * is "an unknown operator ... we will ignore it for now" and just clears
 * the operand stack, exactly like the C.                                  */
const TOPDICT_NUM = {
  13:    'unique_id',
  15:    'charset_offset',
  16:    'encoding_offset',
  17:    'charstrings_offset',
  0x103: 'underline_position',
  0x104: 'underline_thickness',
  0x105: 'paint_type',
  0x106: 'charstring_type',
  0x108: 'stroke_width',
  0x114: 'synthetic_base',
  0x11F: 'cid_font_version',
  0x120: 'cid_font_revision',
  0x121: 'cid_font_type',
  0x122: 'cid_count',
  0x123: 'cid_uid_base',
  0x124: 'cid_fd_array_offset',
  0x125: 'cid_fd_select_offset'
};
const TOPDICT_STRING = {
  0: 'version', 1: 'notice', 0x100: 'copyright', 2: 'full_name',
  3: 'family_name', 4: 'weight', 0x115: 'embedded_postscript',
  0x126: 'cid_font_name'
};
const PRIVATE_NUM = {
  0x10A: 'blue_shift', 0x10B: 'blue_fuzz', 10: 'standard_width',
  11: 'standard_height', 0x110: 'lenIV', 0x111: 'language_group',
  0x113: 'initial_random_seed', 19: 'local_subrs_offset',
  20: 'default_width', 21: 'nominal_width'
};
const PRIVATE_DELTA = {
  6: [ 'blue_values', 14 ], 7: [ 'other_blues', 10 ],
  8: [ 'family_blues', 14 ], 9: [ 'family_other_blues', 10 ],
  0x10C: [ 'snap_widths', 13 ], 0x10D: [ 'snap_heights', 13 ]
};

/* cfftoken.h, CFF2_CODE_TOPDICT / CFF2_CODE_PRIVATE sections */
const CFF2_TOPDICT_NUM = {
  17:    'charstrings_offset',
  24:    'vstore_offset',
  0x124: 'cid_fd_array_offset',
  0x125: 'cid_fd_select_offset'
};
const CFF2_PRIVATE_NUM = {
  0x10A: 'blue_shift', 0x10B: 'blue_fuzz', 10: 'standard_width',
  11: 'standard_height', 0x111: 'language_group',
  19: 'local_subrs_offset', 22: 'vsindex'
};

function newFontDict() {
  return {
    underline_position: -100, underline_thickness: 50, charstring_type: 2,
    font_matrix: { xx: 0x10000, yx: 0, xy: 0, yy: 0x10000 },
    font_offset: { x: 0, y: 0 },
    has_font_matrix: false, units_per_em: 0,
    font_bbox: { xMin: 0, yMin: 0, xMax: 0, yMax: 0 },
    cid_count: 8720,
    version: 0xFFFF, notice: 0xFFFF, copyright: 0xFFFF, full_name: 0xFFFF,
    family_name: 0xFFFF, weight: 0xFFFF, embedded_postscript: 0xFFFF,
    cid_registry: 0xFFFF, cid_ordering: 0xFFFF, cid_font_name: 0xFFFF,
    cid_supplement: 0, charset_offset: 0, encoding_offset: 0,
    charstrings_offset: 0, private_offset: 0, private_size: 0,
    cid_fd_array_offset: 0, cid_fd_select_offset: 0,
    num_designs: 0, num_axes: 0, maxstack: 48
  };
}

function newPrivateDict() {
  return {
    num_blue_values: 0, num_other_blues: 0, num_family_blues: 0,
    num_family_other_blues: 0,
    blue_values: [], other_blues: [], family_blues: [], family_other_blues: [],
    blue_scale: Math.trunc( 0.039625 * 0x10000 * 1000 ),
    blue_shift: 7, blue_fuzz: 1,
    standard_width: 0, standard_height: 0,
    snap_widths: [], snap_heights: [],
    force_bold: 0, force_bold_threshold: 0,
    lenIV: -1, language_group: 0,
    expansion_factor: Math.trunc( 0.06 * 0x10000 ),
    initial_random_seed: 0, local_subrs_offset: 0,
    default_width: 0, nominal_width: 0, vsindex: 0
  };
}

/* cff_parser_run() for a CFF (not CFF2) Top/Font or Private DICT. */
function cffParserRun( cff, obj, start, limit, isPrivate, stackSize ) {
  const d = cff.data;
  const stack = [];                        /* positions of the operands */
  /* cffload.c: stackSize = cff2 ? CFF2_DEFAULT_STACK : CFF_MAX_STACK_DEPTH,
   * and the Private DICT parser gets one more.                          */
  const maxOps = stackSize || ( cff.cff2 ? CFF2_DEFAULT_STACK : CFF_MAX_STACK_DEPTH );

  let p = start;
  while ( p < limit ) {
    let v = d[p];

    if ( v >= 27 && v !== 31 && v !== 255 ) {
      if ( stack.length >= maxOps ) return Err_Invalid_File_Format;   /* Stack_Overflow */
      stack.push( p );

      if ( v === 30 ) {
        p++;
        for (;;) {
          if ( p >= limit ) return Err_Ok;
          v = d[p] >> 4;  if ( v === 15 ) break;
          v = d[p] & 0xF; if ( v === 15 ) break;
          p++;
        }
      }
      else if ( v === 28 ) p += 2;
      else if ( v === 29 ) p += 4;
      else if ( v > 246 )  p += 1;
    } else {
      /* an operator */
      let code = v;
      if ( v === 12 ) {
        p++;
        if ( p >= limit ) return Err_Invalid_File_Format;
        code = 0x100 | d[p];
      }
      const numArgs = stack.length;
      if ( cff.cff2 && isPrivate && code === 23 ) {
        /* cffparse.c cff_parse_blend + cff_blend_doBlend.  The operand just
         * before the operator is numBlends; numBlends * lenBV operands are
         * consumed and numBlends results remain.  With no variation
         * coordinates every weight is zero, so the results are the base
         * values -- we only have to get the COUNTS right so that later keys
         * see the correct operands.  Note the stack is NOT cleared.       */
        const nB = cffParseNum( d, stack[ stack.length - 1 ], limit );
        const lenBV = blendLenBV( cff, obj );
        const numOperands = nB * lenBV;
        const count = stack.length - 1;
        if ( nB < 0 || lenBV < 1 || numOperands > count ) return Err_Invalid_File_Format;
        const base = count - numOperands;
        stack.length = base + nB;
        p++;
        continue;
      }
      const e = dictOperator( cff, obj, code, stack, numArgs, limit, isPrivate );
      if ( e ) return e;
      stack.length = 0;                    /* "clear stack" */
    }
    p++;
  }
  return Err_Ok;
}

function dictOperator( cff, obj, code, stack, numArgs, limit, isPrivate ) {
  const d = cff.data;

  if ( cff.cff2 ) {
    /* CFF2 DICTs are a different, much smaller token set. */
    if ( !isPrivate ) {
      if ( code in CFF2_TOPDICT_NUM ) {
        if ( numArgs < 1 ) return Err_Invalid_File_Format;
        obj[ CFF2_TOPDICT_NUM[code] ] = cffParseNum( d, stack[0], limit );
        return Err_Ok;
      }
      if ( code === 0x107 ) return cffParseFontMatrix( cff, obj, stack, limit );
      if ( code === 25 ) {                                  /* maxstack */
        if ( numArgs < 1 ) return Err_Invalid_File_Format;
        let m = cffParseNum( d, stack[0], limit ) >>> 0;
        if ( m > CFF2_MAX_STACK ) m = CFF2_MAX_STACK;
        if ( m < CFF2_DEFAULT_STACK ) m = CFF2_DEFAULT_STACK;
        obj.maxstack = m;
        return Err_Ok;
      }
      if ( code === 18 ) return cffParsePrivateDict( cff, obj, stack, limit );
      return Err_Ok;
    }
    if ( code in CFF2_PRIVATE_NUM ) {
      if ( numArgs < 1 ) return Err_Invalid_File_Format;
      obj[ CFF2_PRIVATE_NUM[code] ] = cffParseNum( d, stack[0], limit );
      return Err_Ok;
    }
    if ( code in PRIVATE_DELTA ) { /* blue values etc: same shape */ }
    else if ( code === 0x109 ) {
      if ( numArgs < 1 ) return Err_Invalid_File_Format;
      obj.blue_scale = cffDoFixed( d, stack[0], limit, 3 );
      return Err_Ok;
    }
    else if ( code !== 23 ) return Err_Ok;                 /* 23 = blend */
    else return Err_Ok;
  }

  if ( !isPrivate ) {
    if ( code in TOPDICT_NUM ) {
      if ( numArgs < 1 ) return Err_Invalid_File_Format;
      obj[ TOPDICT_NUM[code] ] = cffParseNum( d, stack[0], limit );
      /* charstring_type is an FT_Int -- (FT_Int32) truncation */
      if ( TOPDICT_NUM[code] === 'charstring_type' )
        obj.charstring_type |= 0;
      return Err_Ok;
    }
    if ( code in TOPDICT_STRING ) {
      if ( numArgs < 1 ) return Err_Invalid_File_Format;
      obj[ TOPDICT_STRING[code] ] = cffParseNum( d, stack[0], limit ) >>> 0;
      return Err_Ok;
    }
    if ( code === 0x102 ) {                                   /* ItalicAngle */
      if ( numArgs < 1 ) return Err_Invalid_File_Format;
      obj.italic_angle = cffParseFixed( d, stack[0], limit );
      return Err_Ok;
    }
    if ( code === 0x101 ) {                                   /* isFixedPitch */
      if ( numArgs < 1 ) return Err_Invalid_File_Format;
      obj.is_fixed_pitch = cffParseNum( d, stack[0], limit ) ? 1 : 0;
      return Err_Ok;
    }
    if ( code === 0x107 ) return cffParseFontMatrix( cff, obj, stack, limit );
    if ( code === 5 )     return cffParseFontBBox  ( cff, obj, stack, limit );
    if ( code === 18 )    return cffParsePrivateDict( cff, obj, stack, limit );
    if ( code === 0x11E ) return cffParseCidRos    ( cff, obj, stack, limit );
    if ( code === 0x118 ) return cffParseMultipleMaster( cff, obj, stack, limit );
    return Err_Ok;                                            /* ignored */
  }

  /* Private DICT */
  if ( code in PRIVATE_NUM ) {
    if ( numArgs < 1 ) return Err_Invalid_File_Format;
    const name = PRIVATE_NUM[code];
    let val = cffParseNum( d, stack[0], limit );
    if ( name === 'lenIV' || name === 'language_group' ) val |= 0;   /* FT_Int */
    obj[name] = val;
    return Err_Ok;
  }
  if ( code in PRIVATE_DELTA ) {
    const spec = PRIVATE_DELTA[code];
    const name = spec[0], max = spec[1];
    let n = numArgs > max ? max : numArgs;
    obj[ 'num_' + name ] = n & 0xFF;                       /* FT_Byte count */
    const arr = obj[name];
    arr.length = 0;
    let val = 0, i = 0;
    while ( n > 0 ) {
      val = ADD_LONG( val, cffParseNum( d, stack[i++], limit ) );
      arr.push( val ); n--;
    }
    return Err_Ok;
  }
  if ( code === 0x109 ) {                                  /* BlueScale */
    if ( numArgs < 1 ) return Err_Invalid_File_Format;
    obj.blue_scale = cffDoFixed( d, stack[0], limit, 3 );
    return Err_Ok;
  }
  if ( code === 0x10E ) {                                  /* ForceBold */
    if ( numArgs < 1 ) return Err_Invalid_File_Format;
    obj.force_bold = cffParseNum( d, stack[0], limit ) ? 1 : 0;
    return Err_Ok;
  }
  if ( code === 0x10F || code === 0x112 ) {                /* thresholds */
    if ( numArgs < 1 ) return Err_Invalid_File_Format;
    obj[ code === 0x10F ? 'force_bold_threshold' : 'expansion_factor' ] =
      cffParseFixed( d, stack[0], limit );
    return Err_Ok;
  }
  return Err_Ok;
}

/* lenBV for a Private DICT blend: regionIdxCount of the ItemVariationData
 * selected by the dict's vsindex, plus one for the default master.      */
function blendLenBV( cff, priv ) {
  const vs = cff.vstore;
  if ( !vs || !vs.dataCount ) return 0;
  const idx = ( priv && priv.vsindex ) | 0;
  if ( idx >= vs.dataCount ) return 0;
  return vs.regionCounts[idx] + 1;
}

/* cff_parse_font_matrix() */
function cffParseFontMatrix( cff, dict, stack, limit ) {
  const d = cff.data;
  if ( stack.length < 6 ) return Err_Invalid_File_Format;   /* Stack_Underflow */

  dict.has_font_matrix = true;

  const values = new Array( 6 ), scalings = new Array( 6 );
  let maxScaling = -Infinity, minScaling = Infinity;
  const out = { scaling: 0 };

  for ( let i = 0; i < 6; i++ ) {
    values[i] = cffParseFixedDynamic( d, stack[i], limit, out );
    scalings[i] = out.scaling;
    if ( values[i] ) {
      if ( scalings[i] > maxScaling ) maxScaling = scalings[i];
      if ( scalings[i] < minScaling ) minScaling = scalings[i];
    }
  }

  if ( maxScaling < -9 || maxScaling > 0 ||
       ( maxScaling - minScaling ) < 0 || ( maxScaling - minScaling ) > 9 )
    return unlikelyMatrix( dict );

  for ( let i = 0; i < 6; i++ ) {
    const value = values[i];
    if ( !value ) continue;
    const divisor = POWER_TENS[ maxScaling - scalings[i] ];
    const halfDivisor = divisor >> 1;
    /* C's integer division truncates toward zero */
    values[i] = value < 0 ? Math.trunc( ( value - halfDivisor ) / divisor )
                          : Math.trunc( ( value + halfDivisor ) / divisor );
  }

  dict.font_matrix = { xx: values[0], yx: values[1], xy: values[2], yy: values[3] };
  dict.font_offset = { x: values[4], y: values[5] };
  dict.units_per_em = POWER_TENS[ -maxScaling ];

  if ( !ftMatrixCheck( dict.font_matrix ) ) return unlikelyMatrix( dict );
  return Err_Ok;
}

function unlikelyMatrix( dict ) {
  dict.font_matrix = { xx: 0x10000, yx: 0, xy: 0, yy: 0x10000 };
  dict.font_offset = { x: 0, y: 0 };
  dict.units_per_em = 1;
  return Err_Ok;
}

function cffParseFontBBox( cff, dict, stack, limit ) {
  if ( stack.length < 4 ) return Err_Invalid_File_Format;
  const d = cff.data;
  /* FT_RoundFix() */
  const R = ( x ) => ( x >= 0 ?  ( ( x + 0x8000 ) & ~0xFFFF )
                             : -( ( -x + 0x8000 ) & ~0xFFFF ) );
  dict.font_bbox = {
    xMin: R( cffParseFixed( d, stack[0], limit ) ),
    yMin: R( cffParseFixed( d, stack[1], limit ) ),
    xMax: R( cffParseFixed( d, stack[2], limit ) ),
    yMax: R( cffParseFixed( d, stack[3], limit ) )
  };
  return Err_Ok;
}

function cffParsePrivateDict( cff, dict, stack, limit ) {
  if ( stack.length < 2 ) return Err_Invalid_File_Format;
  const d = cff.data;
  const size = cffParseNum( d, stack[0], limit );
  if ( size < 0 ) return Err_Invalid_File_Format;
  const off = cffParseNum( d, stack[1], limit );
  if ( off < 0 ) return Err_Invalid_File_Format;
  dict.private_size = size;
  dict.private_offset = off;
  return Err_Ok;
}

function cffParseCidRos( cff, dict, stack, limit ) {
  if ( stack.length < 3 ) return Err_Invalid_File_Format;
  const d = cff.data;
  dict.cid_registry   = cffParseNum( d, stack[0], limit ) >>> 0;
  dict.cid_ordering   = cffParseNum( d, stack[1], limit ) >>> 0;
  dict.cid_supplement = cffParseNum( d, stack[2], limit );
  return Err_Ok;
}

function cffParseMultipleMaster( cff, dict, stack, limit ) {
  if ( stack.length < 5 ) return Err_Invalid_File_Format;
  const numDesigns = cffParseNum( cff.data, stack[0], limit );
  if ( numDesigns > 16 || numDesigns < 2 ) return Err_Invalid_File_Format;
  dict.num_designs = numDesigns & 0xFFFF;
  dict.num_axes    = ( stack.length - 4 ) & 0xFFFF;
  return Err_Ok;
}


/* ---- charset / encoding / FDSelect ---- */

/* cffload.c cff_standard_encoding[256] -- code to SID.  Needed by the
 * deprecated 4-argument `seac' form of endchar.                          */
const CFF_STANDARD_ENCODING = new Uint16Array( [
  0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
  1,2,3,4,5,6,7,8, 9,10,11,12,13,14,15,16,
  17,18,19,20,21,22,23,24, 25,26,27,28,29,30,31,32,
  33,34,35,36,37,38,39,40, 41,42,43,44,45,46,47,48,
  49,50,51,52,53,54,55,56, 57,58,59,60,61,62,63,64,
  65,66,67,68,69,70,71,72, 73,74,75,76,77,78,79,80,
  81,82,83,84,85,86,87,88, 89,90,91,92,93,94,95,0,
  0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
  0,96,97,98,99,100,101,102, 103,104,105,106,107,108,109,110,
  0,111,112,113,114,0,115,116, 117,118,119,120,121,122,0,123,
  0,124,125,126,127,128,129,130, 131,0,132,133,0,134,135,136,
  137,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
  0,138,0,139,0,0,0,0, 140,141,142,143,0,0,0,0,
  0,144,0,0,0,145,0,0, 146,147,148,149,0,0,0,0
] );

/* The three predefined charsets (cffload.c). */
const CFF_ISOADOBE_CHARSET = ( function () {
  const a = new Uint16Array( 229 );
  for ( let i = 0; i < 229; i++ ) a[i] = i;
  return a;
} )();
const CFF_EXPERT_CHARSET = new Uint16Array( [
  0,1,229,230,231,232,233,234,235,236,237,238,13,14,15,99,
  239,240,241,242,243,244,245,246,247,248,27,28,249,250,251,252,
  253,254,255,256,257,258,259,260,261,262,263,264,265,266,109,110,
  267,268,269,270,271,272,273,274,275,276,277,278,279,280,281,282,
  283,284,285,286,287,288,289,290,291,292,293,294,295,296,297,298,
  299,300,301,302,303,304,305,306,307,308,309,310,311,312,313,314,
  315,316,317,318,158,155,163,319,320,321,322,323,324,325,326,150,
  164,169,327,328,329,330,331,332,333,334,335,336,337,338,339,340,
  341,342,343,344,345,346,347,348,349,350,351,352,353,354,355,356,
  357,358,359,360,361,362,363,364,365,366,367,368,369,370,371,372,
  373,374,375,376,377,378
] );
const CFF_EXPERTSUBSET_CHARSET = new Uint16Array( [
  0,1,231,232,235,236,237,238,13,14,15,99,239,240,241,242,
  243,244,245,246,247,248,27,28,249,250,251,253,254,255,256,257,
  258,259,260,261,262,263,264,265,266,109,110,267,268,269,270,272,
  300,301,302,305,314,315,158,155,163,320,321,322,323,324,325,326,
  150,164,169,327,328,329,330,331,332,333,334,335,336,337,338,339,
  340,341,342,343,344,345,346
] );

/* cff_charset_load() */
function cffCharsetLoad( cff, numGlyphs, baseOffset, offset, invert ) {
  const d = cff.data;
  const charset = { format: 0, offset: 0, sids: null, cids: null,
                    max_cid: 0, num_glyphs: 0 };

  if ( offset > 2 ) {
    charset.offset = baseOffset + offset;
    charset.format = d[ charset.offset ];
    const sids = new Uint16Array( numGlyphs );
    charset.sids = sids;
    sids[0] = 0;                                   /* the .notdef glyph */

    let p = charset.offset + 1;
    switch ( charset.format ) {
      case 0:
        for ( let j = 1; j < numGlyphs; j++, p += 2 ) sids[j] = u16( d, p );
        break;

      case 1:
      case 2: {
        let j = 1;
        while ( j < numGlyphs ) {
          let glyphSid = u16( d, p ); p += 2;
          let nleft;
          if ( charset.format === 2 ) { nleft = u16( d, p ); p += 2; }
          else                        { nleft = d[p];       p += 1; }

          if ( glyphSid > 0xFFFF - nleft ) nleft = 0xFFFF - glyphSid;
          for ( let i = 0; j < numGlyphs && i <= nleft; i++, j++, glyphSid++ )
            sids[j] = glyphSid;
        }
        break;
      }

      default:
        return { err: Err_Invalid_File_Format, charset: null };
    }
  } else {
    charset.offset = offset;                       /* record charset type */
    let src;
    switch ( offset ) {
      case 0:
        if ( numGlyphs > 229 ) return { err: Err_Invalid_File_Format, charset: null };
        src = CFF_ISOADOBE_CHARSET; break;
      case 1:
        if ( numGlyphs > 166 ) return { err: Err_Invalid_File_Format, charset: null };
        src = CFF_EXPERT_CHARSET; break;
      case 2:
        if ( numGlyphs > 87 )  return { err: Err_Invalid_File_Format, charset: null };
        src = CFF_EXPERTSUBSET_CHARSET; break;
      default:
        return { err: Err_Invalid_File_Format, charset: null };
    }
    charset.sids = new Uint16Array( numGlyphs );
    for ( let i = 0; i < numGlyphs; i++ ) charset.sids[i] = src[i];
  }

  if ( invert ) cffCharsetComputeCids( charset, numGlyphs );
  return { err: Err_Ok, charset };
}

/* cff_charset_compute_cids(): "When multiple GIDs map to the same CID, we
 * choose the lowest GID" -- the loop runs downwards.                      */
function cffCharsetComputeCids( charset, numGlyphs ) {
  if ( charset.max_cid > 0 ) return;
  let maxCid = 0;
  for ( let i = 0; i < numGlyphs; i++ )
    if ( charset.sids[i] > maxCid ) maxCid = charset.sids[i];

  charset.cids = new Uint16Array( maxCid + 1 );
  for ( let i = numGlyphs - 1; i >= 0; i-- )
    charset.cids[ charset.sids[i] ] = i;

  charset.max_cid = maxCid;
  charset.num_glyphs = numGlyphs;
}

function cffCharsetCidToGindex( charset, cid ) {
  return cid <= charset.max_cid ? charset.cids[cid] : 0;
}

/* CFF_Load_FD_Select() */
function cffLoadFdSelect( cff, numGlyphs, offset ) {
  const d = cff.data;
  const fdselect = { format: d[offset], data: 0, dataSize: 0 };

  switch ( fdselect.format ) {
    case 0:
      fdselect.dataSize = numGlyphs;
      fdselect.data = offset + 1;
      break;
    case 3: {
      const numRanges = u16( d, offset + 1 );
      if ( !numRanges ) return null;
      fdselect.dataSize = numRanges * 3 + 2;
      fdselect.data = offset + 3;
      break;
    }
    default:
      /* cffload.c CFF_Load_FD_Select: "hmm... that's wrong"
       *   default: error = FT_THROW( Invalid_File_Format );
       * Returning a null selector instead would silently accept fonts
       * FreeType refuses -- including a CFF2 whose FDArray count is > 1 but
       * which has no FDSelect operator at all, where FreeType reads the
       * format byte from base_offset + 0 (the CFF2 major version). */
      throw new Error( 'cff.js: invalid FDSelect format ' + fdselect.format );
  }
  return fdselect;
}

/* cff_fd_select_get() */
function cffFdSelectGet( cff, fdselect, glyphIndex ) {
  if ( !fdselect || !fdselect.data ) return 0;
  const d = cff.data;

  switch ( fdselect.format ) {
    case 0:
      return d[ fdselect.data + glyphIndex ];

    case 3: {
      let p = fdselect.data;
      const pLimit = p + fdselect.dataSize;
      let first = u16( d, p ); p += 2;
      do {
        if ( glyphIndex < first ) break;
        const fd2 = d[p++];
        const limit = u16( d, p ); p += 2;
        if ( glyphIndex < limit ) return fd2;
        first = limit;
      } while ( p < pLimit );
      return 0;
    }

    default:
      return 0;
  }
}

/* cffdecode.c cff_lookup_glyph_by_stdcharcode() -- a LINEAR scan of the
 * charset, first match wins.  Returns -1 on failure.                      */
function cffLookupGlyphByStdCharcode( cff, charcode ) {
  if ( !cff.charset || !cff.charset.sids ) return -1;   /* CID: no glyph names */
  if ( charcode < 0 || charcode > 255 )    return -1;

  const glyphSid = CFF_STANDARD_ENCODING[ charcode ];
  for ( let n = 0; n < cff.numGlyphs; n++ )
    if ( cff.charset.sids[n] === glyphSid ) return n;
  return -1;
}


/* cffload.c cff_vstore_load -- we only need regionIdxCount per varData,
 * which is what cf2_doBlend's lenBV is derived from.                     */
function cffVStoreLoad( d, baseOffset, offset ) {
  if ( !offset ) return { dataCount: 0, regionCounts: [] };
  /* the table starts with a uint16 length, then the ItemVariationStore */
  const vs = baseOffset + offset + 2;
  if ( u16( d, vs ) !== 1 ) return { dataCount: 0, regionCounts: [] };
  const dataCount = u16( d, vs + 6 );
  const regionCounts = [];
  for ( let i = 0; i < dataCount; i++ ) {
    const off = u32( d, vs + 8 + i * 4 );
    regionCounts.push( u16( d, vs + off + 4 ) );
  }
  return { dataCount, regionCounts };
}

/* ---- cff_subfont_load / cff_font_load ---- */

function newSubfont() {
  return {
    font_dict: newFontDict(),
    private_dict: newPrivateDict(),
    local_subrs_index: null,
    local_subrs: null,
    random: 0
  };
}

/* cff_load_private_dict() */
function cffLoadPrivateDict( cff, subfont ) {
  const top  = subfont.font_dict;
  const priv = subfont.private_dict;

  if ( !top.private_offset || !top.private_size ) return Err_Ok;

  /* defaults, as set by the C before running the parser */
  priv.blue_shift = 7;
  priv.blue_fuzz  = 1;
  priv.lenIV      = -1;
  priv.expansion_factor = Math.trunc( 0.06 * 0x10000 );
  priv.blue_scale = Math.trunc( 0.039625 * 0x10000 * 1000 );

  const start = cff.baseOffset + top.private_offset;
  const err = cffParserRun( cff, priv, start, start + top.private_size, true,
                            ( cff.cff2 ? ( cff.top_font.font_dict.maxstack || CFF2_DEFAULT_STACK )
                                       : CFF_MAX_STACK_DEPTH ) + 1 );
  if ( err ) return err;

  priv.num_blue_values &= ~1;                /* ensure it is even */

  if ( priv.initial_random_seed < 0 ) priv.initial_random_seed = -priv.initial_random_seed;
  else if ( priv.initial_random_seed === 0 ) priv.initial_random_seed = 987654321;

  if ( priv.blue_shift > 1000 || priv.blue_shift < 0 ) priv.blue_shift = 7;
  if ( priv.blue_fuzz  > 1000 || priv.blue_fuzz  < 0 ) priv.blue_fuzz  = 1;

  return Err_Ok;
}

/* cff_subfont_load() */
function cffSubfontLoad( cff, subfont, idx, fontIndex ) {
  const top = subfont.font_dict;

  /* cffload.c cff_subfont_load: "set default stack size"
   *   top->maxstack = cff2 ? CFF2_DEFAULT_STACK : 48;
   * This is not cosmetic.  maxstack sizes BOTH the Private DICT parser's
   * operand stack and the charstring interpreter's, and CFF2 `blend' runs
   * routinely push far more than 48 operands, so leaving the CFF1 default
   * in place makes CFF2 fonts fail to parse or silently truncate. */
  top.maxstack = cff.cff2 ? CFF2_DEFAULT_STACK : 48;

  let el;
  if ( idx.count ) el = cffIndexAccessElement( cff, idx, fontIndex );
  else el = { start: idx.dataOffset, end: idx.dataOffset + idx.dataSize };
  if ( !el ) return Err_Invalid_File_Format;

  const err = cffParserRun( cff, top, el.start, el.end, false );
  if ( err ) return err;

  /* "if it is a CID font, we stop there" (CFF2 subfonts still load Private) */
  if ( !cff.cff2 && top.cid_registry !== 0xFFFF ) return Err_Ok;

  const e2 = cffLoadPrivateDict( cff, subfont );
  if ( e2 ) return e2;

  /* Random seed.  FreeType derives the driver seed from memory addresses,
   * which we cannot reproduce; `face->internal->random_seed' is -1 by
   * default and the driver seed is non-zero, so `subfont->random' ends up
   * NON-zero and the `initialRandomSeed' fallback is NOT used.  The
   * `random' operator is therefore non-deterministic in real FreeType.
   * We use the documented fallback so our output is at least stable.      */
  subfont.random = subfont.private_dict.initial_random_seed >>> 0;

  if ( subfont.private_dict.local_subrs_offset ) {
    const start = cff.baseOffset + top.private_offset +
                  subfont.private_dict.local_subrs_offset;
    /* CFF2 INDEXes have a 32-bit count; reading the Local Subr INDEX as a
     * CFF1 (16-bit count) INDEX mis-sizes the whole subroutine table, so
     * callsubr lands out of range (Invalid_Glyph_Format) or inside a bogus
     * range (Invalid_Stream_Operation). */
    subfont.local_subrs_index = cffIndexInit( cff.data, start, cff.cff2 );
    subfont.local_subrs = cffIndexGetPointers( cff, subfont.local_subrs_index );
  }
  return Err_Ok;
}

/* cff_font_load(), pure_cff = FALSE (we are always inside an SFNT here). */
function cffFontLoad( face ) {
  const data = face.data;
  /* Prefer `CFF '; fall back to `CFF2'.  FreeType reaches the CFF2 path via
   * cff_face_init with is_cff2 set, and cff_font_load ( ..., cff2 = TRUE ).  */
  let tbl = getTable( face, 'CFF ' );
  let cff2 = false;
  if ( !tbl ) { tbl = getTable( face, 'CFF2' ); cff2 = !!tbl; }
  if ( !tbl ) throw new Error( 'cff.js: no CFF table in this face' );

  const baseOffset = tbl.offset;
  const cff = {
    data, baseOffset, streamSize: data.length,
    version_major: data[baseOffset], version_minor: data[baseOffset + 1],
    header_size: data[baseOffset + 2],
    charset: null, encoding: null, fd_select: null,
    subfonts: [], num_subfonts: 0,
    top_font: newSubfont(), numGlyphs: 0,
    cff2
  };

  let p;
  if ( cff2 ) {
    if ( cff.version_major !== 2 || cff.header_size < 5 )
      throw new Error( 'cff.js: not a CFF2 font header' );
    cff.top_dict_length = u16( data, baseOffset + 3 );

    /* "For CFF2, the top dict data immediately follow the header and the
     * length is stored in the header; there is no index for it."  FreeType
     * fakes an index with count == 0 to carry offset+size.               */
    cff.font_dict_index = { start: 0, hdrSize: 5, count: 0, offSize: 0,
                            dataOffset: baseOffset + cff.header_size,
                            dataSize: cff.top_dict_length, end: 0, ok: true };
    p = baseOffset + cff.header_size + cff.top_dict_length;
    cff.global_subrs_index = cffIndexInit( data, p, true );
    cff.name_index = { count: 0 };
    cff.string_index = { count: 0 };
    cff.num_strings = 0;
  } else {
    const absoluteOffset = data[baseOffset + 3];
    if ( cff.version_major !== 1 || cff.header_size < 4 || absoluteOffset > 4 )
      throw new Error( 'cff.js: not a CFF font header' );

    p = baseOffset + cff.header_size;
    cff.name_index         = cffIndexInit( data, p, false ); p = cff.name_index.end;
    cff.font_dict_index    = cffIndexInit( data, p, false ); p = cff.font_dict_index.end;
    cff.string_index       = cffIndexInit( data, p, false ); p = cff.string_index.end;
    cff.global_subrs_index = cffIndexInit( data, p, false );

    cff.num_strings = cff.string_index.count;

    if ( cff.name_index.count > 1 )
      throw new Error( 'cff.js: invalid CFF font with multiple subfonts in SFNT wrapper' );
  }

  const err = cffSubfontLoad( cff, cff.top_font, cff.font_dict_index, 0 );
  if ( err ) throw new Error( 'cff.js: cannot parse top DICT (err ' + err + ')' );

  const dict = cff.top_font.font_dict;

  if ( !dict.charstrings_offset )
    throw new Error( 'cff.js: no charstrings offset' );

  cff.charstrings_index = cffIndexInit( data, baseOffset + dict.charstrings_offset, cff2 );

  /* CID-keyed font -- or any CFF2, which always has an FDArray */
  if ( dict.cid_registry !== 0xFFFF || cff2 ) {
    if ( cff2 ) cff.vstore = cffVStoreLoad( data, baseOffset, dict.vstore_offset );
    if ( !dict.cid_fd_array_offset )
      throw new Error( 'cff.js: CFF2 without an FDArray' );
    const fdIdx = cffIndexInit( data, baseOffset + dict.cid_fd_array_offset, cff2 );
    if ( fdIdx.count > 256 )
      throw new Error( 'cff.js: FD array too large in CID font' );

    cff.num_subfonts = fdIdx.count;
    for ( let i = 0; i < fdIdx.count; i++ ) {
      const sub = newSubfont();
      cff.subfonts.push( sub );
      const e = cffSubfontLoad( cff, sub, fdIdx, i );
      if ( e ) throw new Error( 'cff.js: cannot parse FDArray subfont ' + i );
    }
    /* CFF2 omits FDSelect when there is only one FD */
    if ( !cff2 || fdIdx.count > 1 )
      cff.fd_select = cffLoadFdSelect( cff, cff.charstrings_index.count,
                                       baseOffset + dict.cid_fd_select_offset );
  }

  cff.numGlyphs = cff.charstrings_index.count;
  cff.global_subrs = cffIndexGetPointers( cff, cff.global_subrs_index );

  /* Charset / Encoding.  `invert' is TRUE only for pure CFF fonts, so for
   * an SFNT-wrapped CID font charset.cids stays NULL and cff_slot_load
   * does NOT remap the glyph index -- see loadCffOutline().               */
  if ( !cff2 && cff.numGlyphs > 0 ) {
    const invert = false;                 /* pure_cff is always FALSE here */
    const r = cffCharsetLoad( cff, cff.numGlyphs, baseOffset,
                              dict.charset_offset, invert );
    if ( r.err ) throw new Error( 'cff.js: cannot load charset' );
    cff.charset = r.charset;
  }

  /* ---- cffobjs.c: units_per_em and the font matrix ---- */
  cffNormalizeMatrices( cff, face );

  return cff;
}

/* cff_face_init()'s font-matrix normalisation (cffobjs.c lines ~708-816).
 * "Normalize the font matrix so that `matrix->yy' is 1 ... The scaling is
 * done with `units_per_em' then."                                         */
function cffNormalizeMatrices( cff, face ) {
  const dict = cff.top_font.font_dict;

  if ( !dict.has_font_matrix )
    dict.units_per_em = face.unitsPerEM;        /* pure_cff ? 1000 : face upem */

  normalizeOne( dict );

  for ( let i = cff.num_subfonts; i > 0; i-- ) {
    const sub = cff.subfonts[i - 1].font_dict;
    const top = cff.top_font.font_dict;

    if ( sub.has_font_matrix ) {
      if ( top.has_font_matrix ) {
        const scaling = ( top.units_per_em > 1 && sub.units_per_em > 1 )
                          ? Math.min( top.units_per_em, sub.units_per_em ) : 1;
        /* NOTE: the C multiplies INTO sub->font_matrix (b == &sub->...)
         * with top->font_matrix as `a'.                                   */
        ftMatrixMultiplyScaled( top.font_matrix, sub.font_matrix, scaling );
        ftVectorTransformScaled( sub.font_offset, top.font_matrix, scaling );
        sub.units_per_em = FT_MulDiv( sub.units_per_em, top.units_per_em, scaling );
      }
    } else {
      sub.font_matrix  = Object.assign( {}, top.font_matrix );
      sub.font_offset  = Object.assign( {}, top.font_offset );
      sub.units_per_em = top.units_per_em;
    }
    normalizeOne( sub );
  }
}

function normalizeOne( dict ) {
  const m = dict.font_matrix, o = dict.font_offset;
  const temp = m.yy ? Math.abs( m.yy ) : Math.abs( m.yx );

  if ( temp !== 0x10000 ) {
    dict.units_per_em = FT_DivFix( dict.units_per_em, temp );
    m.xx = FT_DivFix( m.xx, temp );
    m.yx = FT_DivFix( m.yx, temp );
    m.xy = FT_DivFix( m.xy, temp );
    m.yy = FT_DivFix( m.yy, temp );
    o.x  = FT_DivFix( o.x,  temp );
    o.y  = FT_DivFix( o.y,  temp );
  }
  /* "the offsets must be expressed in integer font units" */
  o.x = Math.floor( o.x / 65536 );        /* arithmetic >> 16 */
  o.y = Math.floor( o.y / 65536 );
}

function getTable( face, tag ) {
  const t = face.tables;
  if ( !t ) return null;
  if ( typeof t.get === 'function' ) return t.get( tag ) || null;
  return t[tag] || null;
}


/* ------------------------------------------------------------------ */
/* S3  PS_Builder -- psobjs.c                                          */
/* ------------------------------------------------------------------ */

function newBuilder() {
  return {
    xs: [], ys: [], tags: [], contours: [],
    nPoints: 0, nContours: 0,
    pathBegun: 0,          /* cff_builder_init: builder->path_begun  = 0 */
    loadPoints: 1          /* cff_builder_init: builder->load_points = 1 */
  };
}

/* ps_builder_add_point().  The Adobe engine is the default hinting engine,
 * so we always take the `x >> 10' branch:
 *
 *     /  cf2_decoder_parse_charstrings uses 16.16 coordinates  /
 *     point->x = x >> 10;
 *
 * x is an FT_Pos (64-bit signed), so this is an arithmetic shift.         */
function psBuilderAddPoint( b, x, y, flag ) {
  if ( b.loadPoints ) {
    const n = b.nPoints;
    b.xs[n]   = Math.floor( x / 1024 );      /* arithmetic >> 10 */
    b.ys[n]   = Math.floor( y / 1024 );
    b.tags[n] = flag ? FT_CURVE_TAG_ON : FT_CURVE_TAG_CUBIC;
  }
  b.nPoints++;
}

function psBuilderAddPoint1( b, x, y ) { psBuilderAddPoint( b, x, y, 1 ); }

function psBuilderAddContour( b ) {
  if ( !b.loadPoints ) { b.nContours++; return; }
  if ( b.nContours > 0 ) b.contours[ b.nContours - 1 ] = b.nPoints - 1;
  b.nContours++;
}

function psBuilderStartPoint( b, x, y ) {
  if ( !b.pathBegun ) {
    b.pathBegun = 1;
    psBuilderAddContour( b );
    psBuilderAddPoint1( b, x, y );
  }
}

/* ps_builder_close_contour() */
function psBuilderCloseContour( b ) {
  const first = b.nContours <= 1 ? 0 : b.contours[ b.nContours - 2 ] + 1;

  /* "in malformed fonts it can happen that a contour was started but no
   *  points were added"                                                  */
  if ( b.nContours && first === b.nPoints ) { b.nContours--; return; }

  /* "We must not include the last point in the path if it is located on
   *  the first point" -- and only if it is not a control point.          */
  if ( b.nPoints > 1 ) {
    const i1 = first, i2 = b.nPoints - 1;
    if ( b.xs[i1] === b.xs[i2] && b.ys[i1] === b.ys[i2] &&
         b.tags[i2] === FT_CURVE_TAG_ON )
      b.nPoints--;
  }

  if ( b.nContours > 0 ) {
    /* "Don't add contours only consisting of one point" */
    if ( first === b.nPoints - 1 ) { b.nContours--; b.nPoints--; }
    else b.contours[ b.nContours - 1 ] = b.nPoints - 1;
  }
}


/* ---- psft.c outline callbacks ---- */

function cf2BuilderMoveTo( ctx ) {
  /* "note: two successive moves simply close the contour twice" */
  psBuilderCloseContour( ctx.builder );
  ctx.builder.pathBegun = 0;
}

function cf2BuilderLineTo( ctx, p0x, p0y, p1x, p1y ) {
  const b = ctx.builder;
  if ( !b.pathBegun ) psBuilderStartPoint( b, p0x, p0y );
  psBuilderAddPoint1( b, p1x, p1y );
}

function cf2BuilderCubeTo( ctx, p0x, p0y, p1x, p1y, p2x, p2y, p3x, p3y ) {
  const b = ctx.builder;
  if ( !b.pathBegun ) psBuilderStartPoint( b, p0x, p0y );
  psBuilderAddPoint( b, p1x, p1y, 0 );
  psBuilderAddPoint( b, p2x, p2y, 0 );
  psBuilderAddPoint( b, p3x, p3y, 1 );
}


/* ------------------------------------------------------------------ */
/* S4  CF2_GlyphPath -- pshints.c                                      */
/*                                                                     */
/*  This is the UNHINTED, UNDARKENED path.  `ftlint -f 2' is           */
/*  FT_LOAD_NO_HINTING, and the CFF driver defaults to                  */
/*  no_stem_darkening = TRUE (cffobjs.c cff_driver_init) with           */
/*  face->internal->no_stem_darkening = -1, so neither CF2_FlagsHinted  */
/*  nor CF2_FlagsDarkened is set.  Consequences, all verified against   */
/*  the C:                                                             */
/*                                                                     */
/*   - cf2_glyphpath_computeOffset() returns (0,0) immediately, so      */
/*     every P0/P1/P2/P3 equals the plain character-space point and     */
/*     the miter/intersection machinery is never reached                */
/*     (prevP1 == nextP0 always).                                       */
/*   - cf2_hintmap_map() returns FT_MulFix( csCoord, scale ) because    */
/*     hintmap->hinted is FALSE, whatever the map contains.             */
/*   - outerTransform is the identity and fractionalTranslation is 0.   */
/*                                                                     */
/*  What is NOT skipped is the hint-mask bookkeeping: cf2_hintmap_build */
/*  still runs and still sets hintMap.isValid and clears               */
/*  hintMask.isNew, and that feeds back into the interpreter's          */
/*  "never add hints after the mask is computed" tests.                */
/* ------------------------------------------------------------------ */

const OP_LINE = 1, OP_CUBE = 2;

function newGlyphPath( ctx, scale ) {
  return {
    ctx,
    scaleX: scale, scaleC: 0, scaleY: scale,
    hintMapIsValid: false,
    initialHintMapIsValid: false,
    currentCSx: 0, currentCSy: 0,
    startx: 0, starty: 0,
    currentDSx: 0, currentDSy: 0,
    offsetStart0x: 0, offsetStart0y: 0,
    offsetStart1x: 0, offsetStart1y: 0,
    prevElemOp: 0,
    prevElemP0x: 0, prevElemP0y: 0, prevElemP1x: 0, prevElemP1y: 0,
    prevElemP2x: 0, prevElemP2y: 0, prevElemP3x: 0, prevElemP3y: 0,
    moveIsPending: true, pathIsOpen: false, pathIsClosing: false,
    elemIsQueued: false
  };
}

/* cf2_glyphpath_hintPoint(), unhinted:
 *   pt.x = FT_MulFix( scaleX, x ) + FT_MulFix( scaleC, y )      [scaleC = 0]
 *   pt.y = cf2_hintmap_map( hintmap, y ) = FT_MulFix( y, scale )
 *   then outerTransform (identity) and fractionalTranslation (0).         */
function hintPointX( gp, x ) { return FT_MulFix( gp.scaleX, x ); }
function hintPointY( gp, y ) { return FT_MulFix( y, gp.scaleY ); }

function newHintMask() {
  return { mask: new Uint8Array( ( CF2_MAX_HINTS + 7 ) >> 3 ),
           bitCount: 0, byteCount: 0, isValid: false, isNew: false };
}

/* cf2_hintmask_setCounts() -- returns bitCount, 0 meaning "did nothing". */
function cf2HintmaskSetCounts( ctx, hm, bitCount ) {
  if ( bitCount > CF2_MAX_HINTS ) {
    setError( ctx, Err_Invalid_Glyph_Format );
    return 0;
  }
  hm.bitCount  = bitCount;
  hm.byteCount = ( bitCount + 7 ) >> 3;
  hm.isValid   = true;
  hm.isNew     = true;
  return bitCount;
}

function cf2HintmaskSetAll( ctx, hm, bitCount ) {
  const mask = ( 1 << ( -bitCount & 7 ) ) - 1;
  if ( cf2HintmaskSetCounts( ctx, hm, bitCount ) === 0 ) return;
  for ( let i = 0; i < hm.byteCount; i++ ) hm.mask[i] = 0xFF;
  hm.mask[ hm.byteCount - 1 ] &= ~mask;
}

/* cf2_hintmask_read() -- consumes the mask bytes from the charstring.
 * NOTE both early returns: > 96 bits is an error AND consumes nothing,
 * and a bitCount of exactly 0 also consumes nothing (byteCount == 0).     */
function cf2HintmaskRead( ctx, hm, cs, bitCount ) {
  if ( cf2HintmaskSetCounts( ctx, hm, bitCount ) === 0 ) return;
  for ( let i = 0; i < hm.byteCount; i++ )
    hm.mask[i] = cf2BufReadByte( ctx, cs ) & 0xFF;
}

/* cf2_hintmap_build(), reduced to its observable effects when hinting is
 * off: it validates the hint mask (implicitly turning on every stem when
 * no hintmask operator was seen), marks the map valid and clears `isNew'.
 * The edge list it builds cannot influence an unhinted outline because
 * cf2_hintmap_map() ignores it.                                          */
function cf2HintmapBuild( gp, initialMap ) {
  const ctx = gp.ctx;
  const hm = ctx.hintMask;

  if ( !initialMap && !gp.initialHintMapIsValid ) {
    /* recursive build of the initial map with a fresh temporary mask */
    const temp = newHintMask();
    cf2HintmaskSetAll( ctx, temp, ctx.hStemCount + ctx.vStemCount );
    if ( temp.isValid ) gp.initialHintMapIsValid = true;
  }

  if ( !hm.isValid ) {
    cf2HintmaskSetAll( ctx, hm, ctx.hStemCount + ctx.vStemCount );
    if ( !hm.isValid ) return;                   /* too many stem hints */
  }

  /* "Defense-in-depth.  Should never return here." */
  if ( ctx.hStemCount > hm.bitCount ) return;

  gp.hintMapIsValid = true;
  hm.isNew = false;                              /* cf2_hintmask_setNew */
}

/* cf2_glyphpath_pushMove() */
function glyphPathPushMove( gp, p0x, p0y ) {
  /* "Test if move has really happened yet" */
  if ( !gp.hintMapIsValid )
    glyphPathMoveTo( gp, gp.startx, gp.starty );

  const x = hintPointX( gp, p0x ), y = hintPointY( gp, p0y );
  cf2BuilderMoveTo( gp.ctx );

  gp.currentDSx = x;      gp.currentDSy = y;
  gp.offsetStart0x = p0x; gp.offsetStart0y = p0y;
}

/* cf2_glyphpath_pushPrevElem(), with darkening off.  `useIntersection' can
 * never become TRUE because prevP1 always equals nextP0 with a zero
 * offset, so the whole miter path collapses to "emit the queued element".  */
function glyphPathPushPrevElem( gp, nextP0x, nextP0y, close ) {
  const ctx = gp.ctx;
  const pt0x = gp.currentDSx, pt0y = gp.currentDSy;

  if ( gp.prevElemOp === OP_LINE ) {
    const p1x = hintPointX( gp, gp.prevElemP1x );
    const p1y = hintPointY( gp, gp.prevElemP1y );

    /* "output only non-zero length lines" -- the test is in DEVICE space */
    if ( pt0x !== p1x || pt0y !== p1y ) {
      cf2BuilderLineTo( ctx, pt0x, pt0y, p1x, p1y );
      gp.currentDSx = p1x; gp.currentDSy = p1y;
    }
  } else {
    const p1x = hintPointX( gp, gp.prevElemP1x ), p1y = hintPointY( gp, gp.prevElemP1y );
    const p2x = hintPointX( gp, gp.prevElemP2x ), p2y = hintPointY( gp, gp.prevElemP2y );
    const p3x = hintPointX( gp, gp.prevElemP3x ), p3y = hintPointY( gp, gp.prevElemP3y );

    cf2BuilderCubeTo( ctx, pt0x, pt0y, p1x, p1y, p2x, p2y, p3x, p3y );
    gp.currentDSx = p3x; gp.currentDSy = p3y;
  }

  /* !useIntersection is always TRUE here: insert a connecting line if the
   * next element does not start where this one ended.                     */
  const nx = hintPointX( gp, nextP0x ), ny = hintPointY( gp, nextP0y );
  if ( nx !== gp.currentDSx || ny !== gp.currentDSy ) {
    cf2BuilderLineTo( ctx, gp.currentDSx, gp.currentDSy, nx, ny );
    gp.currentDSx = nx; gp.currentDSy = ny;
  }
}

/* cf2_glyphpath_moveTo() */
function glyphPathMoveTo( gp, x, y ) {
  glyphPathCloseOpenPath( gp );

  gp.currentCSx = gp.startx = x;
  gp.currentCSy = gp.starty = y;
  gp.moveIsPending = true;

  if ( !gp.hintMapIsValid || gp.ctx.hintMask.isNew )
    cf2HintmapBuild( gp, false );
  /* firstHintMap = hintMap (structure copy) -- irrelevant when unhinted */
}

/* cf2_glyphpath_lineTo() */
function glyphPathLineTo( gp, x, y ) {
  const newHintMap = gp.ctx.hintMask.isNew && !gp.pathIsClosing;

  /* "Ignore zero-length lines in CS where the hint map is the same" */
  if ( gp.currentCSx === x && gp.currentCSy === y && !newHintMap ) return;

  const P0x = gp.currentCSx, P0y = gp.currentCSy;    /* zero darkening offset */
  const P1x = x,             P1y = y;

  if ( gp.moveIsPending ) {
    glyphPathPushMove( gp, P0x, P0y );
    gp.moveIsPending = false;
    gp.pathIsOpen = true;
    gp.offsetStart1x = P1x; gp.offsetStart1y = P1y;
  }

  if ( gp.elemIsQueued )
    glyphPathPushPrevElem( gp, P0x, P0y, false );

  gp.elemIsQueued = true;
  gp.prevElemOp = OP_LINE;
  gp.prevElemP0x = P0x; gp.prevElemP0y = P0y;
  gp.prevElemP1x = P1x; gp.prevElemP1y = P1y;

  if ( newHintMap ) cf2HintmapBuild( gp, false );

  gp.currentCSx = x; gp.currentCSy = y;
}

/* cf2_glyphpath_curveTo() */
function glyphPathCurveTo( gp, x1, y1, x2, y2, x3, y3 ) {
  const P0x = gp.currentCSx, P0y = gp.currentCSy;

  if ( gp.moveIsPending ) {
    glyphPathPushMove( gp, P0x, P0y );
    gp.moveIsPending = false;
    gp.pathIsOpen = true;
    gp.offsetStart1x = x1; gp.offsetStart1y = y1;
  }

  if ( gp.elemIsQueued )
    glyphPathPushPrevElem( gp, P0x, P0y, false );

  gp.elemIsQueued = true;
  gp.prevElemOp = OP_CUBE;
  gp.prevElemP0x = P0x; gp.prevElemP0y = P0y;
  gp.prevElemP1x = x1;  gp.prevElemP1y = y1;
  gp.prevElemP2x = x2;  gp.prevElemP2y = y2;
  gp.prevElemP3x = x3;  gp.prevElemP3y = y3;

  if ( gp.ctx.hintMask.isNew ) cf2HintmapBuild( gp, false );

  gp.currentCSx = x3; gp.currentCSy = y3;
}

/* cf2_glyphpath_closeOpenPath() */
function glyphPathCloseOpenPath( gp ) {
  if ( !gp.pathIsOpen ) return;

  gp.pathIsClosing = true;
  glyphPathLineTo( gp, gp.startx, gp.starty );

  if ( gp.elemIsQueued )
    glyphPathPushPrevElem( gp, gp.offsetStart0x, gp.offsetStart0y, true );

  gp.moveIsPending = true;
  gp.pathIsOpen    = false;
  gp.pathIsClosing = false;
  gp.elemIsQueued  = false;
}


/* ------------------------------------------------------------------ */
/* S5  the Type 2 charstring interpreter -- psintrp.c                  */
/* ------------------------------------------------------------------ */

/* CF2_StackNumber types (psfixed.h enum order) */
const NUM_FIXED = 0, NUM_FRAC = 1, NUM_INT = 2;

function newStack( size ) {
  const n = size || CF2_OPERAND_STACK_SIZE;
  return { v: new Array( n ).fill( 0 ), t: new Array( n ).fill( NUM_INT ),
           top: 0, size: n };
}

function stackCount( s )  { return s.top; }
function stackClear( s )  { s.top = 0; }

function stackPushInt( ctx, s, val ) {
  if ( s.top === s.size ) { setError( ctx, Err_Stack_Overflow ); return; }
  s.v[s.top] = val | 0; s.t[s.top] = NUM_INT; s.top++;
}
function stackPushFixed( ctx, s, val ) {
  if ( s.top === s.size ) { setError( ctx, Err_Stack_Overflow ); return; }
  s.v[s.top] = val | 0; s.t[s.top] = NUM_FIXED; s.top++;
}
/* "this function is only allowed to pop an integer type" */
function stackPopInt( ctx, s ) {
  if ( s.top === 0 ) { setError( ctx, Err_Stack_Underflow ); return 0; }
  if ( s.t[s.top - 1] !== NUM_INT ) { setError( ctx, Err_Syntax_Error ); return 0; }
  s.top--;
  return s.v[s.top];
}
function stackPopFixed( ctx, s ) {
  if ( s.top === 0 ) { setError( ctx, Err_Stack_Underflow ); return cf2_intToFixed( 0 ); }
  s.top--;
  switch ( s.t[s.top] ) {
    case NUM_INT:  return cf2_intToFixed( s.v[s.top] );
    case NUM_FRAC: return cf2_fracToFixed( s.v[s.top] );
    default:       return s.v[s.top];
  }
}
function stackGetReal( ctx, s, idx ) {
  if ( idx >= s.top ) { setError( ctx, Err_Stack_Overflow ); return cf2_intToFixed( 0 ); }
  switch ( s.t[idx] ) {
    case NUM_INT:  return cf2_intToFixed( s.v[idx] );
    case NUM_FRAC: return cf2_fracToFixed( s.v[idx] );
    default:       return s.v[idx];
  }
}
function stackSetReal( ctx, s, idx, val ) {
  if ( idx > s.top ) { setError( ctx, Err_Stack_Overflow ); return; }
  s.v[idx] = val | 0; s.t[idx] = NUM_FIXED;
}
function stackPop( ctx, s, num ) {
  if ( num > s.top ) { setError( ctx, Err_Stack_Underflow ); return; }
  s.top -= num;
}
/* cf2_stack_roll() */
function stackRoll( ctx, s, count, shift ) {
  if ( count < 2 ) return;
  if ( count > s.top ) { setError( ctx, Err_Stack_Overflow ); return; }

  /* C99 `%' truncates toward zero; mirror the explicit sign handling */
  if ( shift < 0 ) shift = -( ( -shift ) % count );
  else shift %= count;
  if ( shift === 0 ) return;

  let lastV = 0, lastT = NUM_INT;
  let startIdx = -1, idx = -1;
  for ( let i = 0; i < count; i++ ) {
    if ( startIdx === idx ) {
      startIdx++; idx = startIdx;
      lastV = s.v[idx]; lastT = s.t[idx];
    }
    idx += shift;
    if ( idx >= count ) idx -= count;
    else if ( idx < 0 ) idx += count;

    const tmpV = s.v[idx], tmpT = s.t[idx];
    s.v[idx] = lastV; s.t[idx] = lastT;
    lastV = tmpV; lastT = tmpT;
  }
}

/* psread.c: "reading past the end of the buffer sets error and returns zero" */
function cf2BufReadByte( ctx, buf ) {
  if ( buf.ptr < buf.end ) return ctx.data[ buf.ptr++ ];
  /* psread.c reports this through `buf->error':
   *     CF2_SET_ERROR( buf->error, Invalid_Stream_Operation );
   * but cf2_setError is `if ( error && !*error ) *error = value', and the
   * CFF path FT_ZEROs every CF2_BufferRec (psft.c cf2_decoder_parse_charstrings
   * and cf2_init{Local,Global}RegionBuffer), so buf->error is NULL and the
   * call does nothing.  Reading past the end therefore returns 0 silently and
   * does NOT advance ptr, so the next loop iteration sees cf2_buf_isEnd and
   * synthesizes ENDCHAR.  Raising an error here instead rejects charstrings
   * that FreeType renders -- e.g. one ending in a truncated 2-byte operand. */
  return 0;
}
function cf2BufIsEnd( buf ) { return buf.ptr >= buf.end; }

/* psft.c cf2_initLocalRegionBuffer / cf2_initGlobalRegionBuffer.
 * Returns TRUE (an error) when the biased index is out of range.          */
function initRegionBuffer( subrs, numSubrs, bias, subrNum, buf ) {
  /* C: idx = (CF2_UInt)( subrNum + bias ); if ( idx >= num ) return TRUE;
   * The cast to unsigned makes a negative index wrap to a huge value, so
   * the single >= test catches both ends.                                 */
  const idx = ( subrNum + bias ) >>> 0;
  if ( !subrs || idx >= numSubrs ) return true;
  buf.start = buf.ptr = subrs[idx];
  buf.end   = subrs[idx + 1];
  return false;
}

/* psintrp.c cf2_doStems() */
function cf2DoStems( ctx, s, isVertical ) {
  const count = stackCount( s );
  const hasWidthArg = ( count & 1 ) !== 0;
  let position = 0;                              /* hintOffset is 0 for CFF */

  if ( hasWidthArg && !ctx.haveWidth )
    ctx.width = ADD_INT32( stackGetReal( ctx, s, 0 ), ctx.nominalWidthX );

  if ( ctx.widthOnly ) { ctx.haveWidth = true; return; }

  const arr = isVertical ? ctx.vStems : ctx.hStems;
  for ( let i = hasWidthArg ? 1 : 0; i < count; i += 2 ) {
    const min = position = ADD_INT32( position, stackGetReal( ctx, s, i ) );
    const max = position = ADD_INT32( position, stackGetReal( ctx, s, i + 1 ) );
    arr.push( min, max );
  }
  if ( isVertical ) ctx.vStemCount = ctx.vStems.length >> 1;
  else              ctx.hStemCount = ctx.hStems.length >> 1;

  stackClear( s );
  ctx.haveWidth = true;      /* "cf2_doStems must define a width" */
}

/* psintrp.c cf2_doFlex() */
function cf2DoFlex( ctx, s, gp, readFromStack, doConditionalLastRead ) {
  const vals = new Array( 14 ).fill( 0 );
  let idx = 0;

  vals[0] = ctx.curX;
  vals[1] = ctx.curY;

  const isHFlex = readFromStack[9] === false;
  const top = isHFlex ? 9 : 10;

  for ( let i = 0; i < top; i++ ) {
    vals[i + 2] = vals[i];
    if ( readFromStack[i] )
      vals[i + 2] = ADD_INT32( vals[i + 2], stackGetReal( ctx, s, idx++ ) );
  }

  if ( isHFlex ) vals[11] = ctx.curY;

  if ( doConditionalLastRead ) {
    const lastIsX = cf2_fixedAbs( SUB_INT32( vals[10], ctx.curX ) ) >
                    cf2_fixedAbs( SUB_INT32( vals[11], ctx.curY ) );
    const lastVal = stackGetReal( ctx, s, idx );
    if ( lastIsX ) { vals[12] = ADD_INT32( vals[10], lastVal ); vals[13] = ctx.curY; }
    else           { vals[12] = ctx.curX; vals[13] = ADD_INT32( vals[11], lastVal ); }
  } else {
    vals[12] = readFromStack[10] ? ADD_INT32( vals[10], stackGetReal( ctx, s, idx++ ) )
                                 : ctx.curX;
    vals[13] = readFromStack[11] ? ADD_INT32( vals[11], stackGetReal( ctx, s, idx ) )
                                 : ctx.curY;
  }

  for ( let j = 0; j < 2; j++ )
    glyphPathCurveTo( gp, vals[j * 6 + 2], vals[j * 6 + 3],
                          vals[j * 6 + 4], vals[j * 6 + 5],
                          vals[j * 6 + 6], vals[j * 6 + 7] );

  stackClear( s );
  ctx.curX = vals[12];
  ctx.curY = vals[13];
}

/* psintrp.c cf2_doBlend.  With no variation coordinates FreeType builds the
 * default blend vector (1, 0, 0, ...) -- cff_blend_build_vector: "lenNDV == 0
 * produces a default blend vector" -- so every weight is zero and each
 * blended value collapses to its base operand.  We still must consume
 * numBlends * lenBV operands and leave numBlends, so lenBV (hence the
 * ItemVariationStore's regionIdxCount) has to be parsed regardless.      */
function cf2DoBlend( ctx, s, numBlends ) {
  const lenBV = ctx.lenBV;
  const numOperands = numBlends * lenBV;
  const base = stackCount( s ) - numOperands;
  let delta = base + numBlends;

  for ( let i = 0; i < numBlends; i++ ) {
    let sum = stackGetReal( ctx, s, i + base );
    for ( let j = 1; j < lenBV; j++ )
      sum = ADD_INT32( sum, FT_MulFix( 0 /* BV[j] is 0 */,
                                       stackGetReal( ctx, s, delta++ ) ) );
    stackSetReal( ctx, s, i + base, sum );
  }
  stackPop( ctx, s, numOperands - numBlends );
}

const RFS_HFLEX  = [ true,false, true,true,  true,false, true,false, true,false, true,false ];
const RFS_FLEX   = [ true,true,  true,true,  true,true,  true,true,  true,true,  true,true  ];
const RFS_HFLEX1 = [ true,true,  true,true,  true,false, true,false, true,true,  true,false ];
const RFS_FLEX1  = [ true,true,  true,true,  true,true,  true,true,  true,true,  false,false ];


/*
 * cf2_interpT2CharString().  `doingSeac' and the initial current point are
 * the recursion parameters used by the deprecated 4-argument endchar form.
 * Returns the glyph width (16.16).
 */
function cf2InterpT2CharString( ctx, buf, gp, doingSeac, curX, curY ) {
  const s = ctx.stack;
  const nominalWidthX = ctx.nominalWidthX;

  const storage = new Int32Array( CF2_STORAGE_SIZE );

  let instructionLimit = 20000000;

  /* the subroutine stack; entry 0 is the top-level charstring */
  const subrStack = new Array( CF2_MAX_SUBR + 2 );
  for ( let i = 0; i < subrStack.length; i++ )
    subrStack[i] = { start: 0, ptr: 0, end: 0 };

  let charstringIndex = 0;
  let charstring = subrStack[0];
  charstring.start = buf.start; charstring.ptr = buf.ptr; charstring.end = buf.end;

  ctx.curX = curX;
  ctx.curY = curY;

  /* width state; `haveWidth' and `width' live on ctx because cf2_doStems
   * mutates them through pointers in the C.                               */
  const savedHaveWidth = ctx.haveWidth, savedWidth = ctx.width;
  /* "CFF2 charstrings always return the default width (0)." */
  ctx.haveWidth = ctx.isCFF2 ? true : false;
  ctx.width = ctx.defaultWidthX;

  let op1 = 0;

  exitLbl:
  {
    mainLoop:
    for (;;) {
      if ( cf2BufIsEnd( charstring ) ) {
        /* simulate a RETURN (in a subroutine) or an ENDCHAR (at top level) */
        op1 = charstringIndex ? 11 : 14;
      } else {
        op1 = cf2BufReadByte( ctx, charstring );

        /* "Explicit RETURN and ENDCHAR in CFF2 should be ignored." */
        if ( ( op1 === 11 || op1 === 14 ) && ctx.isCFF2 )
          op1 = 0;
      }

      /* "check for errors once per loop" */
      if ( ctx.error ) break exitLbl;

      if ( _opCensus ) census( op1 < 32 ? String( op1 ) : 'num' );

      instructionLimit--;
      if ( instructionLimit === 0 ) { setError( ctx, Err_Invalid_Glyph_Format ); break exitLbl; }

      switch ( op1 ) {

        case 0:  case 2:  case 17:                       /* reserved */
          break;

        case 15: {                                       /* vsindex */
          if ( !ctx.isCFF2 ) break;                      /* clear stack & ignore */
          if ( ctx.usedBV ) { setError( ctx, Err_Invalid_Glyph_Format ); break exitLbl; }
          const temp = stackPopInt( ctx, s );
          if ( temp >= 0 ) { ctx.vsindex = temp; ctx.lenBV = 0; }
          break;
        }

        case 16: {                                       /* blend */
          if ( !ctx.isCFF2 ) break;                      /* clear stack & ignore */
          /* "do we have a `blend' op in a non-variant font?" */
          if ( !ctx.vstore || ctx.vstore.dataCount === 0 ) {
            setError( ctx, Err_Invalid_Glyph_Format ); break exitLbl;
          }
          if ( !ctx.lenBV ) {
            if ( ctx.vsindex >= ctx.vstore.dataCount ) {
              setError( ctx, Err_Invalid_File_Format ); break exitLbl;
            }
            ctx.lenBV = ctx.vstore.regionCounts[ ctx.vsindex ] + 1;
          }
          const numBlends = stackPopInt( ctx, s ) >>> 0;
          if ( numBlends > s.size ) { setError( ctx, Err_Invalid_Glyph_Format ); break exitLbl; }
          cf2DoBlend( ctx, s, numBlends );
          ctx.usedBV = true;
          continue mainLoop;                             /* do not clear the stack */
        }

        case 18:                                         /* hstemhm */
        case 1:                                          /* hstem   */
          /* "never add hints after the mask is computed" */
          if ( ctx.hintMask.isValid ) break;
          cf2DoStems( ctx, s, false );
          if ( ctx.widthOnly ) break exitLbl;
          break;

        case 23:                                         /* vstemhm */
        case 3:                                          /* vstem   */
          if ( ctx.hintMask.isValid ) break;
          cf2DoStems( ctx, s, true );
          if ( ctx.widthOnly ) break exitLbl;
          break;

        case 4: {                                        /* vmoveto */
          if ( stackCount( s ) > 1 && !ctx.haveWidth )
            ctx.width = ADD_INT32( stackGetReal( ctx, s, 0 ), nominalWidthX );
          ctx.haveWidth = true;
          if ( ctx.widthOnly ) break exitLbl;

          ctx.curY = ADD_INT32( ctx.curY, stackPopFixed( ctx, s ) );
          glyphPathMoveTo( gp, ctx.curX, ctx.curY );
          break;
        }

        case 5: {                                        /* rlineto */
          const count = stackCount( s );
          for ( let idx = 0; idx < count; idx += 2 ) {
            ctx.curX = ADD_INT32( ctx.curX, stackGetReal( ctx, s, idx + 0 ) );
            ctx.curY = ADD_INT32( ctx.curY, stackGetReal( ctx, s, idx + 1 ) );
            glyphPathLineTo( gp, ctx.curX, ctx.curY );
          }
          stackClear( s );
          continue mainLoop;
        }

        case 6:                                          /* hlineto */
        case 7: {                                        /* vlineto */
          const count = stackCount( s );
          let isX = ( op1 === 6 );
          for ( let idx = 0; idx < count; idx++ ) {
            const v = stackGetReal( ctx, s, idx );
            if ( isX ) ctx.curX = ADD_INT32( ctx.curX, v );
            else       ctx.curY = ADD_INT32( ctx.curY, v );
            isX = !isX;
            glyphPathLineTo( gp, ctx.curX, ctx.curY );
          }
          stackClear( s );
          continue mainLoop;
        }

        case 24:                                         /* rcurveline */
        case 8: {                                        /* rrcurveto  */
          const count = stackCount( s );
          let idx = 0;
          while ( idx + 6 <= count ) {
            const x1 = ADD_INT32( stackGetReal( ctx, s, idx + 0 ), ctx.curX );
            const y1 = ADD_INT32( stackGetReal( ctx, s, idx + 1 ), ctx.curY );
            const x2 = ADD_INT32( stackGetReal( ctx, s, idx + 2 ), x1 );
            const y2 = ADD_INT32( stackGetReal( ctx, s, idx + 3 ), y1 );
            const x3 = ADD_INT32( stackGetReal( ctx, s, idx + 4 ), x2 );
            const y3 = ADD_INT32( stackGetReal( ctx, s, idx + 5 ), y2 );
            glyphPathCurveTo( gp, x1, y1, x2, y2, x3, y3 );
            ctx.curX = x3; ctx.curY = y3; idx += 6;
          }
          if ( op1 === 24 ) {
            ctx.curX = ADD_INT32( ctx.curX, stackGetReal( ctx, s, idx + 0 ) );
            ctx.curY = ADD_INT32( ctx.curY, stackGetReal( ctx, s, idx + 1 ) );
            glyphPathLineTo( gp, ctx.curX, ctx.curY );
          }
          stackClear( s );
          continue mainLoop;
        }

        case 9:                                          /* closepath, T1 only */
          break;

        case 29:                                         /* callgsubr */
        case 10: {                                       /* callsubr  */
          if ( charstringIndex > CF2_MAX_SUBR ) {
            setError( ctx, Err_Invalid_Glyph_Format ); break exitLbl;
          }

          /* push the current region on the subr stack */
          charstring = subrStack[ charstringIndex + 1 ];

          const subrNum = stackPopInt( ctx, s );

          const fail = ( op1 === 29 )
            ? initRegionBuffer( ctx.globalSubrs, ctx.numGlobals, ctx.globalsBias,
                                subrNum, charstring )
            : initRegionBuffer( ctx.localSubrs, ctx.numLocals, ctx.localsBias,
                                subrNum, charstring );
          if ( fail ) { setError( ctx, Err_Invalid_Glyph_Format ); break exitLbl; }

          charstringIndex += 1;
          continue mainLoop;
        }

        case 11:                                         /* return */
          if ( charstringIndex < 1 ) {
            setError( ctx, Err_Invalid_Glyph_Format ); break exitLbl;
          }
          charstring = subrStack[ --charstringIndex ];
          continue mainLoop;

        case 12: {                                       /* escape */
          const op2 = cf2BufReadByte( ctx, charstring );
          if ( _opCensus ) census( '12/' + op2 );

          switch ( op2 ) {
            case 34:                                     /* hflex  */
              cf2DoFlex( ctx, s, gp, RFS_HFLEX, false );
              continue mainLoop;

            case 35:                                     /* flex   */
              cf2DoFlex( ctx, s, gp, RFS_FLEX, false );
              break;   /* the C does `break', not `continue', here */

            case 36:                                     /* hflex1 */
              cf2DoFlex( ctx, s, gp, RFS_HFLEX1, false );
              continue mainLoop;

            case 37:                                     /* flex1  */
              cf2DoFlex( ctx, s, gp, RFS_FLEX1, true );
              continue mainLoop;

            case 8: case 13: case 19: case 25: case 31: case 32:
              break;                                     /* reserved */

            default: {
              if ( ctx.isCFF2 || op2 >= 38 ) break;      /* unknown */

              switch ( op2 ) {
                case 0:                                  /* dotsection */
                  break;

                case 1: case 2:                          /* vstem3/hstem3, T1 */
                  break;

                case 3: {                                /* and */
                  const arg2 = stackPopFixed( ctx, s );
                  const arg1 = stackPopFixed( ctx, s );
                  stackPushInt( ctx, s, ( arg1 && arg2 ) ? 1 : 0 );
                  continue mainLoop;
                }
                case 4: {                                /* or */
                  const arg2 = stackPopFixed( ctx, s );
                  const arg1 = stackPopFixed( ctx, s );
                  stackPushInt( ctx, s, ( arg1 || arg2 ) ? 1 : 0 );
                  continue mainLoop;
                }
                case 5: {                                /* not */
                  const arg = stackPopFixed( ctx, s );
                  stackPushInt( ctx, s, arg ? 0 : 1 );
                  continue mainLoop;
                }

                case 6:                                  /* seac, T1 only */
                case 7:                                  /* sbw,  T1 only */
                  break;

                case 9: {                                /* abs */
                  const arg = stackPopFixed( ctx, s );
                  if ( arg < -CF2_FIXED_MAX ) stackPushFixed( ctx, s, CF2_FIXED_MAX );
                  else stackPushFixed( ctx, s, arg < 0 ? -arg : arg );
                  continue mainLoop;
                }
                case 10: {                               /* add */
                  const b = stackPopFixed( ctx, s ), a = stackPopFixed( ctx, s );
                  stackPushFixed( ctx, s, ADD_INT32( a, b ) );
                  continue mainLoop;
                }
                case 11: {                               /* sub */
                  const b = stackPopFixed( ctx, s ), a = stackPopFixed( ctx, s );
                  stackPushFixed( ctx, s, SUB_INT32( a, b ) );
                  continue mainLoop;
                }
                case 12: {                               /* div */
                  const divisor  = stackPopFixed( ctx, s );
                  const dividend = stackPopFixed( ctx, s );
                  stackPushFixed( ctx, s, FT_DivFix( dividend, divisor ) );
                  continue mainLoop;
                }
                case 14: {                               /* neg */
                  const arg = stackPopFixed( ctx, s );
                  if ( arg < -CF2_FIXED_MAX ) stackPushFixed( ctx, s, CF2_FIXED_MAX );
                  else stackPushFixed( ctx, s, -arg );
                  continue mainLoop;
                }
                case 15: {                               /* eq */
                  const arg2 = stackPopFixed( ctx, s );
                  const arg1 = stackPopFixed( ctx, s );
                  stackPushInt( ctx, s, arg1 === arg2 ? 1 : 0 );
                  continue mainLoop;
                }

                case 16:                                 /* callothersubr, T1 */
                case 17:                                 /* pop, T1 */
                  break;

                case 18:                                 /* drop */
                  stackPopFixed( ctx, s );
                  continue mainLoop;

                case 20: {                               /* put */
                  const idx = stackPopInt( ctx, s ) >>> 0;
                  const val = stackPopFixed( ctx, s );
                  if ( idx < CF2_STORAGE_SIZE ) storage[idx] = val;
                  continue mainLoop;
                }
                case 21: {                               /* get */
                  const idx = stackPopInt( ctx, s ) >>> 0;
                  if ( idx < CF2_STORAGE_SIZE ) stackPushFixed( ctx, s, storage[idx] );
                  continue mainLoop;
                }
                case 22: {                               /* ifelse */
                  const cond2 = stackPopFixed( ctx, s );
                  const cond1 = stackPopFixed( ctx, s );
                  const arg2  = stackPopFixed( ctx, s );
                  const arg1  = stackPopFixed( ctx, s );
                  stackPushFixed( ctx, s, cond1 <= cond2 ? arg1 : arg2 );
                  continue mainLoop;
                }
                case 23: {                               /* random */
                  /* "only use the lower 16 bits of `random' to generate a
                   *  number in the range (0;1]"                            */
                  const r = ( ( ctx.subfont.random & 0xFFFF ) + 1 ) | 0;
                  ctx.subfont.random = cff_random( ctx.subfont.random );
                  stackPushFixed( ctx, s, r );
                  continue mainLoop;
                }
                case 24: {                               /* mul */
                  const f2 = stackPopFixed( ctx, s ), f1 = stackPopFixed( ctx, s );
                  stackPushFixed( ctx, s, FT_MulFix( f1, f2 ) );
                  continue mainLoop;
                }
                case 26: {                               /* sqrt */
                  let arg = stackPopFixed( ctx, s );
                  arg = arg > 0 ? FT_SqrtFixed( arg ) | 0 : 0;
                  stackPushFixed( ctx, s, arg );
                  continue mainLoop;
                }
                case 27: {                               /* dup */
                  const arg = stackPopFixed( ctx, s );
                  stackPushFixed( ctx, s, arg );
                  stackPushFixed( ctx, s, arg );
                  continue mainLoop;
                }
                case 28: {                               /* exch */
                  const arg2 = stackPopFixed( ctx, s );
                  const arg1 = stackPopFixed( ctx, s );
                  stackPushFixed( ctx, s, arg2 );
                  stackPushFixed( ctx, s, arg1 );
                  continue mainLoop;
                }
                case 29: {                               /* index */
                  const idx = stackPopInt( ctx, s );
                  const size = stackCount( s );
                  if ( size > 0 ) {
                    /* index 0 is the BOTTOM of the stack for getReal */
                    let grIdx;
                    if ( idx < 0 ) grIdx = size - 1;
                    else if ( idx >= size ) grIdx = 0;
                    else grIdx = size - 1 - idx;
                    stackPushFixed( ctx, s, stackGetReal( ctx, s, grIdx ) );
                  }
                  continue mainLoop;
                }
                case 30: {                               /* roll */
                  const idx = stackPopInt( ctx, s );
                  const count = stackPopInt( ctx, s );
                  stackRoll( ctx, s, count, idx );
                  continue mainLoop;
                }

                case 33:                                 /* setcurrentpoint, T1 */
                  break;

                default:
                  break;
              }
            }
          }
          break;                                         /* end of case 12 */
        }

        case 13:                                         /* hsbw, T1 only */
          break;

        case 14: {                                       /* endchar */
          const cnt = stackCount( s );
          if ( cnt === 1 || cnt === 5 ) {
            if ( !ctx.haveWidth )
              ctx.width = ADD_INT32( stackGetReal( ctx, s, 0 ), nominalWidthX );
          }
          ctx.haveWidth = true;
          if ( ctx.widthOnly ) break exitLbl;

          glyphPathCloseOpenPath( gp );

          /* the deprecated implied `seac': 4 or 5 operands left over */
          if ( !ctx.isCFF2 && stackCount( s ) > 1 ) {
            if ( doingSeac ) { setError( ctx, Err_Invalid_Glyph_Format ); break exitLbl; }

            const achar = stackPopInt( ctx, s );
            const bchar = stackPopInt( ctx, s );
            const adyY  = stackPopFixed( ctx, s );
            const adxX  = stackPopFixed( ctx, s );

            /* NOTE the order: the Adobe engine draws the ACCENT first (at
             * adx/ady) and the BASE second (at the origin).                */
            const acc = getSeacComponent( ctx, achar );
            if ( !acc ) { setError( ctx, Err_Invalid_Glyph_Format ); break exitLbl; }
            interpSeacComponent( ctx, acc, adxX, adyY );

            const base = getSeacComponent( ctx, bchar );
            if ( !base ) { setError( ctx, Err_Invalid_Glyph_Format ); break exitLbl; }
            interpSeacComponent( ctx, base, 0, 0 );
          }
          break exitLbl;
        }

        case 20:                                         /* cntrmask */
        case 19: {                                       /* hintmask */
          /* "never add hints after the mask is computed" -- note that this
           * `break' also skips READING the mask bytes.                     */
          if ( stackCount( s ) > 1 && ctx.hintMask.isValid ) break;

          /* "if there are arguments on the stack, this is an implied
           *  cf2_cmdVSTEMHM"                                               */
          cf2DoStems( ctx, s, true );
          if ( ctx.widthOnly ) break exitLbl;

          const bits = ctx.hStemCount + ctx.vStemCount;
          if ( op1 === 19 ) {
            cf2HintmaskRead( ctx, ctx.hintMask, charstring, bits );
          } else {
            /* cntrmask builds a throw-away hint map from a SEPARATE mask;
             * with hinting off the map cannot affect the outline, but the
             * mask bytes must still be consumed.  Crucially it does NOT
             * validate the interpreter's own hintMask.                     */
            const counterMask = newHintMask();
            cf2HintmaskRead( ctx, counterMask, charstring, bits );
            if ( counterMask.isValid ) gp.initialHintMapIsValid = true;
          }
          break;
        }

        case 21: {                                       /* rmoveto */
          if ( stackCount( s ) > 2 && !ctx.haveWidth )
            ctx.width = ADD_INT32( stackGetReal( ctx, s, 0 ), nominalWidthX );
          ctx.haveWidth = true;
          if ( ctx.widthOnly ) break exitLbl;

          ctx.curY = ADD_INT32( ctx.curY, stackPopFixed( ctx, s ) );
          ctx.curX = ADD_INT32( ctx.curX, stackPopFixed( ctx, s ) );
          glyphPathMoveTo( gp, ctx.curX, ctx.curY );
          break;
        }

        case 22: {                                       /* hmoveto */
          if ( stackCount( s ) > 1 && !ctx.haveWidth )
            ctx.width = ADD_INT32( stackGetReal( ctx, s, 0 ), nominalWidthX );
          ctx.haveWidth = true;
          if ( ctx.widthOnly ) break exitLbl;

          ctx.curX = ADD_INT32( ctx.curX, stackPopFixed( ctx, s ) );
          glyphPathMoveTo( gp, ctx.curX, ctx.curY );
          break;
        }

        case 25: {                                       /* rlinecurve */
          const count = stackCount( s );
          let idx = 0;
          while ( idx + 6 < count ) {
            ctx.curX = ADD_INT32( ctx.curX, stackGetReal( ctx, s, idx + 0 ) );
            ctx.curY = ADD_INT32( ctx.curY, stackGetReal( ctx, s, idx + 1 ) );
            glyphPathLineTo( gp, ctx.curX, ctx.curY );
            idx += 2;
          }
          while ( idx < count ) {
            const x1 = ADD_INT32( stackGetReal( ctx, s, idx + 0 ), ctx.curX );
            const y1 = ADD_INT32( stackGetReal( ctx, s, idx + 1 ), ctx.curY );
            const x2 = ADD_INT32( stackGetReal( ctx, s, idx + 2 ), x1 );
            const y2 = ADD_INT32( stackGetReal( ctx, s, idx + 3 ), y1 );
            const x3 = ADD_INT32( stackGetReal( ctx, s, idx + 4 ), x2 );
            const y3 = ADD_INT32( stackGetReal( ctx, s, idx + 5 ), y2 );
            glyphPathCurveTo( gp, x1, y1, x2, y2, x3, y3 );
            ctx.curX = x3; ctx.curY = y3; idx += 6;
          }
          stackClear( s );
          continue mainLoop;
        }

        case 26: {                                       /* vvcurveto */
          const count1 = stackCount( s );
          /* "if `cf2_stack_count' isn't of the form 4n or 4n+1, we enforce
           *  it by clearing the second bit"                                */
          const count = count1 & ~2;
          let idx = count1 - count;

          while ( idx < count ) {
            let x1;
            if ( ( count - idx ) & 1 ) {
              x1 = ADD_INT32( stackGetReal( ctx, s, idx ), ctx.curX );
              idx++;
            } else x1 = ctx.curX;

            const y1 = ADD_INT32( stackGetReal( ctx, s, idx + 0 ), ctx.curY );
            const x2 = ADD_INT32( stackGetReal( ctx, s, idx + 1 ), x1 );
            const y2 = ADD_INT32( stackGetReal( ctx, s, idx + 2 ), y1 );
            const x3 = x2;
            const y3 = ADD_INT32( stackGetReal( ctx, s, idx + 3 ), y2 );
            glyphPathCurveTo( gp, x1, y1, x2, y2, x3, y3 );
            ctx.curX = x3; ctx.curY = y3; idx += 4;
          }
          stackClear( s );
          continue mainLoop;
        }

        case 27: {                                       /* hhcurveto */
          const count1 = stackCount( s );
          const count = count1 & ~2;
          let idx = count1 - count;

          while ( idx < count ) {
            let y1;
            if ( ( count - idx ) & 1 ) {
              y1 = ADD_INT32( stackGetReal( ctx, s, idx ), ctx.curY );
              idx++;
            } else y1 = ctx.curY;

            const x1 = ADD_INT32( stackGetReal( ctx, s, idx + 0 ), ctx.curX );
            const x2 = ADD_INT32( stackGetReal( ctx, s, idx + 1 ), x1 );
            const y2 = ADD_INT32( stackGetReal( ctx, s, idx + 2 ), y1 );
            const x3 = ADD_INT32( stackGetReal( ctx, s, idx + 3 ), x2 );
            const y3 = y2;
            glyphPathCurveTo( gp, x1, y1, x2, y2, x3, y3 );
            ctx.curX = x3; ctx.curY = y3; idx += 4;
          }
          stackClear( s );
          continue mainLoop;
        }

        case 30:                                         /* vhcurveto */
        case 31: {                                       /* hvcurveto */
          const count1 = stackCount( s );
          const count = count1 & ~2;
          let idx = count1 - count;
          let alternate = ( op1 === 31 );

          while ( idx < count ) {
            let x1, y1, x2, y2, x3, y3;
            if ( alternate ) {
              x1 = ADD_INT32( stackGetReal( ctx, s, idx + 0 ), ctx.curX );
              y1 = ctx.curY;
              x2 = ADD_INT32( stackGetReal( ctx, s, idx + 1 ), x1 );
              y2 = ADD_INT32( stackGetReal( ctx, s, idx + 2 ), y1 );
              y3 = ADD_INT32( stackGetReal( ctx, s, idx + 3 ), y2 );
              if ( count - idx === 5 ) {
                x3 = ADD_INT32( stackGetReal( ctx, s, idx + 4 ), x2 );
                idx++;
              } else x3 = x2;
              alternate = false;
            } else {
              x1 = ctx.curX;
              y1 = ADD_INT32( stackGetReal( ctx, s, idx + 0 ), ctx.curY );
              x2 = ADD_INT32( stackGetReal( ctx, s, idx + 1 ), x1 );
              y2 = ADD_INT32( stackGetReal( ctx, s, idx + 2 ), y1 );
              x3 = ADD_INT32( stackGetReal( ctx, s, idx + 3 ), x2 );
              if ( count - idx === 5 ) {
                y3 = ADD_INT32( stackGetReal( ctx, s, idx + 4 ), y2 );
                idx++;
              } else y3 = y2;
              alternate = true;
            }
            glyphPathCurveTo( gp, x1, y1, x2, y2, x3, y3 );
            ctx.curX = x3; ctx.curY = y3; idx += 4;
          }
          stackClear( s );
          continue mainLoop;
        }

        case 28: {                                       /* 16-bit number */
          const byte1 = cf2BufReadByte( ctx, charstring );
          const byte2 = cf2BufReadByte( ctx, charstring );
          const v = ( ( ( byte1 << 8 ) | byte2 ) << 16 ) >> 16;   /* FT_Short */
          stackPushInt( ctx, s, v );
          continue mainLoop;
        }

        default: {                                       /* numbers */
          if ( op1 <= 246 ) {
            stackPushInt( ctx, s, op1 - 139 );            /* -107 .. 107 */
          } else if ( op1 <= 250 ) {
            let v = op1; v -= 247; v *= 256;
            v += cf2BufReadByte( ctx, charstring );
            v += 108;
            stackPushInt( ctx, s, v );                    /* 108 .. 1131 */
          } else if ( op1 <= 254 ) {
            let v = op1; v -= 251; v *= 256;
            v += cf2BufReadByte( ctx, charstring );
            v = -v - 108;
            stackPushInt( ctx, s, v );                    /* -1131 .. -108 */
          } else {                                        /* op1 == 255 */
            const b1 = cf2BufReadByte( ctx, charstring );
            const b2 = cf2BufReadByte( ctx, charstring );
            const b3 = cf2BufReadByte( ctx, charstring );
            const b4 = cf2BufReadByte( ctx, charstring );
            /* a 16.16 fixed value, assembled as a wrapping int32 */
            const v = ( ( b1 << 24 ) | ( b2 << 16 ) | ( b3 << 8 ) | b4 ) | 0;
            stackPushFixed( ctx, s, v );
          }
          continue mainLoop;
        }
      }

      stackClear( s );
    }
  }

  const width = ctx.width;
  if ( doingSeac ) { ctx.haveWidth = savedHaveWidth; ctx.width = savedWidth; }
  return width;
}

/* psft.c cf2_getSeacComponent() */
function getSeacComponent( ctx, code ) {
  const gid = cffLookupGlyphByStdCharcode( ctx.cff, code );
  if ( gid < 0 ) return null;
  const el = cffIndexAccessElement( ctx.cff, ctx.cff.charstrings_index, gid );
  if ( !el ) return null;
  return { start: el.start, ptr: el.start, end: el.end };
}

/* The recursive cf2_interpT2CharString() call for a seac component.  It
 * shares the builder (the callbacks) but gets a FRESH CF2_GlyphPath, a
 * fresh hint state and a fresh operand stack in the C, because those are
 * automatic variables of the interpreter.                                 */
function interpSeacComponent( ctx, buf, curX, curY ) {
  const savedStack   = ctx.stack;
  const savedHStems  = ctx.hStems,  savedHCount = ctx.hStemCount;
  const savedVStems  = ctx.vStems,  savedVCount = ctx.vStemCount;
  const savedMask    = ctx.hintMask;
  const savedCurX    = ctx.curX,    savedCurY   = ctx.curY;

  ctx.stack      = newStack( ctx.isCFF2 ? ( savedStack.size ) : 0 );
  ctx.hStems     = []; ctx.hStemCount = 0;
  ctx.vStems     = []; ctx.vStemCount = 0;
  ctx.hintMask   = newHintMask();

  const gp = newGlyphPath( ctx, ctx.scale );
  cf2InterpT2CharString( ctx, buf, gp, true, curX, curY );

  ctx.stack = savedStack;
  ctx.hStems = savedHStems; ctx.hStemCount = savedHCount;
  ctx.vStems = savedVStems; ctx.vStemCount = savedVCount;
  ctx.hintMask = savedMask;
  ctx.curX = savedCurX; ctx.curY = savedCurY;
}


/* ------------------------------------------------------------------ */
/* S6  cff_slot_load() front end                                       */
/* ------------------------------------------------------------------ */

/**
 * Parse (and cache on the face) the CFF table of `face'.
 */
export function getCffFont( face ) {
  if ( !face._cffFont ) face._cffFont = cffFontLoad( face );
  return face._cffFont;
}

/**
 * loadCffOutline( face, gid ) -> { outline, advance, ... }
 *
 * Reproduces cff_slot_load() with FT_LOAD_NO_HINTING (what `ftlint -f 2'
 * asks for) up to, but NOT including, the final
 *
 *     vec->x = FT_MulFix( vec->x, x_scale )
 *
 * loop -- that is W2's ftscale.js.  The FontMatrix and the font offset ARE
 * applied here, because FreeType applies them before scaling and they are
 * expressed in font units.
 *
 * Extra (additive) fields let the CLI finish the job exactly:
 *   topUpm, subUpm  - if they differ, x_scale and y_scale must be adjusted
 *                     with FT_MulDiv( scale, topUpm, subUpm ) and the
 *                     outline must be scaled even under FT_LOAD_NO_SCALE
 *                     (cffgload.c `force_scaling').
 *   forceScaling    - the same condition, precomputed.
 *   fdIndex         - which FDArray subfont was used (CID fonts).
 *   cffWidth        - the charstring-derived advance.  cff_slot_load only
 *                     uses it when the face has no `hmtx'; with an hmtx
 *                     (always, in an SFNT) the metrics come from there.
 */
export function loadCffOutline( face, gid, options ) {
  const cff = getCffFont( face );
  const dict = cff.top_font.font_dict;
  const widthOnly = !!( options && options.widthOnly );

  let glyphIndex = gid;

  /* "in a CID-keyed font, consider `glyph_index' as a CID and map it
   *  immediately to the real glyph_index".  charset.cids is only built for
   *  PURE CFF fonts (cff_font_load passes invert = cid && pure_cff), so
   *  this never triggers for an SFNT-wrapped font.                        */
  if ( dict.cid_registry !== 0xFFFF && cff.charset && cff.charset.cids ) {
    if ( glyphIndex !== 0 ) {
      glyphIndex = cffCharsetCidToGindex( cff.charset, glyphIndex );
      if ( glyphIndex === 0 ) throw new Error( 'cff.js: invalid CID ' + gid );
    }
  } else if ( glyphIndex >= cff.numGlyphs ) {
    throw new Error( 'cff.js: glyph index ' + gid + ' out of range' );
  }

  /* pick the subfont (cff_decoder_prepare) and the font matrix/offset
   * (cff_slot_load)                                                       */
  let subfont = cff.top_font;
  let fontMatrix = dict.font_matrix, fontOffset = dict.font_offset;
  let fdIndex = 0;
  let topUpm = dict.units_per_em, subUpm = dict.units_per_em;
  let forceScaling = false;

  if ( cff.num_subfonts ) {
    fdIndex = cff.fd_select ? cffFdSelectGet( cff, cff.fd_select, glyphIndex ) : 0;
    if ( fdIndex >= cff.num_subfonts ) fdIndex = cff.num_subfonts - 1;

    topUpm = dict.units_per_em;
    subUpm = cff.subfonts[fdIndex].font_dict.units_per_em;
    fontMatrix = cff.subfonts[fdIndex].font_dict.font_matrix;
    fontOffset = cff.subfonts[fdIndex].font_dict.font_offset;
    if ( topUpm !== subUpm ) forceScaling = true;

    subfont = cff.subfonts[fdIndex];
  }

  const priv = subfont.private_dict;

  /* ---- run the interpreter ---- */
  const el = cffIndexAccessElement( cff, cff.charstrings_index, glyphIndex );
  if ( !el ) throw new Error( 'cff.js: cannot access charstring ' + glyphIndex );

  const builder = newBuilder();

  const ctx = {
    data: cff.data, cff, subfont, builder,
    error: Err_Ok,
    widthOnly,
    isCFF2: cff.cff2,
    vstore: cff.vstore || null,
    vsindex: priv.vsindex | 0,
    lenBV: 0,
    usedBV: false,
    stack: newStack( cff.cff2 ? ( dict.maxstack || CFF2_DEFAULT_STACK ) : 0 ),
    hStems: [], vStems: [], hStemCount: 0, vStemCount: 0,
    hintMask: newHintMask(),
    curX: 0, curY: 0,
    haveWidth: false,
    width: 0,
    defaultWidthX: cf2_intToFixed( priv.default_width ),
    nominalWidthX: cf2_intToFixed( priv.nominal_width ),
    localSubrs:  subfont.local_subrs,
    numLocals:   subfont.local_subrs_index ? subfont.local_subrs_index.count : 0,
    localsBias:  0,
    globalSubrs: cff.global_subrs,
    numGlobals:  cff.global_subrs_index.count,
    globalsBias: 0,
    /* psft.c cf2_getScaleAndHintFlag(), UNHINTED branch:
     *   *x_scale = 0x0400;    1/64 as 16.16                               */
    scale: 0x0400
  };
  ctx.localsBias  = cffComputeBias( dict.charstring_type, ctx.numLocals );
  ctx.globalsBias = cffComputeBias( dict.charstring_type, ctx.numGlobals );

  const gp = newGlyphPath( ctx, ctx.scale );

  const buf = { start: el.start, ptr: el.start, end: el.end };
  const width = cf2InterpT2CharString( ctx, buf, gp, false, 0, 0 );

  /* cf2_outline_close(): ps_builder_close_contour + FT_GlyphLoader_Add */
  psBuilderCloseContour( builder );

  /* cf2_setGlyphWidth(): *decoder->glyph_width = cf2_fixedToInt( width );
   * NOTE the (FT_Short) truncation inside cf2_fixedToInt.                 */
  const cffWidth = cf2_fixedToInt( width );

  /* ---- build the FT_Outline image ---- */
  const n = builder.nPoints, nc = builder.nContours;
  const outline = {
    n_points: n, n_contours: nc,
    x: new Int32Array( n ), y: new Int32Array( n ),
    tags: new Uint8Array( n ),
    contours: new Int32Array( nc ),
    /* cffgload.c cff_slot_load():
     *   glyph->root.outline.flags = FT_OUTLINE_REVERSE_FILL;
     *   if ( size && size->root.metrics.y_ppem < 24 )
     *     glyph->root.outline.flags |= FT_OUTLINE_HIGH_PRECISION;
     * We honour the second line when the caller hands us a size object
     * (W2's CLI passes the one from requestCharSize).                     */
    flags: FT_OUTLINE_REVERSE_FILL |
           ( ( options && options.y_ppem !== undefined && options.y_ppem < 24 )
               ? FT_OUTLINE_HIGH_PRECISION : 0 )
  };
  for ( let i = 0; i < n; i++ ) {
    outline.x[i] = builder.xs[i];
    outline.y[i] = builder.ys[i];
    outline.tags[i] = builder.tags[i];
  }
  for ( let i = 0; i < nc; i++ ) outline.contours[i] = builder.contours[i];

  /* "apply the font matrix, if any" (cffgload.c) */
  let advance = cffWidth;
  if ( fontMatrix.xx !== 0x10000 || fontMatrix.yy !== 0x10000 ||
       fontMatrix.xy !== 0       || fontMatrix.yx !== 0 ) {
    for ( let i = 0; i < n; i++ ) {
      const r = ftVectorTransform( outline.x[i], outline.y[i], fontMatrix );
      outline.x[i] = r[0]; outline.y[i] = r[1];
    }
    advance = FT_MulFix( advance, fontMatrix.xx );
  }

  if ( fontOffset.x || fontOffset.y ) {
    for ( let i = 0; i < n; i++ ) {
      outline.x[i] = ADD_LONG( outline.x[i], fontOffset.x );
      outline.y[i] = ADD_LONG( outline.y[i], fontOffset.y );
    }
    advance += fontOffset.x;
  }

  return {
    outline, advance,
    cffWidth, fdIndex, topUpm, subUpm, forceScaling,
    error: ctx.error
  };
}

export default { loadCffOutline, getCffFont, FT_OUTLINE_REVERSE_FILL,
                 FT_OUTLINE_HIGH_PRECISION, usingW1Ftcalc, cff_random };

/* END */
