/* ttinterp.js -- TrueType bytecode interpreter, port of
 * ../freetype/src/truetype/ttinterp.c (FreeType 2.14.3).
 *
 * TARGET: TT_INTERPRETER_VERSION_35 (the classic interpreter).  Every place
 * where the C source branches on `driver->interpreter_version` the v35 branch
 * has been taken and the v40 branch DELETED, per the workstream contract.
 * Concretely, all code under `#ifdef TT_SUPPORT_SUBPIXEL_HINTING_MINIMAL`
 * (backward_compatibility, the x-axis/post-IUP curfews, the SHPIX/DELTAP
 * gating, the v40 GETINFO bits, the v40 MPS) is gone, and
 * `NO_SUBPIXEL_HINTING` is a compile-time TRUE.
 *
 * JS has no integers.  Every C expression that relies on a 64-bit
 * intermediate, on truncating (toward-zero) C division, on a 32-bit wrap, or
 * on a cast that narrows, carries a comment saying so.  FT_Long/FT_Pos/
 * FT_Fixed/FT_F26Dot6 are 64-bit on the reference platform (x86-64 Linux),
 * which is what we emulate; FT_Int32/FT_UInt32/FT_Short/FT_UShort are not.
 */

/* W1 owns ftcalc.js.  Import theirs when it exists; until then fall back to
 * the local reference implementations below (which are what we validate
 * against anyway).  Never create or edit ftcalc.js here. */
let _ext = null;
try {
  _ext = await import( './ftcalc.js' );
}
catch {
  _ext = null;
}


/* ------------------------------------------------------------------ */
/*  error codes (include/freetype/fterrdef.h; FT_Mod_Err_TrueType = 0) */
/* ------------------------------------------------------------------ */

export const ERR = {
  Ok:                        0x00,
  Invalid_Glyph_Index:       0x10,
  Invalid_Outline:           0x14,
  Invalid_Composite:         0x15,
  Too_Many_Hints:            0x16,
  Invalid_Size_Handle:       0x24,
  Invalid_Opcode:            0x80,
  Too_Few_Arguments:         0x81,
  Stack_Overflow:            0x82,
  Code_Overflow:             0x83,
  Bad_Argument:              0x84,
  Divide_By_Zero:            0x85,
  Invalid_Reference:         0x86,
  Debug_OpCode:              0x87,
  ENDF_In_Exec_Stream:       0x88,
  Nested_DEFS:               0x89,
  Invalid_CodeRange:         0x8A,
  Execution_Too_Long:        0x8B,
  Too_Many_Function_Defs:    0x8C,
  Too_Many_Instruction_Defs: 0x8D,
  Table_Missing:             0x8E,
  Invalid_PPem:              0x97,
  Could_Not_Find_Context:    0x99,
  DEF_In_Glyf_Bytecode:      0x9C,
};

/* ttinterp.c: TT_CONFIG_OPTION_MAX_RUNNABLE_OPCODES */
const MAX_RUNNABLE_OPCODES = 1000000;

/* ttinterp.h rounding modes */
export const TT_Round_Off            = 5;
export const TT_Round_To_Half_Grid   = 0;
export const TT_Round_To_Grid        = 1;
export const TT_Round_To_Double_Grid = 2;
export const TT_Round_Up_To_Grid     = 4;
export const TT_Round_Down_To_Grid   = 3;
export const TT_Round_Super          = 6;
export const TT_Round_Super_45       = 7;

/* ttinterp.h TT_CodeRange_Tag */
export const tt_coderange_none  = 0;
export const tt_coderange_font  = 1;
export const tt_coderange_cvt   = 2;
export const tt_coderange_glyph = 3;

/* ftimage.h */
export const FT_CURVE_TAG_ON           = 0x01;
export const FT_CURVE_TAG_HAS_SCANMODE = 0x04;
export const FT_CURVE_TAG_TOUCH_X      = 0x08;
export const FT_CURVE_TAG_TOUCH_Y      = 0x10;
export const FT_CURVE_TAG_TOUCH_BOTH   = 0x18;

const SUCCESS = 0;
const FAILURE = 1;


/* ------------------------------------------------------------------ */
/*  64-bit / 32-bit emulation helpers                                  */
/* ------------------------------------------------------------------ */

const P2 = new Float64Array( 65 );
for ( let i = 0; i <= 64; i++ ) P2[i] = Math.pow( 2, i );

/* C `>>` on a *signed* 64-bit value: arithmetic shift == floor-divide.
 * JS `>>` would truncate the operand to 32 bits first, so it cannot be used. */
function asr( x, n ) { return Math.floor( x / P2[n] ); }

/* C `/` on integers truncates toward zero; JS `/` does not. */
function cdiv( a, b ) { return Math.trunc( a / b ); }

/* FT_PAD_FLOOR(x, n) == x & ~(n-1) for n a power of two.  Written as a
 * floor-to-multiple because JS `&` is 32-bit and FT_Pos is 64-bit. */
function padFloor( x, n ) { return Math.floor( x / n ) * n; }

export function FT_PIX_FLOOR( x ) { return Math.floor( x / 64 ) * 64; }
export function FT_PIX_ROUND( x ) { return FT_PIX_FLOOR( x + 32 ); }
export function FT_PIX_CEIL( x )  { return FT_PIX_FLOOR( x + 63 ); }
/* The _LONG variants differ from the plain ones only by using ADD_LONG,
 * i.e. by wrapping at 64 bits instead of trapping; with JS doubles the two
 * coincide for every value a real font can produce. */
export const FT_PIX_ROUND_LONG = FT_PIX_ROUND;
export const FT_PIX_CEIL_LONG  = FT_PIX_CEIL;
export function FT_PAD_ROUND_LONG( x, n ) { return padFloor( x + Math.floor( n / 2 ), n ); }

/* (FT_Short)x / (FT_F2Dot14)x : keep the low 16 bits, then sign-extend.
 * `x & 0xFFFF` goes through ToInt32, which preserves the low 32 (hence the
 * low 16) bits of any integral double, so this matches the C narrowing. */
function toS16( x ) { return ( ( x & 0xFFFF ) ^ 0x8000 ) - 0x8000; }
/* (FT_UShort)x */
function toU16( x ) { return x & 0xFFFF; }

/* BOUNDS(x,n): (FT_UInt)x >= (FT_UInt)n -- a 32-bit unsigned compare. */
function BOUNDS( x, n ) { return ( x >>> 0 ) >= ( n >>> 0 ); }
/* BOUNDSL(x,n): (FT_ULong)x >= (FT_ULong)n -- a 64-bit unsigned compare.
 * A negative x becomes a huge unsigned value, hence always out of bounds. */
function BOUNDSL( x, n ) { return x < 0 || x >= n; }

const SAFE = 4503599627370496; /* 2^52; beyond this a double product may lose
                                * integer precision, so we go to BigInt.  */

/* ftcalc.c FT_MulFix: 64-bit intermediate, round-half-away-from-zero. */
function _FT_MulFix( a, b ) {
  const ab = a * b;                                   /* 64-bit intermediate */
  if ( ab > -SAFE && ab < SAFE )
    return Math.floor( ( ab + 0x8000 + ( ab < 0 ? -1 : 0 ) ) / 65536 );
  let t = BigInt( a ) * BigInt( b );
  t = t + 0x8000n + ( t < 0n ? -1n : 0n );
  return Number( t >> 16n );
}

/* ftcalc.c FT_MulDiv: magnitudes in FT_UInt64, C truncating division. */
function _FT_MulDiv( a, b, c ) {
  let s = 1;
  if ( a < 0 ) { a = -a; s = -s; }
  if ( b < 0 ) { b = -b; s = -s; }
  if ( c < 0 ) { c = -c; s = -s; }
  let d;
  if ( c > 0 ) {
    const ab = a * b;                                 /* 64-bit intermediate */
    if ( ab < SAFE ) d = Math.floor( ( ab + Math.floor( c / 2 ) ) / c );
    else d = Number( ( BigInt( a ) * BigInt( b ) + ( BigInt( c ) >> 1n ) ) / BigInt( c ) );
  }
  else d = 0x7FFFFFFF;
  return s < 0 ? -d : d;
}

function _FT_MulDiv_No_Round( a, b, c ) {
  let s = 1;
  if ( a < 0 ) { a = -a; s = -s; }
  if ( b < 0 ) { b = -b; s = -s; }
  if ( c < 0 ) { c = -c; s = -s; }
  let d;
  if ( c > 0 ) {
    const ab = a * b;                                 /* 64-bit intermediate */
    if ( ab < SAFE ) d = Math.floor( ab / c );
    else d = Number( ( BigInt( a ) * BigInt( b ) ) / BigInt( c ) );
  }
  else d = 0x7FFFFFFF;
  return s < 0 ? -d : d;
}

/* ftcalc.c FT_DivFix: (a << 16) needs a 64-bit intermediate. */
function _FT_DivFix( a, b ) {
  let s = 1;
  if ( a < 0 ) { a = -a; s = -s; }
  if ( b < 0 ) { b = -b; s = -s; }
  let q;
  if ( b > 0 ) {
    const a16 = a * 65536;                            /* 64-bit intermediate */
    if ( a16 < SAFE ) q = Math.floor( ( a16 + Math.floor( b / 2 ) ) / b );
    else q = Number( ( ( BigInt( a ) << 16n ) + ( BigInt( b ) >> 1n ) ) / BigInt( b ) );
  }
  else q = 0x7FFFFFFF;
  return s < 0 ? -q : q;
}

export const FT_MulFix          = _ext?.FT_MulFix          ?? _FT_MulFix;
export const FT_MulDiv          = _ext?.FT_MulDiv          ?? _FT_MulDiv;
export const FT_MulDiv_No_Round = _ext?.FT_MulDiv_No_Round ?? _FT_MulDiv_No_Round;
export const FT_DivFix          = _ext?.FT_DivFix          ?? _FT_DivFix;

/* ftcalc.c FT_MSB, on a 32-bit unsigned. */
function FT_MSB( z ) {
  let shift = 0;
  z = z >>> 0;
  if ( z & 0xFFFF0000 ) { z >>>= 16; shift += 16; }
  if ( z & 0x0000FF00 ) { z >>>= 8;  shift += 8;  }
  if ( z & 0x000000F0 ) { z >>>= 4;  shift += 4;  }
  if ( z & 0x0000000C ) { z >>>= 2;  shift += 2;  }
  if ( z & 0x00000002 ) {            shift += 1;  }
  return shift;
}

/* ttinterp.c TT_MulFix14_64: (a*b)/2^14 with rounding, 64-bit intermediate. */
export function TT_MulFix14( a, b ) {
  const ab = a * b;                                   /* 64-bit intermediate */
  return Math.floor( ( ab + 0x2000 + ( ab < 0 ? -1 : 0 ) ) / 16384 );
}

/* ttinterp.c TT_DotFix14: (ax*bx + ay*by)/2^14, 64-bit intermediate. */
export function TT_DotFix14( ax, ay, bx, by ) {
  const c = ax * bx + ay * by;                        /* 64-bit intermediate */
  return Math.floor( ( c + 0x2000 + ( c < 0 ? -1 : 0 ) ) / 16384 );
}

/* ftcalc.c FT_Vector_NormLen.  This one is genuinely 32-bit throughout:
 * every product below is an FT_Int32/FT_UInt32 multiply that WRAPS, which is
 * what Math.imul gives us. */
function FT_Vector_NormLen( vec ) {
  let x_ = vec.x | 0, y_ = vec.y | 0;   /* FT_Int32 x_ = vector->x (narrows) */
  let sx = 1, sy = 1, x, y;

  if ( x_ < 0 ) { x = ( 0 - x_ ) >>> 0; sx = -sx; } else x = x_ >>> 0;
  if ( y_ < 0 ) { y = ( 0 - y_ ) >>> 0; sy = -sy; } else y = y_ >>> 0;

  if ( x === 0 ) { if ( y > 0 ) vec.y = sy * 0x10000; return y; }
  else if ( y === 0 ) { if ( x > 0 ) vec.x = sx * 0x10000; return x; }

  let l = ( x > y ? x + ( y >>> 1 ) : y + ( x >>> 1 ) ) >>> 0;

  let shift = 31 - FT_MSB( l );
  shift -= 15 + ( l >= ( 0xAAAAAAAA >>> shift ) ? 1 : 0 );

  if ( shift > 0 ) {
    x = ( x << shift ) >>> 0;
    y = ( y << shift ) >>> 0;
    l = ( x > y ? x + ( y >>> 1 ) : y + ( x >>> 1 ) ) >>> 0;
  }
  else {
    x = x >>> ( -shift );
    y = y >>> ( -shift );
    l = l >>> ( -shift );
  }

  let b = ( 0x10000 - ( l | 0 ) ) | 0;
  x_ = x | 0;
  y_ = y | 0;

  let u = 0, v = 0, z;
  do {
    /* FT_Int32 multiplies that wrap -> Math.imul */
    u = ( x_ + ( Math.imul( x_, b ) >> 16 ) ) >>> 0;
    v = ( y_ + ( Math.imul( y_, b ) >> 16 ) ) >>> 0;

    const ss = ( Math.imul( u, u ) + Math.imul( v, v ) ) | 0;
    z = Math.trunc( ( ( 0 - ss ) | 0 ) / 0x200 );     /* C signed division */
    z = Math.trunc( Math.imul( z, ( 0x10000 + b ) >> 8 ) / 0x10000 );

    b = ( b + z ) | 0;
  } while ( z > 0 );

  /* (FT_Pos)u widens the *unsigned* 32-bit value */
  vec.x = sx < 0 ? -u : u;
  vec.y = sy < 0 ? -v : v;

  const t = ( Math.imul( u, x ) + Math.imul( v, y ) ) | 0;
  l = ( 0x10000 + Math.trunc( t / 0x10000 ) ) >>> 0;
  if ( shift > 0 ) l = ( ( l + ( 1 << ( shift - 1 ) ) ) >>> 0 ) >>> shift;
  else l = ( l << ( -shift ) ) >>> 0;

  return l;
}

/* fttrigon.c -- only reachable through FT_Hypot (non-square ppem, and
 * SCALED_COMPONENT_OFFSET composites). */
const FT_TRIG_SCALE     = 0xDBD95B16n;
const FT_TRIG_SAFE_MSB  = 29;
const FT_TRIG_MAX_ITERS = 23;
const FT_ANGLE_PI  = 180 << 16;
const FT_ANGLE_PI2 = FT_ANGLE_PI / 2;
const ft_trig_arctan_table = [
  1740967, 919879, 466945, 234379, 117304, 58666, 29335,
  14668, 7334, 3667, 1833, 917, 458, 229, 115,
  57, 29, 14, 7, 4, 2, 1,
];

function ft_trig_downscale( val ) {
  let s = 1;
  if ( val < 0 ) { val = -val; s = -1; }
  /* val * 0xDBD95B16 overflows 2^53, so this must be BigInt */
  val = Number( ( BigInt( val ) * FT_TRIG_SCALE + 0x40000000n ) >> 32n );
  return s < 0 ? -val : val;
}

function ft_trig_prenorm( vec ) {
  const x = vec.x, y = vec.y;
  let shift = FT_MSB( ( Math.abs( x ) | Math.abs( y ) ) >>> 0 );
  if ( shift <= FT_TRIG_SAFE_MSB ) {
    shift = FT_TRIG_SAFE_MSB - shift;
    vec.x = x * P2[shift];                            /* 64-bit left shift */
    vec.y = y * P2[shift];
  }
  else {
    shift -= FT_TRIG_SAFE_MSB;
    vec.x = asr( x, shift );
    vec.y = asr( y, shift );
    shift = -shift;
  }
  return shift;
}

function ft_trig_pseudo_polarize( vec ) {
  let theta, x = vec.x, y = vec.y, xtemp, b;

  if ( y > x ) {
    if ( y > -x ) { theta = FT_ANGLE_PI2; xtemp = y; y = -x; x = xtemp; }
    else { theta = y > 0 ? FT_ANGLE_PI : -FT_ANGLE_PI; x = -x; y = -y; }
  }
  else {
    if ( y < -x ) { theta = -FT_ANGLE_PI2; xtemp = -y; y = x; x = xtemp; }
    else theta = 0;
  }

  let k = 0;
  for ( let i = 1; i < FT_TRIG_MAX_ITERS; i++ ) {
    b = P2[i - 1];
    if ( y > 0 ) {
      xtemp = x + asr( y + b, i );
      y     = y - asr( x + b, i );
      x     = xtemp;
      theta += ft_trig_arctan_table[k++];
    }
    else {
      xtemp = x - asr( y + b, i );
      y     = y + asr( x + b, i );
      x     = xtemp;
      theta -= ft_trig_arctan_table[k++];
    }
  }

  if ( theta >= 0 ) theta = padFloor( theta + 8, 16 );
  else theta = -padFloor( -theta + 8, 16 );

  vec.x = x;
  vec.y = theta;
}

function FT_Vector_Length( vx, vy ) {
  if ( vx === 0 ) return Math.abs( vy );
  if ( vy === 0 ) return Math.abs( vx );
  const v = { x: vx, y: vy };
  const shift = ft_trig_prenorm( v );
  ft_trig_pseudo_polarize( v );
  v.x = ft_trig_downscale( v.x );
  if ( shift > 0 ) return asr( v.x + P2[shift - 1], shift );
  /* (FT_Fixed)( (FT_UInt32)v.x << -shift ) -- a *32-bit* shift */
  return ( ( v.x >>> 0 ) << ( -shift ) ) >>> 0;
}

export function FT_Hypot( x, y ) { return FT_Vector_Length( x, y ); }


/* ------------------------------------------------------------------ */
/*  Pop_Push_Count / opcode_length                                     */
/* ------------------------------------------------------------------ */

const PACK = ( x, y ) => ( x << 4 ) | y;

const Pop_Push_Count = new Uint8Array( 256 );
{
  const t = Pop_Push_Count;
  /* 0x00 */
  t[0x00] = PACK(0,0); t[0x01] = PACK(0,0); t[0x02] = PACK(0,0); t[0x03] = PACK(0,0);
  t[0x04] = PACK(0,0); t[0x05] = PACK(0,0); t[0x06] = PACK(2,0); t[0x07] = PACK(2,0);
  t[0x08] = PACK(2,0); t[0x09] = PACK(2,0); t[0x0A] = PACK(2,0); t[0x0B] = PACK(2,0);
  t[0x0C] = PACK(0,2); t[0x0D] = PACK(0,2); t[0x0E] = PACK(0,0); t[0x0F] = PACK(5,0);
  /* 0x10 */
  t[0x10] = PACK(1,0); t[0x11] = PACK(1,0); t[0x12] = PACK(1,0); t[0x13] = PACK(1,0);
  t[0x14] = PACK(1,0); t[0x15] = PACK(1,0); t[0x16] = PACK(1,0); t[0x17] = PACK(1,0);
  t[0x18] = PACK(0,0); t[0x19] = PACK(0,0); t[0x1A] = PACK(1,0); t[0x1B] = PACK(0,0);
  t[0x1C] = PACK(1,0); t[0x1D] = PACK(1,0); t[0x1E] = PACK(1,0); t[0x1F] = PACK(1,0);
  /* 0x20 */
  t[0x20] = PACK(1,2); t[0x21] = PACK(1,0); t[0x22] = PACK(0,0); t[0x23] = PACK(2,2);
  t[0x24] = PACK(0,1); t[0x25] = PACK(1,1); t[0x26] = PACK(1,0); t[0x27] = PACK(2,0);
  t[0x28] = PACK(0,0); t[0x29] = PACK(1,0); t[0x2A] = PACK(2,0); t[0x2B] = PACK(1,0);
  t[0x2C] = PACK(1,0); t[0x2D] = PACK(0,0); t[0x2E] = PACK(1,0); t[0x2F] = PACK(1,0);
  /* 0x30 */
  t[0x30] = PACK(0,0); t[0x31] = PACK(0,0); t[0x32] = PACK(0,0); t[0x33] = PACK(0,0);
  t[0x34] = PACK(1,0); t[0x35] = PACK(1,0); t[0x36] = PACK(1,0); t[0x37] = PACK(1,0);
  t[0x38] = PACK(1,0); t[0x39] = PACK(0,0); t[0x3A] = PACK(2,0); t[0x3B] = PACK(2,0);
  t[0x3C] = PACK(0,0); t[0x3D] = PACK(0,0); t[0x3E] = PACK(2,0); t[0x3F] = PACK(2,0);
  /* 0x40 */
  t[0x40] = PACK(0,0); t[0x41] = PACK(0,0); t[0x42] = PACK(2,0); t[0x43] = PACK(1,1);
  t[0x44] = PACK(2,0); t[0x45] = PACK(1,1); t[0x46] = PACK(1,1); t[0x47] = PACK(1,1);
  t[0x48] = PACK(2,0); t[0x49] = PACK(2,1); t[0x4A] = PACK(2,1); t[0x4B] = PACK(0,1);
  t[0x4C] = PACK(0,1); t[0x4D] = PACK(0,0); t[0x4E] = PACK(0,0); t[0x4F] = PACK(1,0);
  /* 0x50 */
  t[0x50] = PACK(2,1); t[0x51] = PACK(2,1); t[0x52] = PACK(2,1); t[0x53] = PACK(2,1);
  t[0x54] = PACK(2,1); t[0x55] = PACK(2,1); t[0x56] = PACK(1,1); t[0x57] = PACK(1,1);
  t[0x58] = PACK(1,0); t[0x59] = PACK(0,0); t[0x5A] = PACK(2,1); t[0x5B] = PACK(2,1);
  t[0x5C] = PACK(1,1); t[0x5D] = PACK(1,0); t[0x5E] = PACK(1,0); t[0x5F] = PACK(1,0);
  /* 0x60 */
  t[0x60] = PACK(2,1); t[0x61] = PACK(2,1); t[0x62] = PACK(2,1); t[0x63] = PACK(2,1);
  t[0x64] = PACK(1,1); t[0x65] = PACK(1,1); t[0x66] = PACK(1,1); t[0x67] = PACK(1,1);
  t[0x68] = PACK(1,1); t[0x69] = PACK(1,1); t[0x6A] = PACK(1,1); t[0x6B] = PACK(1,1);
  t[0x6C] = PACK(1,1); t[0x6D] = PACK(1,1); t[0x6E] = PACK(1,1); t[0x6F] = PACK(1,1);
  /* 0x70 */
  t[0x70] = PACK(2,0); t[0x71] = PACK(1,0); t[0x72] = PACK(1,0); t[0x73] = PACK(1,0);
  t[0x74] = PACK(1,0); t[0x75] = PACK(1,0); t[0x76] = PACK(1,0); t[0x77] = PACK(1,0);
  t[0x78] = PACK(2,0); t[0x79] = PACK(2,0); t[0x7A] = PACK(0,0); t[0x7B] = PACK(0,0);
  t[0x7C] = PACK(0,0); t[0x7D] = PACK(0,0); t[0x7E] = PACK(1,0); t[0x7F] = PACK(1,0);
  /* 0x80 */
  t[0x80] = PACK(0,0); t[0x81] = PACK(2,0); t[0x82] = PACK(2,0); t[0x83] = PACK(0,0);
  t[0x84] = PACK(0,0); t[0x85] = PACK(1,0); t[0x86] = PACK(2,0); t[0x87] = PACK(2,0);
  t[0x88] = PACK(1,1); t[0x89] = PACK(1,0); t[0x8A] = PACK(3,3); t[0x8B] = PACK(2,1);
  t[0x8C] = PACK(2,1); t[0x8D] = PACK(1,0); t[0x8E] = PACK(2,0); t[0x8F] = PACK(0,0);
  /* 0x90 */
  t[0x90] = PACK(0,0); t[0x91] = PACK(0,0); t[0x92] = PACK(0,1);
  /* 0x93..0xAF all PACK(0,0) -- already zero */
  /* 0xB0 PUSHB / 0xB8 PUSHW */
  for ( let i = 0; i < 8; i++ ) { t[0xB0 + i] = PACK(0, i + 1); t[0xB8 + i] = PACK(0, i + 1); }
  /* 0xC0..0xDF MDRP / 0xE0..0xFF MIRP */
  for ( let i = 0xC0; i < 0xE0; i++ ) t[i] = PACK(1,0);
  for ( let i = 0xE0; i < 0x100; i++ ) t[i] = PACK(2,0);
}

const opcode_length = new Int8Array( 256 );
opcode_length.fill( 1 );
opcode_length[0x40] = -1;
opcode_length[0x41] = -2;
for ( let i = 0; i < 8; i++ ) {
  opcode_length[0xB0 + i] = i + 2;        /* 2,3,4,5,6,7,8,9    */
  opcode_length[0xB8 + i] = 2 * i + 3;    /* 3,5,7,9,11,13,15,17 */
}


/* ------------------------------------------------------------------ */
/*  graphics state                                                     */
/* ------------------------------------------------------------------ */

export function tt_default_graphics_state() {
  return {
    rp0: 0, rp1: 0, rp2: 0,
    gep0: 1, gep1: 1, gep2: 1,
    dualVector: { x: 0x4000, y: 0 },
    projVector: { x: 0x4000, y: 0 },
    freeVector: { x: 0x4000, y: 0 },
    loop: 1,
    round_state: 1,
    compensation: [ 0, 0, 0, 0 ],
    minimum_distance: 64,
    control_value_cutin: 68,
    single_width_cutin: 0,
    single_width_value: 0,
    delta_base: 9,
    delta_shift: 3,
    auto_flip: true,
    instruct_control: 0,
    scan_control: false,
    scan_type: 0,
  };
}

function copyGS( g ) {
  return {
    rp0: g.rp0, rp1: g.rp1, rp2: g.rp2,
    gep0: g.gep0, gep1: g.gep1, gep2: g.gep2,
    dualVector: { x: g.dualVector.x, y: g.dualVector.y },
    projVector: { x: g.projVector.x, y: g.projVector.y },
    freeVector: { x: g.freeVector.x, y: g.freeVector.y },
    loop: g.loop,
    round_state: g.round_state,
    compensation: g.compensation.slice(),
    minimum_distance: g.minimum_distance,
    control_value_cutin: g.control_value_cutin,
    single_width_cutin: g.single_width_cutin,
    single_width_value: g.single_width_value,
    delta_base: g.delta_base,
    delta_shift: g.delta_shift,
    auto_flip: g.auto_flip,
    instruct_control: g.instruct_control,
    scan_control: g.scan_control,
    scan_type: g.scan_type,
  };
}


/* ------------------------------------------------------------------ */
/*  glyph zones (ttobjs.c tt_glyphzone_new, ttgload.c tt_prepare_zone)  */
/* ------------------------------------------------------------------ */

/* A zone is the JS image of TT_GlyphZoneRec.  C stores FT_Vector arrays; we
 * split them into parallel X/Y arrays so that IUP's `(FT_Pos*)org + 1' trick
 * becomes a plain choice of array.  `org/cur/orus/tags' are *subarray views*
 * offset by first_point exactly as C offsets the pointers, so that the
 * pointer-identity tests in SHC/SHZ (`zp.cur != exc->zp2.cur') can be
 * reproduced by comparing the view objects with `==='. */
export function newGlyphZone( maxPoints, maxContours ) {
  return {
    n_points: maxPoints,
    n_contours: maxContours,
    orgX: new Int32Array( maxPoints ), orgY: new Int32Array( maxPoints ),
    curX: new Int32Array( maxPoints ), curY: new Int32Array( maxPoints ),
    orusX: new Int32Array( maxPoints ), orusY: new Int32Array( maxPoints ),
    tags: new Uint8Array( maxPoints ),
    contours: new Int32Array( maxContours ),
    first_point: 0,
  };
}


/* ------------------------------------------------------------------ */
/*  CVT / ppem accessors                                               */
/* ------------------------------------------------------------------ */

/* ttinterp.c Current_Ratio */
function Current_Ratio( exc ) {
  if ( !exc.tt_metrics.ratio ) {
    if ( exc.GS.projVector.y === 0 )
      exc.tt_metrics.ratio = exc.tt_metrics.x_ratio;
    else if ( exc.GS.projVector.x === 0 )
      exc.tt_metrics.ratio = exc.tt_metrics.y_ratio;
    else {
      const x = TT_MulFix14( exc.tt_metrics.x_ratio, exc.GS.projVector.x );
      const y = TT_MulFix14( exc.tt_metrics.y_ratio, exc.GS.projVector.y );
      exc.tt_metrics.ratio = FT_Hypot( x, y );
    }
  }
  return exc.tt_metrics.ratio;
}

function Current_Ppem( exc ) { return exc.tt_metrics.ppem; }
function Current_Ppem_Stretched( exc ) {
  return FT_MulFix( exc.tt_metrics.ppem, Current_Ratio( exc ) );
}

function Read_CVT( exc, idx ) { return exc.cvt[idx]; }
function Read_CVT_Stretched( exc, idx ) {
  return FT_MulFix( exc.cvt[idx], Current_Ratio( exc ) );
}

/* ttinterp.c Modify_CVT_Check: copy-on-write so that a glyph program cannot
 * leak CVT changes into the next glyph. */
function Modify_CVT_Check( exc ) {
  if ( exc.iniRange === tt_coderange_glyph && exc.cvt !== exc.glyfCvt ) {
    if ( !exc.glyfCvt || exc.glyfCvtSize !== exc.cvtSize ) {
      exc.glyfCvt = new Float64Array( exc.cvtSize );
      exc.glyfCvtSize = exc.cvtSize;
    }
    exc.glyfCvt.set( exc.cvt.subarray( 0, exc.cvtSize ) );
    exc.cvt = exc.glyfCvt;
  }
}

function Write_CVT( exc, idx, value ) {
  Modify_CVT_Check( exc );
  if ( exc.error ) return;
  exc.cvt[idx] = value;
}

function Write_CVT_Stretched( exc, idx, value ) {
  Modify_CVT_Check( exc );
  if ( exc.error ) return;
  exc.cvt[idx] = FT_DivFix( value, Current_Ratio( exc ) );
}

function Move_CVT( exc, idx, value ) {
  Modify_CVT_Check( exc );
  if ( exc.error ) return;
  exc.cvt[idx] = exc.cvt[idx] + value;
}

function Move_CVT_Stretched( exc, idx, value ) {
  Modify_CVT_Check( exc );
  if ( exc.error ) return;
  exc.cvt[idx] = exc.cvt[idx] + FT_DivFix( value, Current_Ratio( exc ) );
}


/* ------------------------------------------------------------------ */
/*  code ranges                                                        */
/* ------------------------------------------------------------------ */

export function TT_Set_CodeRange( exc, range, base, length ) {
  exc.codeRangeTable[range - 1] = { base, size: length };
  exc.code     = base;
  exc.codeSize = length;
  exc.IP       = 0;
  exc.curRange = range;
  exc.iniRange = range;
}

export function TT_Clear_CodeRange( exc, range ) {
  exc.codeRangeTable[range - 1] = { base: null, size: 0 };
}

function Ins_Goto_CodeRange( exc, aRange, aIP ) {
  if ( aRange < 1 || aRange > 3 ) { exc.error = ERR.Bad_Argument; return FAILURE; }
  const range = exc.codeRangeTable[aRange - 1];
  if ( !range.base ) { exc.error = ERR.Invalid_CodeRange; return FAILURE; }
  /* aIP <= size, not <, because the last instruction may be a CALL */
  if ( aIP > range.size ) { exc.error = ERR.Code_Overflow; return FAILURE; }
  exc.code     = range.base;
  exc.codeSize = range.size;
  exc.IP       = aIP;
  exc.length   = 0;
  exc.curRange = aRange;
  return SUCCESS;
}


/* ------------------------------------------------------------------ */
/*  point movement                                                     */
/* ------------------------------------------------------------------ */

/* v35: no backward-compatibility curfew -- the point always moves. */
function Direct_Move( exc, zone, point, distance ) {
  let v = exc.moveVector.x;
  if ( v !== 0 ) {
    zone.curX[point] = zone.curX[point] + FT_MulFix( distance, v );
    zone.tags[point] |= FT_CURVE_TAG_TOUCH_X;
  }
  v = exc.moveVector.y;
  if ( v !== 0 ) {
    zone.curY[point] = zone.curY[point] + FT_MulFix( distance, v );
    zone.tags[point] |= FT_CURVE_TAG_TOUCH_Y;
  }
}

function Direct_Move_Orig( exc, zone, point, distance ) {
  let v = exc.moveVector.x;
  if ( v !== 0 ) zone.orgX[point] = zone.orgX[point] + FT_MulFix( distance, v );
  v = exc.moveVector.y;
  if ( v !== 0 ) zone.orgY[point] = zone.orgY[point] + FT_MulFix( distance, v );
}

function Direct_Move_X( exc, zone, point, distance ) {
  zone.curX[point] = zone.curX[point] + distance;
  zone.tags[point] |= FT_CURVE_TAG_TOUCH_X;
}

function Direct_Move_Y( exc, zone, point, distance ) {
  zone.curY[point] = zone.curY[point] + distance;
  zone.tags[point] |= FT_CURVE_TAG_TOUCH_Y;
}

function Direct_Move_Orig_X( exc, zone, point, distance ) {
  zone.orgX[point] = zone.orgX[point] + distance;
}

function Direct_Move_Orig_Y( exc, zone, point, distance ) {
  zone.orgY[point] = zone.orgY[point] + distance;
}


/* ------------------------------------------------------------------ */
/*  rounding                                                           */
/* ------------------------------------------------------------------ */

function Round_None( exc, distance, compensation ) {
  let val;
  if ( distance >= 0 ) { val = distance + compensation; if ( val < 0 ) val = 0; }
  else { val = distance - compensation; if ( val > 0 ) val = 0; }
  return val;
}

function Round_To_Grid( exc, distance, compensation ) {
  let val;
  if ( distance >= 0 ) { val = FT_PIX_ROUND_LONG( distance + compensation ); if ( val < 0 ) val = 0; }
  else { val = -FT_PIX_ROUND_LONG( compensation - distance ); if ( val > 0 ) val = 0; }
  return val;
}

function Round_To_Half_Grid( exc, distance, compensation ) {
  let val;
  if ( distance >= 0 ) { val = FT_PIX_FLOOR( distance + compensation ) + 32; if ( val < 0 ) val = 32; }
  else { val = -( FT_PIX_FLOOR( compensation - distance ) + 32 ); if ( val > 0 ) val = -32; }
  return val;
}

function Round_Down_To_Grid( exc, distance, compensation ) {
  let val;
  if ( distance >= 0 ) { val = FT_PIX_FLOOR( distance + compensation ); if ( val < 0 ) val = 0; }
  else { val = -FT_PIX_FLOOR( compensation - distance ); if ( val > 0 ) val = 0; }
  return val;
}

function Round_Up_To_Grid( exc, distance, compensation ) {
  let val;
  if ( distance >= 0 ) { val = FT_PIX_CEIL_LONG( distance + compensation ); if ( val < 0 ) val = 0; }
  else { val = -FT_PIX_CEIL_LONG( compensation - distance ); if ( val > 0 ) val = 0; }
  return val;
}

function Round_To_Double_Grid( exc, distance, compensation ) {
  let val;
  if ( distance >= 0 ) { val = FT_PAD_ROUND_LONG( distance + compensation, 32 ); if ( val < 0 ) val = 0; }
  else { val = -FT_PAD_ROUND_LONG( compensation - distance, 32 ); if ( val > 0 ) val = 0; }
  return val;
}

/* `& -exc->period': period is always a power of two here (SROUND passes
 * GridPeriod 0x4000, so period is 32, 64 or 128), and `x & ~(p-1)' on a
 * two's-complement 64-bit value is exactly floor-to-multiple-of-p. */
function Round_Super( exc, distance, compensation ) {
  let val;
  if ( distance >= 0 ) {
    val = padFloor( distance + ( exc.threshold - exc.phase + compensation ), exc.period );
    val = val + exc.phase;
    if ( val < 0 ) val = exc.phase;
  }
  else {
    val = -padFloor( ( exc.threshold - exc.phase + compensation ) - distance, exc.period );
    val = val - exc.phase;
    if ( val > 0 ) val = -exc.phase;
  }
  return val;
}

/* S45ROUND's period (22/45/90) is not a power of two, hence the division. */
function Round_Super_45( exc, distance, compensation ) {
  let val;
  if ( distance >= 0 ) {
    val = cdiv( distance + ( exc.threshold - exc.phase + compensation ), exc.period ) * exc.period;
    val = val + exc.phase;
    if ( val < 0 ) val = exc.phase;
  }
  else {
    val = -( cdiv( ( exc.threshold - exc.phase + compensation ) - distance, exc.period ) * exc.period );
    val = val - exc.phase;
    if ( val > 0 ) val = -exc.phase;
  }
  return val;
}

function SetSuperRound( exc, GridPeriod, selector ) {
  switch ( selector & 0xC0 ) {          /* (FT_Int)(selector & 0xC0) */
  case 0:    exc.period = cdiv( GridPeriod, 2 ); break;
  case 0x40: exc.period = GridPeriod; break;
  case 0x80: exc.period = GridPeriod * 2; break;
  case 0xC0: exc.period = GridPeriod; break;   /* reserved, but... */
  }

  switch ( selector & 0x30 ) {
  case 0:    exc.phase = 0; break;
  case 0x10: exc.phase = cdiv( exc.period, 4 ); break;
  case 0x20: exc.phase = cdiv( exc.period, 2 ); break;
  case 0x30: exc.phase = cdiv( exc.period * 3, 4 ); break;
  }

  if ( ( selector & 0x0F ) === 0 ) exc.threshold = exc.period - 1;
  else exc.threshold = cdiv( ( ( selector & 0x0F ) - 4 ) * exc.period, 8 );

  /* convert to F26Dot6 format (arithmetic shifts on signed values) */
  exc.period    = asr( exc.period, 8 );
  exc.phase     = asr( exc.phase, 8 );
  exc.threshold = asr( exc.threshold, 8 );
}

const ROUND_FUNCS = {
  [TT_Round_Off]:            Round_None,
  [TT_Round_To_Half_Grid]:   Round_To_Half_Grid,
  [TT_Round_To_Grid]:        Round_To_Grid,
  [TT_Round_To_Double_Grid]: Round_To_Double_Grid,
  [TT_Round_Up_To_Grid]:     Round_Up_To_Grid,
  [TT_Round_Down_To_Grid]:   Round_Down_To_Grid,
  [TT_Round_Super]:          Round_Super,
  [TT_Round_Super_45]:       Round_Super_45,
};


/* ------------------------------------------------------------------ */
/*  projection                                                         */
/* ------------------------------------------------------------------ */

function Project( exc, dx, dy ) {
  return TT_DotFix14( dx, dy, exc.GS.projVector.x, exc.GS.projVector.y );
}
function Dual_Project( exc, dx, dy ) {
  return TT_DotFix14( dx, dy, exc.GS.dualVector.x, exc.GS.dualVector.y );
}
function Project_x( exc, dx, dy ) { return dx; }
function Project_y( exc, dx, dy ) { return dy; }

/* PROJECT(v1,v2) / DUALPROJ(v1,v2) with explicit component arguments. */
function PROJ( exc, x1, y1, x2, y2 ) { return exc.func_project( exc, x1 - x2, y1 - y2 ); }
function DUALPROJ( exc, x1, y1, x2, y2 ) { return exc.func_dualproj( exc, x1 - x2, y1 - y2 ); }

function Compute_Funcs( exc ) {
  /* F_dot_P fits comfortably in a double: components are <= 0x4000. */
  const F_dot_P = asr(
    exc.GS.projVector.x * exc.GS.freeVector.x +
    exc.GS.projVector.y * exc.GS.freeVector.y + 0x2000, 14 );

  if ( F_dot_P >= 0x3FFE ) {
    /* commonly collinear */
    exc.moveVector.x = exc.GS.freeVector.x * 4;
    exc.moveVector.y = exc.GS.freeVector.y * 4;
  }
  else if ( -0x400 < F_dot_P && F_dot_P < 0x400 ) {
    /* prohibitively orthogonal */
    exc.moveVector.x = 0;
    exc.moveVector.y = 0;
  }
  else {
    /* C integer division truncates toward zero -- NOT Math.floor */
    exc.moveVector.x = cdiv( exc.GS.freeVector.x * 0x10000, F_dot_P );
    exc.moveVector.y = cdiv( exc.GS.freeVector.y * 0x10000, F_dot_P );
  }

  if ( F_dot_P >= 0x3FFE && exc.GS.freeVector.x === 0x4000 ) {
    exc.func_move      = Direct_Move_X;
    exc.func_move_orig = Direct_Move_Orig_X;
  }
  else if ( F_dot_P >= 0x3FFE && exc.GS.freeVector.y === 0x4000 ) {
    exc.func_move      = Direct_Move_Y;
    exc.func_move_orig = Direct_Move_Orig_Y;
  }
  else {
    exc.func_move      = Direct_Move;
    exc.func_move_orig = Direct_Move_Orig;
  }

  if ( exc.GS.projVector.x === 0x4000 ) exc.func_project = Project_x;
  else if ( exc.GS.projVector.y === 0x4000 ) exc.func_project = Project_y;
  else exc.func_project = Project;

  if ( exc.GS.dualVector.x === 0x4000 ) exc.func_dualproj = Project_x;
  else if ( exc.GS.dualVector.y === 0x4000 ) exc.func_dualproj = Project_y;
  else exc.func_dualproj = Dual_Project;

  exc.tt_metrics.ratio = 0;   /* disable cached aspect ratio */
}

function Normalize( Vx, Vy, R ) {
  if ( Vx === 0 && Vy === 0 ) return SUCCESS;   /* UNDOCUMENTED, see C */
  const V = { x: Vx, y: Vy };
  FT_Vector_NormLen( V );
  /* (FT_F2Dot14)( V.x / 4 ): C truncating division, then narrow to int16 */
  R.x = toS16( cdiv( V.x, 4 ) );
  R.y = toS16( cdiv( V.y, 4 ) );
  return SUCCESS;
}


/* ------------------------------------------------------------------ */
/*  execution context                                                  */
/* ------------------------------------------------------------------ */

export function newExecContext( opts ) {
  const maxFDefs = opts.maxFDefs | 0;
  const maxIDefs = opts.maxIDefs | 0;

  const FDefs = new Array( maxFDefs );
  for ( let i = 0; i < maxFDefs; i++ )
    FDefs[i] = { range: 0, start: 0, end: 0, opc: 0, active: false };
  const IDefs = new Array( maxIDefs );
  for ( let i = 0; i < maxIDefs; i++ )
    IDefs[i] = { range: 0, start: 0, end: 0, opc: 0, active: false };

  return {
    face: null, size: null,
    error: ERR.Ok,

    top: 0,
    /* ttobjs.c tt_size_init_bytecode: 50% more than declared, min +128 */
    stackSize: opts.stackSize,
    stack: new Float64Array( opts.stackSize + 1 ),
    args: 0, new_top: 0,

    zp0: null, zp1: null, zp2: null, pts: null, twilight: null,

    pointSize: opts.pointSize,
    metrics: { x_scale: 0, y_scale: 0, x_ppem: 0, y_ppem: 0 },
    tt_metrics: { x_ratio: 0, y_ratio: 0, ratio: 0, scale: 0, ppem: 0,
                  rotated: false, stretched: false },

    GS: tt_default_graphics_state(),

    iniRange: 0, curRange: 0, code: null, IP: 0, codeSize: 0,
    opcode: 0, length: 0,

    cvtSize: opts.cvtSize,
    cvt: null,
    baseCvt: new Float64Array( opts.cvtSize ),
    glyfCvt: null, glyfCvtSize: 0,

    glyphSize: 0, glyphIns: null,

    numFDefs: 0, maxFDefs, FDefs,
    numIDefs: 0, maxIDefs, IDefs,
    maxFunc: 0, maxIns: 0,

    callTop: 0, callSize: 32,
    callStack: Array.from( { length: 32 },
                           () => ( { Caller_Range: 0, Caller_IP: 0, Cur_Count: 0, Def: null } ) ),

    codeRangeTable: [ { base: null, size: 0 }, { base: null, size: 0 }, { base: null, size: 0 } ],

    storeSize: opts.storeSize,
    storage: null,
    baseStorage: new Float64Array( opts.storeSize ),
    glyfStorage: null, glyfStoreSize: 0,

    period: 0, phase: 0, threshold: 0,

    instruction_trap: false,
    is_composite: false,
    pedantic_hinting: !!opts.pedantic,

    func_round: Round_To_Grid,
    moveVector: { x: 0, y: 0 },
    func_project: Project_x, func_dualproj: Project_x,
    func_move: Direct_Move_X, func_move_orig: Direct_Move_Orig_X,
    func_cur_ppem: Current_Ppem,
    func_read_cvt: Read_CVT, func_write_cvt: Write_CVT, func_move_cvt: Move_CVT,

    grayscale: false,

    loopcall_counter: 0, loopcall_counter_max: 0,
    neg_jump_counter: 0, neg_jump_counter_max: 0,

    /* not in C: the allocated twilight size, so that the per-run clamp in
     * TT_Run_Context can be re-applied without permanently shrinking the
     * size object's zone (C gets this for free from its struct copy). */
    twilightAlloc: 0,
  };
}

export function TT_Load_Context( exc, face, size ) {
  exc.face = face;
  exc.size = size;

  /* CVT and storage are not persistent in FreeType: reset them after they
   * might have been modified by a glyph program (copy-on-write above). */
  exc.storage = exc.baseStorage;
  exc.cvt     = exc.baseCvt;

  exc.glyphIns  = null;
  exc.glyphSize = 0;

  exc.pointSize  = size.point_size;
  exc.tt_metrics = size.ttmetrics;
  exc.metrics    = {
    x_scale: size.metrics.x_scale, y_scale: size.metrics.y_scale,
    x_ppem: size.metrics.x_ppem,   y_ppem: size.metrics.y_ppem,
  };

  exc.twilight      = size.twilight;
  exc.twilightAlloc = size.twilightAlloc;
}

export function TT_Save_Context( exc, size ) {
  /* UNDOCUMENTED!  Only these GS values can be modified by the CVT program. */
  size.GS.minimum_distance    = exc.GS.minimum_distance;
  size.GS.control_value_cutin = exc.GS.control_value_cutin;
  size.GS.single_width_cutin  = exc.GS.single_width_cutin;
  size.GS.single_width_value  = exc.GS.single_width_value;
  size.GS.delta_base          = exc.GS.delta_base;
  size.GS.delta_shift         = exc.GS.delta_shift;
  size.GS.auto_flip           = exc.GS.auto_flip;
  size.GS.instruct_control    = exc.GS.instruct_control;
  size.GS.scan_control        = exc.GS.scan_control;
  size.GS.scan_type           = exc.GS.scan_type;
}


/* ------------------------------------------------------------------ */
/*  opcodes                                                            */
/* ------------------------------------------------------------------ */

function Ins_MPPEM( exc, A ) { exc.stack[A] = exc.func_cur_ppem( exc ); }

/* v35: MS's GDI interpreter always returns 12; FreeType returns the ppem. */
function Ins_MPS( exc, A ) { exc.stack[A] = exc.func_cur_ppem( exc ); }

function Ins_DUP( exc, A ) { exc.stack[A + 1] = exc.stack[A]; }
function Ins_POP() {}
function Ins_CLEAR( exc ) { exc.new_top = 0; }
function Ins_SWAP( exc, A ) {
  const S = exc.stack, L = S[A]; S[A] = S[A + 1]; S[A + 1] = L;
}
function Ins_DEPTH( exc, A ) { exc.stack[A] = exc.top; }

function Ins_LT( exc, A )   { const S = exc.stack; S[A] = S[A] <  S[A + 1] ? 1 : 0; }
function Ins_LTEQ( exc, A ) { const S = exc.stack; S[A] = S[A] <= S[A + 1] ? 1 : 0; }
function Ins_GT( exc, A )   { const S = exc.stack; S[A] = S[A] >  S[A + 1] ? 1 : 0; }
function Ins_GTEQ( exc, A ) { const S = exc.stack; S[A] = S[A] >= S[A + 1] ? 1 : 0; }
function Ins_EQ( exc, A )   { const S = exc.stack; S[A] = S[A] === S[A + 1] ? 1 : 0; }
function Ins_NEQ( exc, A )  { const S = exc.stack; S[A] = S[A] !== S[A + 1] ? 1 : 0; }

function Ins_ODD( exc, A ) {
  /* `& 64' on an F26Dot6: the value is small, so a 32-bit AND is faithful. */
  exc.stack[A] = ( exc.func_round( exc, exc.stack[A], 0 ) & 64 ) === 64 ? 1 : 0;
}
function Ins_EVEN( exc, A ) {
  exc.stack[A] = ( exc.func_round( exc, exc.stack[A], 0 ) & 64 ) === 0 ? 1 : 0;
}

function Ins_AND( exc, A ) { const S = exc.stack; S[A] = ( S[A] && S[A + 1] ) ? 1 : 0; }
function Ins_OR( exc, A )  { const S = exc.stack; S[A] = ( S[A] || S[A + 1] ) ? 1 : 0; }
function Ins_NOT( exc, A ) { const S = exc.stack; S[A] = S[A] ? 0 : 1; }

function Ins_ADD( exc, A ) { const S = exc.stack; S[A] = S[A] + S[A + 1]; }
function Ins_SUB( exc, A ) { const S = exc.stack; S[A] = S[A] - S[A + 1]; }
function Ins_DIV( exc, A ) {
  const S = exc.stack;
  if ( S[A + 1] === 0 ) exc.error = ERR.Divide_By_Zero;
  else S[A] = FT_MulDiv_No_Round( S[A], 64, S[A + 1] );
}
function Ins_MUL( exc, A ) { const S = exc.stack; S[A] = FT_MulDiv( S[A], S[A + 1], 64 ); }
function Ins_ABS( exc, A ) { const S = exc.stack; if ( S[A] < 0 ) S[A] = -S[A]; }
function Ins_NEG( exc, A ) { const S = exc.stack; S[A] = -S[A]; }
function Ins_FLOOR( exc, A ) { const S = exc.stack; S[A] = FT_PIX_FLOOR( S[A] ); }
function Ins_CEILING( exc, A ) { const S = exc.stack; S[A] = FT_PIX_CEIL_LONG( S[A] ); }

function Ins_RS( exc, A ) {
  const S = exc.stack, I = S[A];
  if ( BOUNDSL( I, exc.storeSize ) ) {
    if ( exc.pedantic_hinting ) { exc.error = ERR.Invalid_Reference; return; }
    S[A] = 0;
  }
  else S[A] = exc.storage[I];
}

function Ins_WS( exc, A ) {
  const S = exc.stack, I = S[A];
  if ( BOUNDSL( I, exc.storeSize ) ) {
    if ( exc.pedantic_hinting ) { exc.error = ERR.Invalid_Reference; return; }
  }
  else {
    /* same copy-on-write dance as Modify_CVT_Check, for the storage area */
    if ( exc.iniRange === tt_coderange_glyph && exc.storage !== exc.glyfStorage ) {
      if ( !exc.glyfStorage || exc.glyfStoreSize !== exc.storeSize ) {
        exc.glyfStorage = new Float64Array( exc.storeSize );
        exc.glyfStoreSize = exc.storeSize;
      }
      exc.glyfStorage.set( exc.storage.subarray( 0, exc.storeSize ) );
      exc.storage = exc.glyfStorage;
    }
    exc.storage[I] = S[A + 1];
  }
}

function Ins_WCVTP( exc, A ) {
  const S = exc.stack, I = S[A];
  if ( BOUNDSL( I, exc.cvtSize ) ) {
    if ( exc.pedantic_hinting ) { exc.error = ERR.Invalid_Reference; return; }
  }
  else exc.func_write_cvt( exc, I, S[A + 1] );
}

function Ins_WCVTF( exc, A ) {
  const S = exc.stack, I = S[A];
  if ( BOUNDSL( I, exc.cvtSize ) ) {
    if ( exc.pedantic_hinting ) { exc.error = ERR.Invalid_Reference; return; }
  }
  /* NOTE: unlike WCVTP this writes exc->cvt directly, bypassing the
   * copy-on-write check.  Faithful to ttinterp.c. */
  else exc.cvt[I] = FT_MulFix( S[A + 1], exc.tt_metrics.scale );
}

function Ins_RCVT( exc, A ) {
  const S = exc.stack, I = S[A];
  if ( BOUNDSL( I, exc.cvtSize ) ) {
    if ( exc.pedantic_hinting ) { exc.error = ERR.Invalid_Reference; return; }
    S[A] = 0;
  }
  else S[A] = exc.func_read_cvt( exc, I );
}

function Ins_AA() {}      /* intentionally no longer supported */
function Ins_SANGW() {}   /* instruction not supported anymore */
function Ins_DEBUG( exc ) { exc.error = ERR.Debug_OpCode; }

function Ins_ROUND( exc, A ) {
  exc.stack[A] = exc.func_round( exc, exc.stack[A], exc.GS.compensation[exc.opcode & 3] );
}
function Ins_NROUND( exc, A ) {
  exc.stack[A] = Round_None( exc, exc.stack[A], exc.GS.compensation[exc.opcode & 3] );
}

function Ins_MAX( exc, A ) { const S = exc.stack; if ( S[A + 1] > S[A] ) S[A] = S[A + 1]; }
function Ins_MIN( exc, A ) { const S = exc.stack; if ( S[A + 1] < S[A] ) S[A] = S[A + 1]; }

function Ins_MINDEX( exc, A ) {
  const S = exc.stack, L = S[A];
  if ( L <= 0 || L > exc.args ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
  }
  else {
    const K = S[A - L];
    /* FT_ARRAY_MOVE( args - L, args - L + 1, L - 1 ) == memmove */
    S.copyWithin( A - L, A - L + 1, A );
    S[A - 1] = K;
  }
}

function Ins_CINDEX( exc, A ) {
  const S = exc.stack, L = S[A];
  if ( L <= 0 || L > exc.args ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    S[A] = 0;
  }
  else S[A] = S[A - L];
}

function Ins_ROLL( exc, A ) {
  const S = exc.stack;
  const a = S[A + 2], b = S[A + 1], c = S[A];
  S[A + 2] = c; S[A + 1] = a; S[A] = b;
}

function Ins_SLOOP( exc, A ) {
  const v = exc.stack[A];
  if ( v < 0 ) exc.error = ERR.Bad_Argument;
  else exc.GS.loop = v > 0xFFFF ? 0xFFFF : v;   /* heuristic 16-bit clamp */
}

function SkipCode( exc ) {
  exc.IP += exc.length;
  if ( exc.IP < exc.codeSize ) {
    exc.opcode = exc.code[exc.IP];
    exc.length = opcode_length[exc.opcode];
    if ( exc.length < 0 ) {
      if ( exc.IP + 1 >= exc.codeSize ) { exc.error = ERR.Code_Overflow; return FAILURE; }
      exc.length = 2 - exc.length * exc.code[exc.IP + 1];
    }
    return SUCCESS;
  }
  exc.error = ERR.Code_Overflow;
  return FAILURE;
}

function Ins_IF( exc, A ) {
  if ( exc.stack[A] !== 0 ) return;
  let nIfs = 1, Out = 0;
  do {
    if ( SkipCode( exc ) === FAILURE ) return;
    switch ( exc.opcode ) {
    case 0x58: nIfs++; break;
    case 0x1B: Out = ( nIfs === 1 ) ? 1 : 0; break;
    case 0x59: nIfs--; Out = ( nIfs === 0 ) ? 1 : 0; break;
    default: break;
    }
  } while ( Out === 0 );
}

function Ins_ELSE( exc ) {
  let nIfs = 1;
  do {
    if ( SkipCode( exc ) === FAILURE ) return;
    switch ( exc.opcode ) {
    case 0x58: nIfs++; break;
    case 0x59: nIfs--; break;
    default: break;
    }
  } while ( nIfs !== 0 );
}

function Ins_EIF() {}

function Ins_JMPR( exc, A ) {
  const S = exc.stack;
  if ( S[A] === 0 && exc.args === 0 ) { exc.error = ERR.Bad_Argument; return; }

  exc.IP = exc.IP + S[A];
  if ( exc.IP < 0 ||
       ( exc.callTop > 0 && exc.IP > exc.callStack[exc.callTop - 1].Def.end ) ) {
    exc.error = ERR.Bad_Argument;
    return;
  }

  exc.length = 0;

  if ( S[A] < 0 ) {
    if ( ++exc.neg_jump_counter > exc.neg_jump_counter_max )
      exc.error = ERR.Execution_Too_Long;
  }
}

function Ins_JROT( exc, A ) { if ( exc.stack[A + 1] !== 0 ) Ins_JMPR( exc, A ); }
function Ins_JROF( exc, A ) { if ( exc.stack[A + 1] === 0 ) Ins_JMPR( exc, A ); }

function Ins_FDEF( exc, A ) {
  /* FDEF is only allowed in `prep' or `fpgm' */
  if ( exc.iniRange === tt_coderange_glyph ) { exc.error = ERR.DEF_In_Glyf_Bytecode; return; }

  const n = exc.stack[A];
  let idx = -1;
  for ( let i = 0; i < exc.numFDefs; i++ ) {
    if ( exc.FDefs[i].opc === n ) { idx = i; break; }
  }

  if ( idx < 0 ) {
    /* some font programs are broken enough to redefine functions! */
    if ( exc.numFDefs >= exc.maxFDefs ) { exc.error = ERR.Too_Many_Function_Defs; return; }
    idx = exc.numFDefs;
    exc.numFDefs++;
  }

  /* func # must be within unsigned 16-bit integer */
  if ( n > 0xFFFF ) { exc.error = ERR.Too_Many_Function_Defs; return; }

  const rec = exc.FDefs[idx];
  rec.range  = exc.curRange;
  rec.opc    = toU16( n );
  rec.start  = exc.IP + 1;
  rec.active = true;

  if ( n > exc.maxFunc ) exc.maxFunc = toU16( n );

  /* skip the whole function definition; no nested IDEFs & FDEFs */
  while ( SkipCode( exc ) === SUCCESS ) {
    switch ( exc.opcode ) {
    case 0x89:
    case 0x2C: exc.error = ERR.Nested_DEFS; return;
    case 0x2D: rec.end = exc.IP; return;
    default: break;
    }
  }
}

function Ins_ENDF( exc ) {
  if ( exc.callTop <= 0 ) { exc.error = ERR.ENDF_In_Exec_Stream; return; }

  exc.callTop--;
  const pRec = exc.callStack[exc.callTop];
  pRec.Cur_Count--;

  if ( pRec.Cur_Count > 0 ) {
    exc.callTop++;
    exc.IP     = pRec.Def.start;
    exc.length = 0;
  }
  else
    /* NOTE: the return address of a trailing CALL is legitimately out of
     * range, which is why the result is deliberately not tested. */
    Ins_Goto_CodeRange( exc, pRec.Caller_Range, pRec.Caller_IP );
}

function lookupFDef( exc, F ) {
  /* Normally maxFunc+1 == numFDefs and FDefs[n].opc == n, so index directly;
   * otherwise fall back to a scan.  (C indexes first and relies on the
   * short-circuit to avoid the out-of-range read.) */
  if ( exc.maxFunc + 1 === exc.numFDefs && F < exc.numFDefs && exc.FDefs[F].opc === F )
    return exc.FDefs[F];
  for ( let i = 0; i < exc.numFDefs; i++ )
    if ( exc.FDefs[i].opc === F ) return exc.FDefs[i];
  return null;
}

function Ins_CALL( exc, A ) {
  const F = exc.stack[A];
  if ( BOUNDSL( F, exc.maxFunc + 1 ) ) { exc.error = ERR.Invalid_Reference; return; }

  const def = lookupFDef( exc, F );
  if ( !def || !def.active ) { exc.error = ERR.Invalid_Reference; return; }

  if ( exc.callTop >= exc.callSize ) { exc.error = ERR.Stack_Overflow; return; }

  const pCrec = exc.callStack[exc.callTop];
  pCrec.Caller_Range = exc.curRange;
  pCrec.Caller_IP    = exc.IP + 1;
  pCrec.Cur_Count    = 1;
  pCrec.Def          = def;

  exc.callTop++;
  Ins_Goto_CodeRange( exc, def.range, def.start );
}

function Ins_LOOPCALL( exc, A ) {
  const S = exc.stack;
  const F = S[A + 1];
  if ( BOUNDSL( F, exc.maxFunc + 1 ) ) { exc.error = ERR.Invalid_Reference; return; }

  const def = lookupFDef( exc, F );
  if ( !def || !def.active ) { exc.error = ERR.Invalid_Reference; return; }

  if ( exc.callTop >= exc.callSize ) { exc.error = ERR.Stack_Overflow; return; }

  if ( S[A] > 0 ) {
    const pCrec = exc.callStack[exc.callTop];
    pCrec.Caller_Range = exc.curRange;
    pCrec.Caller_IP    = exc.IP + 1;
    pCrec.Cur_Count    = S[A] | 0;     /* (FT_Int)args[0] */
    pCrec.Def          = def;

    exc.callTop++;
    Ins_Goto_CodeRange( exc, def.range, def.start );

    exc.loopcall_counter += S[A];
    if ( exc.loopcall_counter > exc.loopcall_counter_max )
      exc.error = ERR.Execution_Too_Long;
  }
}

function Ins_IDEF( exc, A ) {
  if ( exc.iniRange === tt_coderange_glyph ) { exc.error = ERR.DEF_In_Glyf_Bytecode; return; }

  const v = exc.stack[A];
  let idx = -1;
  for ( let i = 0; i < exc.numIDefs; i++ )
    if ( exc.IDefs[i].opc === v ) { idx = i; break; }

  if ( idx < 0 ) {
    if ( exc.numIDefs >= exc.maxIDefs ) { exc.error = ERR.Too_Many_Instruction_Defs; return; }
    idx = exc.numIDefs;
    exc.numIDefs++;
  }

  /* opcode must be unsigned 8-bit integer */
  if ( v < 0 || v > 0x00FF ) { exc.error = ERR.Too_Many_Instruction_Defs; return; }

  const def = exc.IDefs[idx];
  def.opc    = v & 0xFF;
  def.start  = exc.IP + 1;
  def.range  = exc.curRange;
  def.active = true;

  if ( v > exc.maxIns ) exc.maxIns = v & 0xFF;

  while ( SkipCode( exc ) === SUCCESS ) {
    switch ( exc.opcode ) {
    case 0x89:
    case 0x2C: exc.error = ERR.Nested_DEFS; return;
    case 0x2D: def.end = exc.IP; return;
    default: break;
    }
  }
}

function Ins_NPUSHB( exc, A ) {
  const S = exc.stack;
  let IP = exc.IP;
  if ( ++IP >= exc.codeSize ) { exc.error = ERR.Code_Overflow; return; }
  const L = exc.code[IP];
  if ( IP + L >= exc.codeSize ) { exc.error = ERR.Code_Overflow; return; }
  if ( BOUNDS( L, exc.stackSize + 1 - exc.top ) ) { exc.error = ERR.Stack_Overflow; return; }
  for ( let K = 0; K < L; K++ ) S[A + K] = exc.code[++IP];
  exc.new_top += L;
  exc.IP = IP;
}

function Ins_NPUSHW( exc, A ) {
  const S = exc.stack;
  let IP = exc.IP;
  if ( ++IP >= exc.codeSize ) { exc.error = ERR.Code_Overflow; return; }
  const L = exc.code[IP];
  if ( IP + 2 * L >= exc.codeSize ) { exc.error = ERR.Code_Overflow; return; }
  if ( BOUNDS( L, exc.stackSize + 1 - exc.top ) ) { exc.error = ERR.Stack_Overflow; return; }
  /* (FT_Short)(hi << 8) | lo -- note the cast is for sign-extension */
  for ( let K = 0; K < L; K++, IP += 2 )
    S[A + K] = toS16( ( exc.code[IP + 1] << 8 ) | exc.code[IP + 2] );
  exc.new_top += L;
  exc.IP = IP;
}

function Ins_PUSHB( exc, A ) {
  const S = exc.stack;
  let IP = exc.IP;
  const L = exc.opcode - 0xB0 + 1;
  if ( IP + L >= exc.codeSize ) { exc.error = ERR.Code_Overflow; return; }
  if ( BOUNDS( L, exc.stackSize + 1 - exc.top ) ) { exc.error = ERR.Stack_Overflow; return; }
  for ( let K = 0; K < L; K++ ) S[A + K] = exc.code[++IP];
  exc.IP = IP;
}

function Ins_PUSHW( exc, A ) {
  const S = exc.stack;
  let IP = exc.IP;
  const L = exc.opcode - 0xB8 + 1;
  if ( IP + 2 * L >= exc.codeSize ) { exc.error = ERR.Code_Overflow; return; }
  if ( BOUNDS( L, exc.stackSize + 1 - exc.top ) ) { exc.error = ERR.Stack_Overflow; return; }
  for ( let K = 0; K < L; K++, IP += 2 )
    S[A + K] = toS16( ( exc.code[IP + 1] << 8 ) | exc.code[IP + 2] );
  exc.IP = IP;
}

function Ins_SxVTL( exc, aIdx1, aIdx2, Vec ) {
  let opcode = exc.opcode;

  if ( BOUNDS( aIdx1, exc.zp2.n_points ) || BOUNDS( aIdx2, exc.zp1.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return FAILURE;
  }

  let A = exc.zp1.curX[aIdx2] - exc.zp2.curX[aIdx1];
  let B = exc.zp1.curY[aIdx2] - exc.zp2.curY[aIdx1];

  /* If p1 == p2, SPvTL and SFvTL behave like SPvTCA[X]/SFvTCA[X]. */
  if ( A === 0 && B === 0 ) { A = 0x4000; opcode = 0; }

  if ( ( opcode & 1 ) !== 0 ) {
    const C = B;   /* counter-clockwise rotation */
    B = A;
    A = -C;
  }

  Normalize( A, B, Vec );
  return SUCCESS;
}

function Ins_SxyTCA( exc ) {
  const opcode = exc.opcode;
  const AA = toS16( ( opcode & 1 ) << 14 );
  const BB = toS16( AA ^ 0x4000 );

  if ( opcode < 4 ) {
    exc.GS.projVector.x = AA; exc.GS.projVector.y = BB;
    exc.GS.dualVector.x = AA; exc.GS.dualVector.y = BB;
  }
  if ( ( opcode & 2 ) === 0 ) {
    exc.GS.freeVector.x = AA; exc.GS.freeVector.y = BB;
  }
  Compute_Funcs( exc );
}

function Ins_SPVTL( exc, A ) {
  const S = exc.stack;
  if ( Ins_SxVTL( exc, toU16( S[A + 1] ), toU16( S[A] ), exc.GS.projVector ) === SUCCESS ) {
    exc.GS.dualVector.x = exc.GS.projVector.x;
    exc.GS.dualVector.y = exc.GS.projVector.y;
    Compute_Funcs( exc );
  }
}

function Ins_SFVTL( exc, A ) {
  const S = exc.stack;
  if ( Ins_SxVTL( exc, toU16( S[A + 1] ), toU16( S[A] ), exc.GS.freeVector ) === SUCCESS )
    Compute_Funcs( exc );
}

function Ins_SFVTPV( exc ) {
  exc.GS.freeVector.x = exc.GS.projVector.x;
  exc.GS.freeVector.y = exc.GS.projVector.y;
  Compute_Funcs( exc );
}

function Ins_SPVFS( exc, A ) {
  const S = exc.stack;
  /* Only use low 16 bits, then sign extend */
  const Y = toS16( S[A + 1] ), X = toS16( S[A] );
  Normalize( X, Y, exc.GS.projVector );
  exc.GS.dualVector.x = exc.GS.projVector.x;
  exc.GS.dualVector.y = exc.GS.projVector.y;
  Compute_Funcs( exc );
}

function Ins_SFVFS( exc, A ) {
  const S = exc.stack;
  const Y = toS16( S[A + 1] ), X = toS16( S[A] );
  Normalize( X, Y, exc.GS.freeVector );
  Compute_Funcs( exc );
}

function Ins_GPV( exc, A ) {
  exc.stack[A] = exc.GS.projVector.x; exc.stack[A + 1] = exc.GS.projVector.y;
}
function Ins_GFV( exc, A ) {
  exc.stack[A] = exc.GS.freeVector.x; exc.stack[A + 1] = exc.GS.freeVector.y;
}

function Ins_SRP0( exc, A ) { exc.GS.rp0 = toU16( exc.stack[A] ); }
function Ins_SRP1( exc, A ) { exc.GS.rp1 = toU16( exc.stack[A] ); }
function Ins_SRP2( exc, A ) { exc.GS.rp2 = toU16( exc.stack[A] ); }

function Ins_SMD( exc, A )    { exc.GS.minimum_distance = exc.stack[A]; }
function Ins_SCVTCI( exc, A ) { exc.GS.control_value_cutin = exc.stack[A]; }
function Ins_SSWCI( exc, A )  { exc.GS.single_width_cutin = exc.stack[A]; }
function Ins_SSW( exc, A ) {
  exc.GS.single_width_value = FT_MulFix( exc.stack[A], exc.tt_metrics.scale );
}

function Ins_FLIPON( exc )  { exc.GS.auto_flip = true; }
function Ins_FLIPOFF( exc ) { exc.GS.auto_flip = false; }

function Ins_SDB( exc, A ) { exc.GS.delta_base = toU16( exc.stack[A] ); }
function Ins_SDS( exc, A ) {
  const v = exc.stack[A];
  /* (FT_ULong)args[0] > 6UL -- negative becomes huge, hence an error */
  if ( v < 0 || v > 6 ) exc.error = ERR.Bad_Argument;
  else exc.GS.delta_shift = toU16( v );
}

function setRound( exc, state ) {
  exc.GS.round_state = state;
  exc.func_round = ROUND_FUNCS[state];
}
function Ins_RTHG( exc ) { setRound( exc, TT_Round_To_Half_Grid ); }
function Ins_RTG( exc )  { setRound( exc, TT_Round_To_Grid ); }
function Ins_RTDG( exc ) { setRound( exc, TT_Round_To_Double_Grid ); }
function Ins_RUTG( exc ) { setRound( exc, TT_Round_Up_To_Grid ); }
function Ins_RDTG( exc ) { setRound( exc, TT_Round_Down_To_Grid ); }
function Ins_ROFF( exc ) { setRound( exc, TT_Round_Off ); }

function Ins_SROUND( exc, A ) {
  SetSuperRound( exc, 0x4000, exc.stack[A] );
  setRound( exc, TT_Round_Super );
}
function Ins_S45ROUND( exc, A ) {
  SetSuperRound( exc, 0x2D41, exc.stack[A] );
  setRound( exc, TT_Round_Super_45 );
}

/* XXX UNDOCUMENTED: measures from the original glyph must be taken along the
 * dual projection vector! */
function Ins_GC( exc, A ) {
  const S = exc.stack, L = S[A];
  let R;
  if ( BOUNDSL( L, exc.zp2.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    R = 0;
  }
  else {
    if ( exc.opcode & 1 ) R = exc.func_dualproj( exc, exc.zp2.orgX[L], exc.zp2.orgY[L] );
    else R = exc.func_project( exc, exc.zp2.curX[L], exc.zp2.curY[L] );
  }
  S[A] = R;
}

function Ins_SCFS( exc, A ) {
  const S = exc.stack, L = toU16( S[A] );
  if ( BOUNDS( L, exc.zp2.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }
  const K = exc.func_project( exc, exc.zp2.curX[L], exc.zp2.curY[L] );
  exc.func_move( exc, exc.zp2, L, S[A + 1] - K );

  /* UNDOCUMENTED!  The MS rasterizer does that with twilight points */
  if ( exc.GS.gep2 === 0 ) {
    exc.zp2.orgX[L] = exc.zp2.curX[L];
    exc.zp2.orgY[L] = exc.zp2.curY[L];
  }
}

/* XXX UNDOCUMENTED: flag attributes are inverted, and it is `zp0 - zp1'. */
function Ins_MD( exc, A ) {
  const S = exc.stack;
  const K = toU16( S[A + 1] ), L = toU16( S[A] );
  let D;

  if ( BOUNDS( L, exc.zp0.n_points ) || BOUNDS( K, exc.zp1.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    D = 0;
  }
  else if ( exc.opcode & 1 ) {
    D = PROJ( exc, exc.zp0.curX[L], exc.zp0.curY[L], exc.zp1.curX[K], exc.zp1.curY[K] );
  }
  else {
    /* XXX UNDOCUMENTED: twilight zone special case */
    if ( exc.GS.gep0 === 0 || exc.GS.gep1 === 0 ) {
      D = DUALPROJ( exc, exc.zp0.orgX[L], exc.zp0.orgY[L], exc.zp1.orgX[K], exc.zp1.orgY[K] );
    }
    else if ( exc.metrics.x_scale === exc.metrics.y_scale ) {
      D = DUALPROJ( exc, exc.zp0.orusX[L], exc.zp0.orusY[L], exc.zp1.orusX[K], exc.zp1.orusY[K] );
      D = FT_MulFix( D, exc.metrics.x_scale );
    }
    else {
      /* NOTE: plain `-', not SUB_LONG, in the C source */
      const vx = FT_MulFix( exc.zp0.orusX[L] - exc.zp1.orusX[K], exc.metrics.x_scale );
      const vy = FT_MulFix( exc.zp0.orusY[L] - exc.zp1.orusY[K], exc.metrics.y_scale );
      D = exc.func_dualproj( exc, vx, vy );
    }
  }
  S[A] = D;
}

function Ins_SDPVTL( exc, Aidx ) {
  const S = exc.stack;
  let opcode = exc.opcode;
  const p1 = toU16( S[Aidx + 1] ), p2 = toU16( S[Aidx] );

  if ( BOUNDS( p2, exc.zp1.n_points ) || BOUNDS( p1, exc.zp2.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }

  let A = exc.zp1.orgX[p2] - exc.zp2.orgX[p1];
  let B = exc.zp1.orgY[p2] - exc.zp2.orgY[p1];
  /* If v1 == v2, SDPvTL behaves the same as SVTCA[X]. */
  if ( A === 0 && B === 0 ) { A = 0x4000; opcode = 0; }

  if ( ( opcode & 1 ) !== 0 ) { const C = B; B = A; A = -C; }
  Normalize( A, B, exc.GS.dualVector );

  A = exc.zp1.curX[p2] - exc.zp2.curX[p1];
  B = exc.zp1.curY[p2] - exc.zp2.curY[p1];
  if ( A === 0 && B === 0 ) { A = 0x4000; opcode = 0; }

  if ( ( opcode & 1 ) !== 0 ) { const C = B; B = A; A = -C; }
  Normalize( A, B, exc.GS.projVector );

  Compute_Funcs( exc );
}

function Ins_SZP0( exc, A ) {
  switch ( exc.stack[A] | 0 ) {
  case 0: exc.zp0 = exc.twilight; break;
  case 1: exc.zp0 = exc.pts; break;
  default:
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }
  exc.GS.gep0 = toU16( exc.stack[A] );
}

function Ins_SZP1( exc, A ) {
  switch ( exc.stack[A] | 0 ) {
  case 0: exc.zp1 = exc.twilight; break;
  case 1: exc.zp1 = exc.pts; break;
  default:
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }
  exc.GS.gep1 = toU16( exc.stack[A] );
}

function Ins_SZP2( exc, A ) {
  switch ( exc.stack[A] | 0 ) {
  case 0: exc.zp2 = exc.twilight; break;
  case 1: exc.zp2 = exc.pts; break;
  default:
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }
  exc.GS.gep2 = toU16( exc.stack[A] );
}

function Ins_SZPS( exc, A ) {
  switch ( exc.stack[A] | 0 ) {
  case 0: exc.zp0 = exc.twilight; break;
  case 1: exc.zp0 = exc.pts; break;
  default:
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }
  exc.zp1 = exc.zp0;
  exc.zp2 = exc.zp0;
  const v = toU16( exc.stack[A] );
  exc.GS.gep0 = v; exc.GS.gep1 = v; exc.GS.gep2 = v;
}

function Ins_INSTCTRL( exc, A ) {
  const S = exc.stack;
  const K = S[A + 1], L = S[A];

  /* selector values cannot be OR'ed; they are indices starting with 1 */
  if ( K < 1 || K > 3 ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }

  const Kf = 1 << ( K - 1 );

  if ( L !== 0 && L !== Kf ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }

  /* INSTCTRL should only be used in the CVT program */
  if ( exc.iniRange === tt_coderange_cvt ) {
    exc.GS.instruct_control &= ~Kf & 0xFF;
    exc.GS.instruct_control |= L & 0xFF;
  }
  /* v35: the `K == 3' glyph-range escape hatch only exists to toggle the v40
   * subpixel flags, so here it is simply a no-op (not an error). */
  else if ( exc.iniRange === tt_coderange_glyph && K === 3 ) {
    /* nothing */
  }
  else if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
}

function Ins_SCANCTRL( exc, A ) {
  const v = exc.stack[A];
  const th = v & 0xFF;            /* (FT_Int)( args[0] & 0xFF ) */

  if ( th === 0xFF ) { exc.GS.scan_control = true; return; }
  else if ( th === 0 ) { exc.GS.scan_control = false; return; }

  if ( ( v & 0x100 ) !== 0 && exc.tt_metrics.ppem <= th ) exc.GS.scan_control = true;
  if ( ( v & 0x200 ) !== 0 && exc.tt_metrics.rotated ) exc.GS.scan_control = true;
  if ( ( v & 0x400 ) !== 0 && exc.tt_metrics.stretched ) exc.GS.scan_control = true;
  if ( ( v & 0x800 ) !== 0 && exc.tt_metrics.ppem > th ) exc.GS.scan_control = false;
  if ( ( v & 0x1000 ) !== 0 && exc.tt_metrics.rotated ) exc.GS.scan_control = false;
  if ( ( v & 0x2000 ) !== 0 && exc.tt_metrics.stretched ) exc.GS.scan_control = false;
}

function Ins_SCANTYPE( exc, A ) {
  if ( exc.stack[A] >= 0 ) exc.GS.scan_type = exc.stack[A] & 0xFFFF;
}

function Ins_FLIPPT( exc, A ) {
  const S = exc.stack;
  let loop = exc.GS.loop;

  if ( exc.new_top < loop ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Too_Few_Arguments;
    exc.GS.loop = 1;
    return;
  }
  exc.new_top -= loop;

  let p = A;
  while ( loop-- ) {
    const point = toU16( S[--p] );
    if ( BOUNDS( point, exc.pts.n_points ) ) {
      if ( exc.pedantic_hinting ) { exc.error = ERR.Invalid_Reference; return; }
    }
    else exc.pts.tags[point] ^= FT_CURVE_TAG_ON;
  }
  exc.GS.loop = 1;
}

function Ins_FLIPRGON( exc, A ) {
  const S = exc.stack;
  const K = toU16( S[A + 1] ), L = toU16( S[A] );
  if ( BOUNDS( K, exc.pts.n_points ) || BOUNDS( L, exc.pts.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }
  for ( let I = L; I <= K; I++ ) exc.pts.tags[I] |= FT_CURVE_TAG_ON;
}

function Ins_FLIPRGOFF( exc, A ) {
  const S = exc.stack;
  const K = toU16( S[A + 1] ), L = toU16( S[A] );
  if ( BOUNDS( K, exc.pts.n_points ) || BOUNDS( L, exc.pts.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }
  for ( let I = L; I <= K; I++ ) exc.pts.tags[I] &= ~FT_CURVE_TAG_ON;
}

/* returns a shared record {ok, x, y, zone, refp} */
const _disp = { ok: false, x: 0, y: 0, zone: null, refp: 0 };
function Compute_Point_Displacement( exc ) {
  let zp, p;
  if ( exc.opcode & 1 ) { zp = exc.zp0; p = exc.GS.rp1; }
  else { zp = exc.zp1; p = exc.GS.rp2; }

  if ( BOUNDS( p, zp.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    _disp.ok = false; _disp.refp = 0;
    return _disp;
  }

  const d = PROJ( exc, zp.curX[p], zp.curY[p], zp.orgX[p], zp.orgY[p] );

  _disp.ok = true;
  _disp.zone = zp;
  _disp.refp = p;
  _disp.x = FT_MulFix( d, exc.moveVector.x );
  _disp.y = FT_MulFix( d, exc.moveVector.y );
  return _disp;
}

function Move_Zp2_Point( exc, point, dx, dy, touch ) {
  if ( exc.GS.freeVector.x !== 0 ) {
    exc.zp2.curX[point] = exc.zp2.curX[point] + dx;
    if ( touch ) exc.zp2.tags[point] |= FT_CURVE_TAG_TOUCH_X;
  }
  if ( exc.GS.freeVector.y !== 0 ) {
    exc.zp2.curY[point] = exc.zp2.curY[point] + dy;
    if ( touch ) exc.zp2.tags[point] |= FT_CURVE_TAG_TOUCH_Y;
  }
}

function Ins_SHP( exc, A ) {
  const S = exc.stack;
  let loop = exc.GS.loop;

  if ( exc.new_top < loop ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Too_Few_Arguments;
    exc.GS.loop = 1;
    return;
  }
  exc.new_top -= loop;

  const d = Compute_Point_Displacement( exc );
  if ( !d.ok ) return;
  const dx = d.x, dy = d.y;

  let p = A;
  while ( loop-- ) {
    const point = toU16( S[--p] );
    if ( BOUNDS( point, exc.zp2.n_points ) ) {
      if ( exc.pedantic_hinting ) { exc.error = ERR.Invalid_Reference; return; }
    }
    else Move_Zp2_Point( exc, point, dx, dy, true );
  }
  exc.GS.loop = 1;
}

/* UNDOCUMENTED: the twilight zone has one virtual contour (number zero)
 * containing all of its points. */
function Ins_SHC( exc, A ) {
  const contour = toU16( exc.stack[A] );
  const bounds = ( exc.GS.gep2 === 0 ) ? 1 : exc.zp2.n_contours;

  if ( BOUNDS( contour, bounds ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }

  const d = Compute_Point_Displacement( exc );
  if ( !d.ok ) return;
  const dx = d.x, dy = d.y, zp = d.zone, refp = d.refp;

  let start;
  if ( contour === 0 ) start = 0;
  else start = toU16( exc.zp2.contours[contour - 1] + 1 - exc.zp2.first_point );

  /* we use the number of points if in the twilight zone */
  let limit;
  if ( exc.GS.gep2 === 0 ) limit = exc.zp2.n_points;
  else limit = toU16( exc.zp2.contours[contour] + 1 - exc.zp2.first_point );

  for ( let i = start; i < limit; i++ ) {
    /* C compares the FT_Vector pointers; our `curX' views compare the same */
    if ( zp.curX !== exc.zp2.curX || refp !== i )
      Move_Zp2_Point( exc, i, dx, dy, true );
  }
}

function Ins_SHZ( exc, A ) {
  if ( BOUNDS( exc.stack[A], 2 ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }

  const d = Compute_Point_Displacement( exc );
  if ( !d.ok ) return;
  const dx = d.x, dy = d.y, zp = d.zone, refp = d.refp;

  /* XXX UNDOCUMENTED! SHZ doesn't move the phantom points.  The twilight
   * zone has no real contours, so use `n_points'; the normal zone's
   * `n_points' includes phantoms, so use the end of the last contour.
   * NOTE: no `- first_point' here -- faithful to ttinterp.c. */
  let limit;
  if ( exc.GS.gep2 === 0 ) limit = exc.zp2.n_points;
  else if ( exc.GS.gep2 === 1 && exc.zp2.n_contours > 0 )
    limit = toU16( exc.zp2.contours[exc.zp2.n_contours - 1] + 1 );
  else limit = 0;

  /* XXX UNDOCUMENTED! SHZ doesn't touch the points */
  for ( let i = 0; i < limit; i++ ) {
    if ( zp.curX !== exc.zp2.curX || refp !== i )
      Move_Zp2_Point( exc, i, dx, dy, false );
  }
}

function Ins_SHPIX( exc, A ) {
  const S = exc.stack;
  let loop = exc.GS.loop;

  if ( exc.new_top < loop ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Too_Few_Arguments;
    exc.GS.loop = 1;
    return;
  }
  exc.new_top -= loop;

  const dx = TT_MulFix14( S[A], exc.GS.freeVector.x );
  const dy = TT_MulFix14( S[A], exc.GS.freeVector.y );

  let p = A;
  while ( loop-- ) {
    const point = toU16( S[--p] );
    if ( BOUNDS( point, exc.zp2.n_points ) ) {
      if ( exc.pedantic_hinting ) { exc.error = ERR.Invalid_Reference; return; }
    }
    /* v35: no backward-compatibility gate -- always move on both axes. */
    else Move_Zp2_Point( exc, point, dx, dy, true );
  }
  exc.GS.loop = 1;
}

function Ins_MSIRP( exc, A ) {
  const S = exc.stack;
  const point = toU16( S[A] );

  if ( BOUNDS( point, exc.zp1.n_points ) || BOUNDS( exc.GS.rp0, exc.zp0.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }

  /* UNDOCUMENTED!  The MS rasterizer does that with twilight points */
  if ( exc.GS.gep1 === 0 ) {
    exc.zp1.orgX[point] = exc.zp0.orgX[exc.GS.rp0];
    exc.zp1.orgY[point] = exc.zp0.orgY[exc.GS.rp0];
    exc.func_move_orig( exc, exc.zp1, point, S[A + 1] );
    exc.zp1.curX[point] = exc.zp1.orgX[point];
    exc.zp1.curY[point] = exc.zp1.orgY[point];
  }

  const distance = PROJ( exc, exc.zp1.curX[point], exc.zp1.curY[point],
                              exc.zp0.curX[exc.GS.rp0], exc.zp0.curY[exc.GS.rp0] );

  exc.func_move( exc, exc.zp1, point, S[A + 1] - distance );

  exc.GS.rp1 = exc.GS.rp0;
  exc.GS.rp2 = point;
  if ( ( exc.opcode & 1 ) !== 0 ) exc.GS.rp0 = point;
}

function Ins_MDAP( exc, A ) {
  const point = toU16( exc.stack[A] );

  if ( BOUNDS( point, exc.zp0.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }

  let distance;
  if ( ( exc.opcode & 1 ) !== 0 ) {
    const cur_dist = exc.func_project( exc, exc.zp0.curX[point], exc.zp0.curY[point] );
    distance = exc.func_round( exc, cur_dist, 0 ) - cur_dist;
  }
  else distance = 0;

  exc.func_move( exc, exc.zp0, point, distance );

  exc.GS.rp0 = point;
  exc.GS.rp1 = point;
}

function Ins_MIAP( exc, A ) {
  const S = exc.stack;
  const cvtEntry = S[A + 1];
  const point = toU16( S[A] );

  if ( BOUNDS( point, exc.zp0.n_points ) || BOUNDSL( cvtEntry, exc.cvtSize ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    exc.GS.rp0 = point; exc.GS.rp1 = point;
    return;
  }

  /* UNDOCUMENTED!  MIAP behaves very differently in the twilight zone: no
   * control value cut-in test, and the original point is set to the
   * absolute, unrounded CVT distance.  Confirmed by Greg Hitchcock. */
  let distance = exc.func_read_cvt( exc, cvtEntry );

  if ( exc.GS.gep0 === 0 ) {
    exc.zp0.orgX[point] = TT_MulFix14( distance, exc.GS.freeVector.x );
    exc.zp0.orgY[point] = TT_MulFix14( distance, exc.GS.freeVector.y );
    exc.zp0.curX[point] = exc.zp0.orgX[point];
    exc.zp0.curY[point] = exc.zp0.orgY[point];
  }

  const org_dist = exc.func_project( exc, exc.zp0.curX[point], exc.zp0.curY[point] );

  if ( ( exc.opcode & 1 ) !== 0 ) {   /* rounding and control cut-in flag */
    let delta = distance - org_dist;
    if ( delta < 0 ) delta = -delta;
    if ( delta > exc.GS.control_value_cutin ) distance = org_dist;
    distance = exc.func_round( exc, distance, 0 );
  }

  exc.func_move( exc, exc.zp0, point, distance - org_dist );

  exc.GS.rp0 = point;
  exc.GS.rp1 = point;
}

function Ins_MDRP( exc, A ) {
  const point = toU16( exc.stack[A] );

  if ( BOUNDS( point, exc.zp1.n_points ) || BOUNDS( exc.GS.rp0, exc.zp0.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    exc.GS.rp1 = exc.GS.rp0; exc.GS.rp2 = point;
    if ( ( exc.opcode & 16 ) !== 0 ) exc.GS.rp0 = point;
    return;
  }

  let org_dist;
  /* XXX UNDOCUMENTED: twilight zone special case */
  if ( exc.GS.gep0 === 0 || exc.GS.gep1 === 0 ) {
    org_dist = DUALPROJ( exc, exc.zp1.orgX[point], exc.zp1.orgY[point],
                              exc.zp0.orgX[exc.GS.rp0], exc.zp0.orgY[exc.GS.rp0] );
  }
  else if ( exc.metrics.x_scale === exc.metrics.y_scale ) {
    org_dist = DUALPROJ( exc, exc.zp1.orusX[point], exc.zp1.orusY[point],
                              exc.zp0.orusX[exc.GS.rp0], exc.zp0.orusY[exc.GS.rp0] );
    org_dist = FT_MulFix( org_dist, exc.metrics.x_scale );
  }
  else {
    const vx = FT_MulFix( exc.zp1.orusX[point] - exc.zp0.orusX[exc.GS.rp0], exc.metrics.x_scale );
    const vy = FT_MulFix( exc.zp1.orusY[point] - exc.zp0.orusY[exc.GS.rp0], exc.metrics.y_scale );
    org_dist = exc.func_dualproj( exc, vx, vy );
  }

  /* single width cut-in test: |org_dist - single_width_value| < cutin */
  if ( exc.GS.single_width_cutin > 0 &&
       org_dist < exc.GS.single_width_value + exc.GS.single_width_cutin &&
       org_dist > exc.GS.single_width_value - exc.GS.single_width_cutin ) {
    if ( org_dist >= 0 ) org_dist = exc.GS.single_width_value;
    else org_dist = -exc.GS.single_width_value;
  }

  /* round flag */
  const compensation = exc.GS.compensation[exc.opcode & 3];
  let distance;
  if ( ( exc.opcode & 4 ) !== 0 ) distance = exc.func_round( exc, org_dist, compensation );
  else distance = Round_None( exc, org_dist, compensation );

  /* minimum distance flag */
  if ( ( exc.opcode & 8 ) !== 0 ) {
    const md = exc.GS.minimum_distance;
    if ( org_dist >= 0 ) { if ( distance < md ) distance = md; }
    else { if ( distance > -md ) distance = -md; }
  }

  /* now move the point */
  const cur = PROJ( exc, exc.zp1.curX[point], exc.zp1.curY[point],
                         exc.zp0.curX[exc.GS.rp0], exc.zp0.curY[exc.GS.rp0] );

  exc.func_move( exc, exc.zp1, point, distance - cur );

  exc.GS.rp1 = exc.GS.rp0;
  exc.GS.rp2 = point;
  if ( ( exc.opcode & 16 ) !== 0 ) exc.GS.rp0 = point;
}

function Ins_MIRP( exc, A ) {
  const S = exc.stack;
  const point = toU16( S[A] );
  /* XXX UNDOCUMENTED! cvt[-1] = 0 always */
  const cvtEntry = S[A + 1] + 1;

  if ( BOUNDS( point, exc.zp1.n_points ) ||
       BOUNDSL( cvtEntry, exc.cvtSize + 1 ) ||
       BOUNDS( exc.GS.rp0, exc.zp0.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    exc.GS.rp1 = exc.GS.rp0; exc.GS.rp2 = point;
    if ( ( exc.opcode & 16 ) !== 0 ) exc.GS.rp0 = point;
    return;
  }

  let cvt_dist;
  if ( !cvtEntry ) cvt_dist = 0;
  else cvt_dist = exc.func_read_cvt( exc, cvtEntry - 1 );

  /* single width test */
  let delta = cvt_dist - exc.GS.single_width_value;
  if ( delta < 0 ) delta = -delta;
  if ( delta < exc.GS.single_width_cutin ) {
    if ( cvt_dist >= 0 ) cvt_dist = exc.GS.single_width_value;
    else cvt_dist = -exc.GS.single_width_value;
  }

  /* UNDOCUMENTED!  The MS rasterizer does that with twilight points */
  if ( exc.GS.gep1 === 0 ) {
    exc.zp1.orgX[point] = exc.zp0.orgX[exc.GS.rp0] +
                          TT_MulFix14( cvt_dist, exc.GS.freeVector.x );
    exc.zp1.orgY[point] = exc.zp0.orgY[exc.GS.rp0] +
                          TT_MulFix14( cvt_dist, exc.GS.freeVector.y );
    exc.zp1.curX[point] = exc.zp1.orgX[point];
    exc.zp1.curY[point] = exc.zp1.orgY[point];
  }

  const org_dist = DUALPROJ( exc, exc.zp1.orgX[point], exc.zp1.orgY[point],
                                  exc.zp0.orgX[exc.GS.rp0], exc.zp0.orgY[exc.GS.rp0] );
  const cur_dist = PROJ( exc, exc.zp1.curX[point], exc.zp1.curY[point],
                              exc.zp0.curX[exc.GS.rp0], exc.zp0.curY[exc.GS.rp0] );

  /* auto-flip test: the C code uses `(org_dist ^ cvt_dist) < 0', i.e. a
   * pure sign test, which we spell out because JS `^' is 32-bit. */
  if ( exc.GS.auto_flip ) {
    if ( ( org_dist < 0 ) !== ( cvt_dist < 0 ) ) cvt_dist = -cvt_dist;
  }

  /* control value cut-in and round */
  const compensation = exc.GS.compensation[exc.opcode & 3];
  let distance;
  if ( ( exc.opcode & 4 ) !== 0 ) {
    /* XXX UNDOCUMENTED! Only perform cut-in test when both points refer to
     * the same zone.  Greg Hitchcock: strictly `>', not `>='. */
    if ( exc.GS.gep0 === exc.GS.gep1 ) {
      let d2 = cvt_dist - org_dist;
      if ( d2 < 0 ) d2 = -d2;
      if ( d2 > exc.GS.control_value_cutin ) cvt_dist = org_dist;
    }
    distance = exc.func_round( exc, cvt_dist, compensation );
  }
  else distance = Round_None( exc, cvt_dist, compensation );

  /* minimum distance test */
  if ( ( exc.opcode & 8 ) !== 0 ) {
    const md = exc.GS.minimum_distance;
    if ( org_dist >= 0 ) { if ( distance < md ) distance = md; }
    else { if ( distance > -md ) distance = -md; }
  }

  exc.func_move( exc, exc.zp1, point, distance - cur_dist );

  exc.GS.rp1 = exc.GS.rp0;
  exc.GS.rp2 = point;
  if ( ( exc.opcode & 16 ) !== 0 ) exc.GS.rp0 = point;
}

function Ins_ALIGNRP( exc, A ) {
  const S = exc.stack;
  let loop = exc.GS.loop;

  if ( exc.new_top < loop ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Too_Few_Arguments;
    exc.GS.loop = 1;
    return;
  }
  exc.new_top -= loop;

  if ( BOUNDS( exc.GS.rp0, exc.zp0.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    exc.GS.loop = 1;
    return;
  }

  let p = A;
  while ( loop-- ) {
    const point = toU16( S[--p] );
    if ( BOUNDS( point, exc.zp1.n_points ) ) {
      if ( exc.pedantic_hinting ) { exc.error = ERR.Invalid_Reference; return; }
    }
    else {
      const distance = PROJ( exc, exc.zp1.curX[point], exc.zp1.curY[point],
                                  exc.zp0.curX[exc.GS.rp0], exc.zp0.curY[exc.GS.rp0] );
      exc.func_move( exc, exc.zp1, point, -distance );
    }
  }
  exc.GS.loop = 1;
}

function Ins_ISECT( exc, A ) {
  const S = exc.stack;
  const point = toU16( S[A] );
  const a0 = toU16( S[A + 1] ), a1 = toU16( S[A + 2] );
  const b0 = toU16( S[A + 3] ), b1 = toU16( S[A + 4] );

  if ( BOUNDS( b0, exc.zp0.n_points ) || BOUNDS( b1, exc.zp0.n_points ) ||
       BOUNDS( a0, exc.zp1.n_points ) || BOUNDS( a1, exc.zp1.n_points ) ||
       BOUNDS( point, exc.zp2.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }

  /* Cramer's rule */
  const dbx = exc.zp0.curX[b1] - exc.zp0.curX[b0];
  const dby = exc.zp0.curY[b1] - exc.zp0.curY[b0];
  const dax = exc.zp1.curX[a1] - exc.zp1.curX[a0];
  const day = exc.zp1.curY[a1] - exc.zp1.curY[a0];
  const dx  = exc.zp0.curX[b0] - exc.zp1.curX[a0];
  const dy  = exc.zp0.curY[b0] - exc.zp1.curY[a0];

  const discriminant = FT_MulDiv( dax, -dby, 0x40 ) + FT_MulDiv( day, dbx, 0x40 );
  const dotproduct   = FT_MulDiv( dax, dbx, 0x40 ) + FT_MulDiv( day, dby, 0x40 );

  /* reject grazing intersections by thresholding |tan(angle)| at 1/19 */
  if ( 19 * Math.abs( discriminant ) > Math.abs( dotproduct ) ) {
    const val = FT_MulDiv( dx, -dby, 0x40 ) + FT_MulDiv( dy, dbx, 0x40 );
    const Rx = FT_MulDiv( val, dax, discriminant );
    const Ry = FT_MulDiv( val, day, discriminant );
    exc.zp2.curX[point] = exc.zp1.curX[a0] + Rx;
    exc.zp2.curY[point] = exc.zp1.curY[a0] + Ry;
  }
  else {
    /* else, take the middle of the middles of A and B.
     * NOTE: C's `/ 4' truncates toward zero. */
    exc.zp2.curX[point] = cdiv(
      ( exc.zp1.curX[a0] + exc.zp1.curX[a1] ) + ( exc.zp0.curX[b0] + exc.zp0.curX[b1] ), 4 );
    exc.zp2.curY[point] = cdiv(
      ( exc.zp1.curY[a0] + exc.zp1.curY[a1] ) + ( exc.zp0.curY[b0] + exc.zp0.curY[b1] ), 4 );
  }

  exc.zp2.tags[point] |= FT_CURVE_TAG_TOUCH_BOTH;
}

function Ins_ALIGNPTS( exc, A ) {
  const S = exc.stack;
  const p1 = toU16( S[A] ), p2 = toU16( S[A + 1] );

  if ( BOUNDS( p1, exc.zp1.n_points ) || BOUNDS( p2, exc.zp0.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }

  /* C's `/ 2' truncates toward zero */
  const distance = cdiv( PROJ( exc, exc.zp0.curX[p2], exc.zp0.curY[p2],
                                    exc.zp1.curX[p1], exc.zp1.curY[p1] ), 2 );

  exc.func_move( exc, exc.zp1, p1, distance );
  exc.func_move( exc, exc.zp0, p2, -distance );
}

/* SOMETIMES, DUMBER CODE IS BETTER CODE */
function Ins_IP( exc, A ) {
  const S = exc.stack;
  let loop = exc.GS.loop;

  if ( exc.new_top < loop ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Too_Few_Arguments;
    exc.GS.loop = 1;
    return;
  }
  exc.new_top -= loop;

  if ( BOUNDS( exc.GS.rp1, exc.zp0.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    exc.GS.loop = 1;
    return;
  }

  /* The twilight zone needs special handling: exc->twilight.orus[n] is
   * (0,0) for every n by definition. */
  const twilight = ( exc.GS.gep0 === 0 || exc.GS.gep1 === 0 || exc.GS.gep2 === 0 );

  let obX, obY;
  if ( twilight ) { obX = exc.zp0.orgX[exc.GS.rp1]; obY = exc.zp0.orgY[exc.GS.rp1]; }
  else { obX = exc.zp0.orusX[exc.GS.rp1]; obY = exc.zp0.orusY[exc.GS.rp1]; }

  const cbX = exc.zp0.curX[exc.GS.rp1], cbY = exc.zp0.curY[exc.GS.rp1];

  let old_range, cur_range;
  /* XXX braindead but popular fonts call IP[] with bad rp[12] */
  if ( BOUNDS( exc.GS.rp2, exc.zp1.n_points ) ) {
    old_range = 0;
    cur_range = 0;
  }
  else {
    if ( twilight )
      old_range = DUALPROJ( exc, exc.zp1.orgX[exc.GS.rp2], exc.zp1.orgY[exc.GS.rp2], obX, obY );
    else if ( exc.metrics.x_scale === exc.metrics.y_scale )
      old_range = DUALPROJ( exc, exc.zp1.orusX[exc.GS.rp2], exc.zp1.orusY[exc.GS.rp2], obX, obY );
    else {
      const vx = FT_MulFix( exc.zp1.orusX[exc.GS.rp2] - obX, exc.metrics.x_scale );
      const vy = FT_MulFix( exc.zp1.orusY[exc.GS.rp2] - obY, exc.metrics.y_scale );
      old_range = exc.func_dualproj( exc, vx, vy );
    }
    cur_range = PROJ( exc, exc.zp1.curX[exc.GS.rp2], exc.zp1.curY[exc.GS.rp2], cbX, cbY );
  }

  let p = A;
  while ( loop-- ) {
    const point = S[--p] >>> 0;   /* FT_UInt point = (FT_UInt)*(--args) */

    if ( BOUNDS( point, exc.zp2.n_points ) ) {
      if ( exc.pedantic_hinting ) { exc.error = ERR.Invalid_Reference; return; }
      continue;
    }

    let org_dist;
    if ( twilight )
      org_dist = DUALPROJ( exc, exc.zp2.orgX[point], exc.zp2.orgY[point], obX, obY );
    else if ( exc.metrics.x_scale === exc.metrics.y_scale )
      org_dist = DUALPROJ( exc, exc.zp2.orusX[point], exc.zp2.orusY[point], obX, obY );
    else {
      const vx = FT_MulFix( exc.zp2.orusX[point] - obX, exc.metrics.x_scale );
      const vy = FT_MulFix( exc.zp2.orusY[point] - obY, exc.metrics.y_scale );
      org_dist = exc.func_dualproj( exc, vx, vy );
    }

    const cur_dist = PROJ( exc, exc.zp2.curX[point], exc.zp2.curY[point], cbX, cbY );

    let new_dist;
    if ( org_dist ) {
      if ( old_range ) new_dist = FT_MulDiv( org_dist, cur_range, old_range );
      /* same as what MS does for the invalid case: new_dist = org_dist */
      else new_dist = org_dist;
    }
    else new_dist = 0;

    exc.func_move( exc, exc.zp2, toU16( point ), new_dist - cur_dist );
  }
  exc.GS.loop = 1;
}

function Ins_UTP( exc, A ) {
  const point = toU16( exc.stack[A] );
  if ( BOUNDS( point, exc.zp0.n_points ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Invalid_Reference;
    return;
  }
  let mask = 0xFF;
  if ( exc.GS.freeVector.x !== 0 ) mask &= ~FT_CURVE_TAG_TOUCH_X;
  if ( exc.GS.freeVector.y !== 0 ) mask &= ~FT_CURVE_TAG_TOUCH_Y;
  exc.zp0.tags[point] &= mask;
}

/* IUP worker: `A' selects the axis array (orgA/curA/orusA), which is how C's
 * `(FT_Pos*)org + 1' pointer trick is expressed with split X/Y arrays. */
function iup_worker_shift( orgA, curA, p1, p2, p ) {
  const dx = curA[p] - orgA[p];
  if ( dx !== 0 ) {
    for ( let i = p1; i < p; i++ ) curA[i] = curA[i] + dx;
    for ( let i = p + 1; i <= p2; i++ ) curA[i] = curA[i] + dx;
  }
}

function iup_worker_interpolate( orgA, curA, orusA, maxPoints, p1, p2, ref1, ref2 ) {
  if ( p1 > p2 ) return;
  if ( BOUNDS( ref1, maxPoints ) || BOUNDS( ref2, maxPoints ) ) return;

  let orus1 = orusA[ref1], orus2 = orusA[ref2];

  if ( orus1 > orus2 ) {
    const tmp_o = orus1; orus1 = orus2; orus2 = tmp_o;
    const tmp_r = ref1;  ref1 = ref2;   ref2 = tmp_r;
  }

  const org1 = orgA[ref1], org2 = orgA[ref2];
  const cur1 = curA[ref1], cur2 = curA[ref2];
  const delta1 = cur1 - org1, delta2 = cur2 - org2;

  if ( cur1 === cur2 || orus1 === orus2 ) {
    /* trivial snap or shift of untouched points */
    for ( let i = p1; i <= p2; i++ ) {
      let x = orgA[i];
      if ( x <= org1 ) x = x + delta1;
      else if ( x >= org2 ) x = x + delta2;
      else x = cur1;
      curA[i] = x;
    }
  }
  else {
    let scale = 0, scale_valid = false;
    for ( let i = p1; i <= p2; i++ ) {
      let x = orgA[i];
      if ( x <= org1 ) x = x + delta1;
      else if ( x >= org2 ) x = x + delta2;
      else {
        if ( !scale_valid ) {
          scale_valid = true;
          scale = FT_DivFix( cur2 - cur1, orus2 - orus1 );
        }
        x = cur1 + FT_MulFix( orusA[i] - orus1, scale );
      }
      curA[i] = x;
    }
  }
}

function Ins_IUP( exc ) {
  /* ignore empty outlines */
  if ( exc.pts.n_contours === 0 ) return;

  let mask, orgA, curA, orusA;
  if ( exc.opcode & 1 ) {
    mask  = FT_CURVE_TAG_TOUCH_X;
    orgA  = exc.pts.orgX; curA = exc.pts.curX; orusA = exc.pts.orusX;
  }
  else {
    mask  = FT_CURVE_TAG_TOUCH_Y;
    orgA  = exc.pts.orgY; curA = exc.pts.curY; orusA = exc.pts.orusY;
  }
  const maxPoints = exc.pts.n_points;
  const tags = exc.pts.tags;

  let contour = 0, point = 0;

  do {
    let end_point   = ( exc.pts.contours[contour] - exc.pts.first_point ) >>> 0;
    const first_point = point;

    if ( BOUNDS( end_point, exc.pts.n_points ) ) end_point = exc.pts.n_points - 1;

    while ( point <= end_point && ( tags[point] & mask ) === 0 ) point++;

    if ( point <= end_point ) {
      const first_touched = point;
      let cur_touched = point;

      point++;

      while ( point <= end_point ) {
        if ( ( tags[point] & mask ) !== 0 ) {
          iup_worker_interpolate( orgA, curA, orusA, maxPoints,
                                  cur_touched + 1, point - 1, cur_touched, point );
          cur_touched = point;
        }
        point++;
      }

      if ( cur_touched === first_touched )
        iup_worker_shift( orgA, curA, first_point, end_point, cur_touched );
      else {
        iup_worker_interpolate( orgA, curA, orusA, maxPoints,
                                toU16( cur_touched + 1 ), end_point,
                                cur_touched, first_touched );
        if ( first_touched > 0 )
          iup_worker_interpolate( orgA, curA, orusA, maxPoints,
                                  first_point, first_touched - 1,
                                  cur_touched, first_touched );
      }
    }
    contour++;
  } while ( contour < exc.pts.n_contours );
}

function Ins_DELTAP( exc, A ) {
  const S = exc.stack;
  let nump = S[A];   /* signed value for convenience */

  if ( nump < 0 || nump > Math.floor( exc.new_top / 2 ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Too_Few_Arguments;
    nump = Math.floor( exc.new_top / 2 );
  }

  exc.new_top -= 2 * nump;

  let P = exc.func_cur_ppem( exc ) - exc.GS.delta_base;

  switch ( exc.opcode ) {
  case 0x5D: break;
  case 0x71: P -= 16; break;
  case 0x72: P -= 32; break;
  }

  /* check applicable range of adjusted ppem: P < 0 || P > 15 */
  if ( P & ~0xF ) return;

  P <<= 4;
  const F = 1 << ( 6 - exc.GS.delta_shift );

  let p = A;
  while ( nump-- ) {
    const Apt = toU16( S[--p] );
    let B = S[--p];

    /* XXX some popular fonts contain invalid DeltaP instructions; ignore
     * out-of-range point references rather than erroring out. */
    if ( BOUNDS( Apt, exc.zp0.n_points ) ) {
      if ( exc.pedantic_hinting ) { exc.error = ERR.Invalid_Reference; return; }
    }
    else if ( ( B & 0xF0 ) === P ) {
      B = ( B & 0xF ) - 8;
      if ( B >= 0 ) B++;
      B *= F;
      /* v35: no backward-compatibility gate */
      exc.func_move( exc, exc.zp0, Apt, B );
    }
  }
}

function Ins_DELTAC( exc, A ) {
  const S = exc.stack;
  let nump = S[A];

  if ( nump < 0 || nump > Math.floor( exc.new_top / 2 ) ) {
    if ( exc.pedantic_hinting ) exc.error = ERR.Too_Few_Arguments;
    nump = Math.floor( exc.new_top / 2 );
  }

  exc.new_top -= 2 * nump;

  let P = exc.func_cur_ppem( exc ) - exc.GS.delta_base;

  switch ( exc.opcode ) {
  case 0x73: break;
  case 0x74: P -= 16; break;
  case 0x75: P -= 32; break;
  }

  if ( P & ~0xF ) return;

  P <<= 4;
  const F = 1 << ( 6 - exc.GS.delta_shift );

  let p = A;
  while ( nump-- ) {
    const Aidx = S[--p];
    let B = S[--p];

    if ( BOUNDSL( Aidx, exc.cvtSize ) ) {
      if ( exc.pedantic_hinting ) { exc.error = ERR.Invalid_Reference; return; }
    }
    else if ( ( B & 0xF0 ) === P ) {
      B = ( B & 0xF ) - 8;
      if ( B >= 0 ) B++;
      B *= F;
      exc.func_move_cvt( exc, Aidx, B );
    }
  }
}

/* v35: bit 0 returns 35; the v40 subpixel bits (13,15,17,18,19) are gone. */
function Ins_GETINFO( exc, A ) {
  const S = exc.stack;
  let K = 0;

  if ( ( S[A] & 1 ) !== 0 ) K = 35;   /* driver->interpreter_version */

  /* GLYPH ROTATED: selector bit 1 -> return bit 8 */
  if ( ( S[A] & 2 ) !== 0 && exc.tt_metrics.rotated ) K |= 1 << 8;

  /* GLYPH STRETCHED: selector bit 2 -> return bit 9 */
  if ( ( S[A] & 4 ) !== 0 && exc.tt_metrics.stretched ) K |= 1 << 9;

  /* VARIATION GLYPH: selector bit 3 -> return bit 10 */
  if ( ( S[A] & 8 ) !== 0 && exc.face && exc.face.blend ) K |= 1 << 10;

  /* BI-LEVEL HINTING AND GRAYSCALE RENDERING: selector 5 -> bit 12 */
  if ( ( S[A] & 32 ) !== 0 && exc.grayscale ) K |= 1 << 12;

  S[A] = K;
}

function Ins_GETVARIATION( exc, A ) {
  const S = exc.stack;
  const blend = exc.face.blend;
  const num_axes = blend.num_axis;

  if ( BOUNDS( num_axes, exc.stackSize + 1 - exc.top ) ) {
    exc.error = ERR.Stack_Overflow;
    return;
  }

  const coords = blend.normalizedcoords;
  if ( coords ) for ( let i = 0; i < num_axes; i++ ) S[A + i] = asr( coords[i], 2 );
  else for ( let i = 0; i < num_axes; i++ ) S[A + i] = 0;

  exc.new_top += num_axes;
}

function Ins_GETDATA( exc, A ) { exc.stack[A] = 17; }

function Ins_UNKNOWN( exc ) {
  for ( let i = 0; i < exc.numIDefs; i++ ) {
    const def = exc.IDefs[i];
    if ( ( def.opc & 0xFF ) === exc.opcode && def.active ) {
      if ( exc.callTop >= exc.callSize ) { exc.error = ERR.Stack_Overflow; return; }
      const call = exc.callStack[exc.callTop++];
      call.Caller_Range = exc.curRange;
      call.Caller_IP    = exc.IP + 1;
      call.Cur_Count    = 1;
      call.Def          = def;
      Ins_Goto_CodeRange( exc, def.range, def.start );
      return;
    }
  }
  exc.error = ERR.Invalid_Opcode;
}


/* ------------------------------------------------------------------ */
/*  THE INTERPRETER'S MAIN LOOP                                        */
/* ------------------------------------------------------------------ */

export function TT_RunIns( exc ) {
  let ins_counter = 0;

  do {
    /* guard against infinite loops */
    if ( ++ins_counter > MAX_RUNNABLE_OPCODES ) {
      exc.error = ERR.Execution_Too_Long;
      return exc.error;
    }

    exc.error  = ERR.Ok;
    exc.opcode = exc.code[exc.IP];
    exc.length = 1;

    const pp = Pop_Push_Count[exc.opcode];

    /* check for empty stack and overflow */
    exc.args = exc.top - ( pp >> 4 );

    if ( exc.args < 0 ) {
      if ( exc.pedantic_hinting ) {
        exc.error = ERR.Too_Few_Arguments;
        return exc.error;
      }
      /* push zeroes onto the stack */
      for ( let i = 0; i < ( pp >> 4 ); i++ ) exc.stack[i] = 0;
      exc.args = 0;
    }

    exc.new_top = exc.args + ( pp & 15 );

    if ( exc.new_top > exc.stackSize ) {
      exc.error = ERR.Stack_Overflow;
      return exc.error;
    }

    const A = exc.args;
    const opcode = exc.opcode;

    switch ( opcode ) {
    case 0x00: case 0x01: case 0x02:
    case 0x03: case 0x04: case 0x05: Ins_SxyTCA( exc ); break;
    case 0x06: case 0x07: Ins_SPVTL( exc, A ); break;
    case 0x08: case 0x09: Ins_SFVTL( exc, A ); break;
    case 0x0A: Ins_SPVFS( exc, A ); break;
    case 0x0B: Ins_SFVFS( exc, A ); break;
    case 0x0C: Ins_GPV( exc, A ); break;
    case 0x0D: Ins_GFV( exc, A ); break;
    case 0x0E: Ins_SFVTPV( exc ); break;
    case 0x0F: Ins_ISECT( exc, A ); break;
    case 0x10: Ins_SRP0( exc, A ); break;
    case 0x11: Ins_SRP1( exc, A ); break;
    case 0x12: Ins_SRP2( exc, A ); break;
    case 0x13: Ins_SZP0( exc, A ); break;
    case 0x14: Ins_SZP1( exc, A ); break;
    case 0x15: Ins_SZP2( exc, A ); break;
    case 0x16: Ins_SZPS( exc, A ); break;
    case 0x17: Ins_SLOOP( exc, A ); break;
    case 0x18: Ins_RTG( exc ); break;
    case 0x19: Ins_RTHG( exc ); break;
    case 0x1A: Ins_SMD( exc, A ); break;
    case 0x1B: Ins_ELSE( exc ); break;
    case 0x1C: Ins_JMPR( exc, A ); break;
    case 0x1D: Ins_SCVTCI( exc, A ); break;
    case 0x1E: Ins_SSWCI( exc, A ); break;
    case 0x1F: Ins_SSW( exc, A ); break;
    case 0x20: Ins_DUP( exc, A ); break;
    case 0x21: Ins_POP(); break;
    case 0x22: Ins_CLEAR( exc ); break;
    case 0x23: Ins_SWAP( exc, A ); break;
    case 0x24: Ins_DEPTH( exc, A ); break;
    case 0x25: Ins_CINDEX( exc, A ); break;
    case 0x26: Ins_MINDEX( exc, A ); break;
    case 0x27: Ins_ALIGNPTS( exc, A ); break;
    case 0x28: Ins_UNKNOWN( exc ); break;
    case 0x29: Ins_UTP( exc, A ); break;
    case 0x2A: Ins_LOOPCALL( exc, A ); break;
    case 0x2B: Ins_CALL( exc, A ); break;
    case 0x2C: Ins_FDEF( exc, A ); break;
    case 0x2D: Ins_ENDF( exc ); break;
    case 0x2E: case 0x2F: Ins_MDAP( exc, A ); break;
    case 0x30: case 0x31: Ins_IUP( exc ); break;
    case 0x32: case 0x33: Ins_SHP( exc, A ); break;
    case 0x34: case 0x35: Ins_SHC( exc, A ); break;
    case 0x36: case 0x37: Ins_SHZ( exc, A ); break;
    case 0x38: Ins_SHPIX( exc, A ); break;
    case 0x39: Ins_IP( exc, A ); break;
    case 0x3A: case 0x3B: Ins_MSIRP( exc, A ); break;
    case 0x3C: Ins_ALIGNRP( exc, A ); break;
    case 0x3D: Ins_RTDG( exc ); break;
    case 0x3E: case 0x3F: Ins_MIAP( exc, A ); break;
    case 0x40: Ins_NPUSHB( exc, A ); break;
    case 0x41: Ins_NPUSHW( exc, A ); break;
    case 0x42: Ins_WS( exc, A ); break;
    case 0x43: Ins_RS( exc, A ); break;
    case 0x44: Ins_WCVTP( exc, A ); break;
    case 0x45: Ins_RCVT( exc, A ); break;
    case 0x46: case 0x47: Ins_GC( exc, A ); break;
    case 0x48: Ins_SCFS( exc, A ); break;
    case 0x49: case 0x4A: Ins_MD( exc, A ); break;
    case 0x4B: Ins_MPPEM( exc, A ); break;
    case 0x4C: Ins_MPS( exc, A ); break;
    case 0x4D: Ins_FLIPON( exc ); break;
    case 0x4E: Ins_FLIPOFF( exc ); break;
    case 0x4F: Ins_DEBUG( exc ); break;
    case 0x50: Ins_LT( exc, A ); break;
    case 0x51: Ins_LTEQ( exc, A ); break;
    case 0x52: Ins_GT( exc, A ); break;
    case 0x53: Ins_GTEQ( exc, A ); break;
    case 0x54: Ins_EQ( exc, A ); break;
    case 0x55: Ins_NEQ( exc, A ); break;
    case 0x56: Ins_ODD( exc, A ); break;
    case 0x57: Ins_EVEN( exc, A ); break;
    case 0x58: Ins_IF( exc, A ); break;
    case 0x59: Ins_EIF(); break;
    case 0x5A: Ins_AND( exc, A ); break;
    case 0x5B: Ins_OR( exc, A ); break;
    case 0x5C: Ins_NOT( exc, A ); break;
    case 0x5D: Ins_DELTAP( exc, A ); break;
    case 0x5E: Ins_SDB( exc, A ); break;
    case 0x5F: Ins_SDS( exc, A ); break;
    case 0x60: Ins_ADD( exc, A ); break;
    case 0x61: Ins_SUB( exc, A ); break;
    case 0x62: Ins_DIV( exc, A ); break;
    case 0x63: Ins_MUL( exc, A ); break;
    case 0x64: Ins_ABS( exc, A ); break;
    case 0x65: Ins_NEG( exc, A ); break;
    case 0x66: Ins_FLOOR( exc, A ); break;
    case 0x67: Ins_CEILING( exc, A ); break;
    case 0x68: case 0x69: case 0x6A: case 0x6B: Ins_ROUND( exc, A ); break;
    case 0x6C: case 0x6D: case 0x6E: case 0x6F: Ins_NROUND( exc, A ); break;
    case 0x70: Ins_WCVTF( exc, A ); break;
    case 0x71: case 0x72: Ins_DELTAP( exc, A ); break;
    case 0x73: case 0x74: case 0x75: Ins_DELTAC( exc, A ); break;
    case 0x76: Ins_SROUND( exc, A ); break;
    case 0x77: Ins_S45ROUND( exc, A ); break;
    case 0x78: Ins_JROT( exc, A ); break;
    case 0x79: Ins_JROF( exc, A ); break;
    case 0x7A: Ins_ROFF( exc ); break;
    case 0x7B: Ins_UNKNOWN( exc ); break;
    case 0x7C: Ins_RUTG( exc ); break;
    case 0x7D: Ins_RDTG( exc ); break;
    case 0x7E: Ins_SANGW(); break;
    case 0x7F: Ins_AA(); break;
    case 0x80: Ins_FLIPPT( exc, A ); break;
    case 0x81: Ins_FLIPRGON( exc, A ); break;
    case 0x82: Ins_FLIPRGOFF( exc, A ); break;
    case 0x83: case 0x84: Ins_UNKNOWN( exc ); break;
    case 0x85: Ins_SCANCTRL( exc, A ); break;
    case 0x86: case 0x87: Ins_SDPVTL( exc, A ); break;
    case 0x88: Ins_GETINFO( exc, A ); break;
    case 0x89: Ins_IDEF( exc, A ); break;
    case 0x8A: Ins_ROLL( exc, A ); break;
    case 0x8B: Ins_MAX( exc, A ); break;
    case 0x8C: Ins_MIN( exc, A ); break;
    case 0x8D: Ins_SCANTYPE( exc, A ); break;
    case 0x8E: Ins_INSTCTRL( exc, A ); break;
    case 0x8F: case 0x90: Ins_UNKNOWN( exc ); break;
    case 0x91:
      /* GETVARIATION is active only for GX fonts */
      if ( exc.face && exc.face.blend ) Ins_GETVARIATION( exc, A );
      else Ins_UNKNOWN( exc );
      break;
    case 0x92:
      if ( exc.face && exc.face.blend ) Ins_GETDATA( exc, A );
      else Ins_UNKNOWN( exc );
      break;
    default:
      if ( opcode >= 0xE0 ) Ins_MIRP( exc, A );
      else if ( opcode >= 0xC0 ) Ins_MDRP( exc, A );
      else if ( opcode >= 0xB8 ) Ins_PUSHW( exc, A );
      else if ( opcode >= 0xB0 ) Ins_PUSHB( exc, A );
      else Ins_UNKNOWN( exc );
    }

    let suite = false;

    if ( exc.error ) {
      if ( exc.error === ERR.Invalid_Opcode ) {
        /* look for redefined instructions */
        let found = false;
        for ( let i = 0; i < exc.numIDefs; i++ ) {
          const def = exc.IDefs[i];
          if ( def.active && exc.opcode === ( def.opc & 0xFF ) ) {
            if ( exc.callTop >= exc.callSize ) {
              exc.error = ERR.Invalid_Reference;
              return exc.error;
            }
            const callrec = exc.callStack[exc.callTop];
            callrec.Caller_Range = exc.curRange;
            callrec.Caller_IP    = exc.IP + 1;
            callrec.Cur_Count    = 1;
            callrec.Def          = def;

            if ( Ins_Goto_CodeRange( exc, def.range, def.start ) === FAILURE )
              return exc.error;

            found = true;
            suite = true;   /* goto LSuiteLabel_ */
            break;
          }
        }
        if ( !found ) return exc.error;
      }
      else return exc.error;
    }

    if ( !suite ) {
      exc.top = exc.new_top;
      exc.IP += exc.length;
    }

    /* LSuiteLabel_ */
    if ( exc.IP >= exc.codeSize ) {
      if ( exc.callTop > 0 ) {
        exc.error = ERR.Code_Overflow;
        return exc.error;
      }
      return ERR.Ok;   /* LNo_Error_ */
    }
  } while ( !exc.instruction_trap );

  return ERR.Ok;
}


/* ------------------------------------------------------------------ */
/*  TT_Run_Context                                                     */
/* ------------------------------------------------------------------ */

export function TT_Run_Context( exc, size ) {
  exc.zp0 = exc.pts;
  exc.zp1 = exc.pts;
  exc.zp2 = exc.pts;

  /* Restrict the number of twilight points to a heuristic value to avoid
   * slow execution of malformed bytecode.  C works on a struct copy of the
   * size's zone, so the clamp is per-run; we restore from twilightAlloc. */
  exc.twilight.n_points = exc.twilightAlloc;
  let num_twilight_points = Math.max( 30, 2 * ( exc.pts.n_points + exc.cvtSize ) );
  if ( exc.twilight.n_points > num_twilight_points ) {
    if ( num_twilight_points > 0xFFFF ) num_twilight_points = 0xFFFF;
    exc.twilight.n_points = num_twilight_points;
  }

  /* loop detectors for LOOPCALL and backward jumps */
  exc.loopcall_counter = 0;
  exc.neg_jump_counter = 0;

  if ( exc.pts.n_points )
    exc.loopcall_counter_max = Math.max( 50, 10 * exc.pts.n_points ) +
                               Math.max( 50, Math.floor( exc.cvtSize / 10 ) );
  else
    exc.loopcall_counter_max = 300 + 22 * exc.cvtSize;

  /* at most 100 control values per glyph */
  if ( exc.loopcall_counter_max > 100 * exc.face.num_glyphs )
    exc.loopcall_counter_max = 100 * exc.face.num_glyphs;

  exc.neg_jump_counter_max = exc.loopcall_counter_max;

  /* set PPEM and CVT functions */
  if ( exc.metrics.x_ppem !== exc.metrics.y_ppem ) {
    /* non-square pixels, use the stretched routines */
    exc.func_cur_ppem  = Current_Ppem_Stretched;
    exc.func_read_cvt  = Read_CVT_Stretched;
    exc.func_write_cvt = Write_CVT_Stretched;
    exc.func_move_cvt  = Move_CVT_Stretched;
  }
  else {
    exc.func_cur_ppem  = Current_Ppem;
    exc.func_read_cvt  = Read_CVT;
    exc.func_write_cvt = Write_CVT;
    exc.func_move_cvt  = Move_CVT;
  }

  /* reset graphics state */
  exc.GS = copyGS( size.GS );
  exc.func_round = Round_To_Grid;
  Compute_Funcs( exc );

  /* some glyphs leave something on the stack, so clean it first */
  exc.top     = 0;
  exc.callTop = 0;

  exc.instruction_trap = false;

  return TT_RunIns( exc );
}
