/* ftscale.js -- FT_Set_Char_Size / FT_Request_Metrics (base/ftobjs.c,
 * truetype/ttdriver.c, truetype/ttobjs.c) plus the fixed-point primitives
 * from base/ftcalc.c that the glyph loader needs.
 *
 * C function -> JS function map
 *   FT_MulDiv, FT_MulDiv_No_Round, FT_MulFix, FT_DivFix, FT_RoundFix
 *                                     -> same names
 *   FT_PIX_FLOOR/ROUND/CEIL (ftobjs.h macros) -> same names
 *   FT_Vector_Transform (ftoutln.c)    -> FT_Vector_Transform
 *   FT_Outline_Transform/Translate/Get_CBox (ftoutln.c) -> same names
 *   FT_Hypot / FT_Vector_Length (fttrigon.c) -> FT_Hypot
 *   FT_Set_Char_Size + FT_Request_Size + tt_size_request + FT_Request_Metrics
 *                                     -> requestCharSize
 *   the scaling loop of TT_Process_Simple_Glyph -> scaleOutline
 *
 * W1 owns ftcalc.js.  If it exists we defer to it so there is exactly one
 * implementation; otherwise we use the transliterations below.
 *
 * C-semantics hazards (see NOTES-W2.md):
 *   - FT_Long is 64-bit on x86-64 Linux, so `a * b' in FT_MulFix may exceed
 *     2^53.  Every product is computed in double only when it is provably
 *     exact (Number.isSafeInteger) and in BigInt otherwise.
 *   - `ab >> 16' on a signed value is an ARITHMETIC shift: floor division,
 *     not truncation.  `x & ~63' likewise floors toward -infinity.
 *   - FT_MOVE_SIGN pulls the sign out and divides UNSIGNED, so FT_MulDiv and
 *     FT_DivFix truncate toward zero on the magnitude, then re-apply the sign.
 */

let W1 = null;
try {
  W1 = await import( './ftcalc.js' );          /* W1's module, when it lands */
} catch {
  W1 = null;
}

const SAFE = Number.isSafeInteger;

/* ---------------------------------------------------------------- MulFix */
function _FT_MulFix( a, b ) {
  /* C: FT_Int64 ab = MUL_INT64( a_, b_ );
   *    return (FT_Long)( ( ab + 0x8000L + ( ab >> 63 ) ) >> 16 );        */
  const ab = a * b;
  if ( SAFE( ab ) ) {
    /* `ab >> 63' is the sign: -1 when negative, 0 otherwise. */
    const t = ab + 0x8000 + ( ab < 0 ? -1 : 0 );
    return Math.floor( t / 65536 );            /* arithmetic >> 16 */
  }
  const A = BigInt( a ) * BigInt( b );
  const t = A + 0x8000n + ( A < 0n ? -1n : 0n );
  return Number( bshr( t, 16n ) );
}

/* arithmetic shift right for BigInt (floor division by 2^n) */
function bshr( v, n ) {
  const d = 1n << n;
  let q = v / d;
  if ( v < 0n && q * d !== v ) q -= 1n;
  return q;
}

/* ---------------------------------------------------------------- MulDiv */
function _FT_MulDiv( a_, b_, c_ ) {
  let s = 1;
  let a = a_, b = b_, c = c_;
  if ( a < 0 ) { a = -a; s = -s; }             /* FT_MOVE_SIGN */
  if ( b < 0 ) { b = -b; s = -s; }
  if ( c < 0 ) { c = -c; s = -s; }

  let d;
  if ( c > 0 ) {
    const ab = a * b;
    if ( SAFE( ab ) )
      d = Math.floor( ( ab + Math.floor( c / 2 ) ) / c );   /* unsigned div */
    else
      d = Number( ( BigInt( a ) * BigInt( b ) + BigInt( c ) / 2n ) / BigInt( c ) );
  } else
    d = 0x7FFFFFFF;

  return s < 0 ? -d : d;
}

function _FT_MulDiv_No_Round( a_, b_, c_ ) {
  let s = 1;
  let a = a_, b = b_, c = c_;
  if ( a < 0 ) { a = -a; s = -s; }
  if ( b < 0 ) { b = -b; s = -s; }
  if ( c < 0 ) { c = -c; s = -s; }

  let d;
  if ( c > 0 ) {
    const ab = a * b;
    d = SAFE( ab ) ? Math.floor( ab / c )
                   : Number( ( BigInt( a ) * BigInt( b ) ) / BigInt( c ) );
  } else
    d = 0x7FFFFFFF;

  return s < 0 ? -d : d;
}

/* ---------------------------------------------------------------- DivFix */
function _FT_DivFix( a_, b_ ) {
  let s = 1;
  let a = a_, b = b_;
  if ( a < 0 ) { a = -a; s = -s; }
  if ( b < 0 ) { b = -b; s = -s; }

  let q;
  if ( b > 0 ) {
    const a16 = a * 65536;
    if ( SAFE( a16 ) )
      q = Math.floor( ( a16 + Math.floor( b / 2 ) ) / b );
    else
      q = Number( ( ( BigInt( a ) << 16n ) + BigInt( b ) / 2n ) / BigInt( b ) );
  } else
    q = 0x7FFFFFFF;

  return s < 0 ? -q : q;
}

function _FT_RoundFix( a ) {
  /* C: ( ADD_LONG( a, 0x8000 ) ) & ~0xFFFF */
  return Math.floor( ( a + 0x8000 ) / 65536 ) * 65536;
}

/* ------------------------------------------------- FT_PIX_* (ftobjs.h) -- */
/* C: (x) & ~63 on a SIGNED value -- floors toward -infinity. */
function _FT_PIX_FLOOR( x ) { return Math.floor( x / 64 ) * 64; }
function _FT_PIX_ROUND( x ) { return _FT_PIX_FLOOR( x + 32 ); }
function _FT_PIX_CEIL ( x ) { return _FT_PIX_FLOOR( x + 63 ); }

/* ------------------------------------------------------ FT_Vector_Length */
const FT_TRIG_SCALE     = 0xDBD95B16n;  /* 0.858785336480436 * 2^32 */
const FT_TRIG_SAFE_MSB  = 29;
const FT_TRIG_MAX_ITERS = 23;
const ft_trig_arctan_table = [
  1740967, 919879, 466945, 234379, 117304, 58666, 29335,
  14668, 7334, 3667, 1833, 917, 458, 229, 115,
  57, 29, 14, 7, 4, 2, 1,
];

function FT_MSB( z ) {                 /* ftcalc.c */
  let shift = 0;
  z = z >>> 0;
  if ( z & 0xFFFF0000 ) { z >>>= 16; shift += 16; }
  if ( z & 0x0000FF00 ) { z >>>= 8;  shift += 8;  }
  if ( z & 0x000000F0 ) { z >>>= 4;  shift += 4;  }
  if ( z & 0x0000000C ) { z >>>= 2;  shift += 2;  }
  if ( z & 0x00000002 ) {             shift += 1; }
  return shift;
}

function ft_trig_downscale( val ) {
  let s = 1;
  if ( val < 0 ) { val = -val; s = -1; }
  /* C (FT_INT64): ( (FT_UInt64)val * FT_TRIG_SCALE + 0x40000000UL ) >> 32 */
  const v = ( BigInt( val ) * FT_TRIG_SCALE + 0x40000000n ) >> 32n;
  const r = Number( v );
  return s < 0 ? -r : r;
}

function ft_trig_prenorm( v ) {
  const x = v.x, y = v.y;
  let shift = FT_MSB( ( Math.abs( x ) | Math.abs( y ) ) >>> 0 );

  if ( shift <= FT_TRIG_SAFE_MSB ) {
    shift = FT_TRIG_SAFE_MSB - shift;
    v.x = x * Math.pow( 2, shift );
    v.y = y * Math.pow( 2, shift );
  } else {
    shift -= FT_TRIG_SAFE_MSB;
    v.x = Math.floor( x / Math.pow( 2, shift ) );   /* arithmetic >> */
    v.y = Math.floor( y / Math.pow( 2, shift ) );
    shift = -shift;
  }
  return shift;
}

function ft_trig_pseudo_polarize( v ) {
  let theta, xtemp;
  let x = v.x, y = v.y;

  if ( y > x ) {
    if ( y > -x ) { theta = FT_ANGLE_PI2; xtemp = y; y = -x; x = xtemp; }
    else          { theta = y > 0 ? FT_ANGLE_PI : -FT_ANGLE_PI; x = -x; y = -y; }
  } else {
    if ( y < -x ) { theta = -FT_ANGLE_PI2; xtemp = -y; y = x; x = xtemp; }
    else          { theta = 0; }
  }

  let ap = 0;
  for ( let i = 1, b = 1; i < FT_TRIG_MAX_ITERS; b *= 2, i++ ) {
    const p = Math.pow( 2, i );
    if ( y > 0 ) {
      xtemp  = x + Math.floor( ( y + b ) / p );
      y      = y - Math.floor( ( x + b ) / p );
      x      = xtemp;
      theta += ft_trig_arctan_table[ap++];
    } else {
      xtemp  = x - Math.floor( ( y + b ) / p );
      y      = y + Math.floor( ( x + b ) / p );
      x      = xtemp;
      theta -= ft_trig_arctan_table[ap++];
    }
  }

  /* FT_PAD_ROUND( theta, 16 ) */
  const padRound = ( t ) => Math.floor( ( t + 8 ) / 16 ) * 16;
  theta = theta >= 0 ? padRound( theta ) : -padRound( -theta );

  v.x = x;
  v.y = theta;
}

const FT_ANGLE_PI  = 180 << 16;
const FT_ANGLE_PI2 = FT_ANGLE_PI / 2;

function _FT_Hypot( x, y ) {
  /* FT_Hypot -> FT_Vector_Length */
  if ( x === 0 ) return Math.abs( y );
  if ( y === 0 ) return Math.abs( x );

  const v = { x, y };
  const shift = ft_trig_prenorm( v );
  ft_trig_pseudo_polarize( v );
  v.x = ft_trig_downscale( v.x );

  if ( shift > 0 )
    return Math.floor( ( v.x + Math.pow( 2, shift - 1 ) ) / Math.pow( 2, shift ) );

  return v.x * Math.pow( 2, -shift );
}

/* ------------------------------------------------------------- exports -- */
export const FT_MulFix           = W1?.FT_MulFix           ?? _FT_MulFix;
export const FT_MulDiv           = W1?.FT_MulDiv           ?? _FT_MulDiv;
export const FT_MulDiv_No_Round  = W1?.FT_MulDiv_No_Round  ?? _FT_MulDiv_No_Round;
export const FT_DivFix           = W1?.FT_DivFix           ?? _FT_DivFix;
export const FT_RoundFix         = W1?.FT_RoundFix         ?? _FT_RoundFix;
export const FT_PIX_ROUND        = W1?.FT_PIX_ROUND        ?? _FT_PIX_ROUND;
export const FT_PIX_FLOOR        = W1?.FT_PIX_FLOOR        ?? _FT_PIX_FLOOR;
export const FT_PIX_CEIL         = W1?.FT_PIX_CEIL         ?? _FT_PIX_CEIL;
export const FT_Hypot            = W1?.FT_Hypot            ?? _FT_Hypot;
export const usingW1Ftcalc       = !!W1;

/** FT_Vector_Transform (ftoutln.c) -- v is {x,y}, m is {xx,xy,yx,yy} 16.16 */
export function FT_Vector_Transform( v, m ) {
  if ( W1?.FT_Vector_Transform ) return W1.FT_Vector_Transform( v, m );
  const xz = FT_MulFix( v.x, m.xx ) + FT_MulFix( v.y, m.xy );
  const yz = FT_MulFix( v.x, m.yx ) + FT_MulFix( v.y, m.yy );
  v.x = xz;
  v.y = yz;
}

/** FT_Outline_Transform over X[first..first+n) / Y[first..first+n) */
export function FT_Outline_Transform( X, Y, first, n, m ) {
  for ( let i = first; i < first + n; i++ ) {
    const xz = FT_MulFix( X[i], m.xx ) + FT_MulFix( Y[i], m.xy );
    const yz = FT_MulFix( X[i], m.yx ) + FT_MulFix( Y[i], m.yy );
    X[i] = xz;
    Y[i] = yz;
  }
}

/** FT_Outline_Translate over X[first..first+n) / Y[first..first+n) */
export function FT_Outline_Translate( X, Y, first, n, dx, dy ) {
  for ( let i = first; i < first + n; i++ ) {
    X[i] += dx;
    Y[i] += dy;
  }
}

/** FT_Outline_Get_CBox (ftoutln.c) */
export function FT_Outline_Get_CBox( outline ) {
  if ( !outline || outline.n_points === 0 )
    return { xMin: 0, yMin: 0, xMax: 0, yMax: 0 };

  const X = outline.x, Y = outline.y, n = outline.n_points;
  let xMin = X[0], xMax = X[0], yMin = Y[0], yMax = Y[0];
  for ( let i = 1; i < n; i++ ) {
    const x = X[i], y = Y[i];
    if ( x < xMin ) xMin = x;
    if ( x > xMax ) xMax = x;
    if ( y < yMin ) yMin = y;
    if ( y > yMax ) yMax = y;
  }
  return { xMin, yMin, xMax, yMax };
}

/**
 * FT_Set_Char_Size( face, ptsize << 6, ptsize << 6, 72, 72 ) as ftlint calls
 * it, followed by tt_size_request -> FT_Request_Metrics -> tt_size_reset.
 *
 * The UNHINTED path uses the BASE metrics (ttdriver.c: `ttsize->metrics =
 * ( load_flags & FT_LOAD_NO_HINTING ) ? &size->metrics : &ttsize->
 * hinted_metrics'), so `x_scale'/`y_scale' below are the base ones and the
 * `head.Flags & 8' integer-ppem re-derivation is NOT applied to them.
 * `hinted' carries the other variant for W4.
 */
export function requestCharSize( face, ptsize ) {
  /* FT_Set_Char_Size */
  let char_width  = ptsize * 64;      /* ptsize << 6 */
  let char_height = ptsize * 64;
  if ( char_width  < 64 ) char_width  = 64;
  if ( char_height < 64 ) char_height = 64;
  const horiResolution = 72, vertResolution = 72;

  /* FT_Request_Metrics, req->type == FT_SIZE_REQUEST_TYPE_NOMINAL */
  const w = face.unitsPerEM;
  const h = face.unitsPerEM;

  /* FT_REQUEST_WIDTH/HEIGHT (ftobjs.h); C integer division truncates */
  const scaled_w = Math.trunc( ( char_width  * horiResolution + 36 ) / 72 );
  const scaled_h = Math.trunc( ( char_height * vertResolution + 36 ) / 72 );

  /* req->height != 0 -> y_scale; req->width != 0 -> x_scale */
  const y_scale = FT_DivFix( scaled_h, h );
  const x_scale = FT_DivFix( scaled_w, w );

  /* req->type IS nominal, so `Calculate_Ppem' keeps scaled_w/scaled_h. */
  const x_ppem = ( scaled_w + 32 ) >> 6;
  const y_ppem = ( scaled_h + 32 ) >> 6;

  /* tt_size_reset: only used by the hinted path. */
  let hx = x_scale, hy = y_scale;
  if ( face.head && ( face.head.Flags & 8 ) ) {
    hx = FT_DivFix( x_ppem * 64, face.unitsPerEM );
    hy = FT_DivFix( y_ppem * 64, face.unitsPerEM );
  }

  return {
    x_ppem, y_ppem, x_scale, y_scale,
    hinted: { x_ppem, y_ppem, x_scale: hx, y_scale: hy },
  };
}

/**
 * The scaling loop of TT_Process_Simple_Glyph: every point of a simple glyph
 * (and its four phantom points) goes through FT_MulFix.  Mutates in place.
 */
export function scaleOutline( outline, x_scale, y_scale ) {
  const X = outline.x, Y = outline.y;
  for ( let i = 0; i < outline.n_points; i++ ) {
    X[i] = FT_MulFix( X[i], x_scale );
    Y[i] = FT_MulFix( Y[i], y_scale );
  }
  return outline;
}
