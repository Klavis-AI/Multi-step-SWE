/* raster.js -- literal port of FreeType 2.14.3 `src/smooth/ftgrays.c`,
 * plus `FT_Outline_Decompose` from `src/base/ftoutln.c` (which ftgrays calls
 * and which no other workstream owns).
 *
 * REFERENCE BUILD: x86-64 Linux / gcc, so FT_SIZEOF_LONG == 8 and **FT_INT64
 * is defined**.  That selects, inside ftgrays.c:
 *   - the SECOND `gray_render_line` body (ftgrays.c:873-997), the one that
 *     walks cells with the `prod` cross-product and FT_UDIV; the first body
 *     (ftgrays.c:639-865, together with `gray_render_scanline`) is `#ifndef
 *     FT_INT64` and is NOT compiled.
 *   - the DDA `gray_render_conic` (ftgrays.c:1012-1141); the bisecting
 *     `gray_split_conic` / `gray_render_conic` pair (ftgrays.c:1150-1235) is
 *     the not-FT_INT64 half and is NOT compiled.
 * Both alternatives are ported here and selected by the `FT_INT64` constant
 * below, mirroring the #ifdef exactly, so the inactive halves stay auditable.
 *
 * TYPES (ftgrays.c:444-446):  TPos = long (64-bit), TCoord = int (32-bit),
 * TArea = int (32-bit).  Cell `cover`/`area` accumulate in 32-bit ints and
 * are *expected* to wrap; see FT_INTEGRATE below.
 */

/* ---- error codes -------------------------------------------------------
 * include/freetype/fterrdef.h.  FT_CONFIG_OPTION_USE_MODULE_ERRORS is off by
 * default, so the smooth module's error base is 0 and Smooth_Err_X == FT_Err_X.
 */
export const Smooth_Err_Ok             = 0x00;
export const Smooth_Err_Invalid_Argument    = 0x06;
export const Smooth_Err_Cannot_Render_Glyph = 0x13;
export const Smooth_Err_Invalid_Outline     = 0x14;
export const Smooth_Err_Raster_Overflow     = 0x62;

/* ftimage.h */
export const FT_RASTER_FLAG_AA     = 0x1;
export const FT_RASTER_FLAG_DIRECT = 0x2;
export const FT_RASTER_FLAG_CLIP   = 0x4;

export const FT_OUTLINE_EVEN_ODD_FILL = 0x2;
export const FT_OUTLINE_OVERLAP       = 0x40;

const FT_CURVE_TAG_ON    = 0x01;
const FT_CURVE_TAG_CONIC = 0x00;
const FT_CURVE_TAG_CUBIC = 0x02;

/* ftgrays.c:329-343 */
const PIXEL_BITS = 8;
const ONE_PIXEL  = 1 << PIXEL_BITS;          /* 256 */

/* Selects the #ifdef FT_INT64 halves of ftgrays.c.  See header comment. */
const FT_INT64 = true;

const INT_MIN          = -2147483648;
const CELL_MAX_X_VALUE = 2147483647;         /* ftgrays.c:524, INT_MAX */

/* ftgrays.c:468-472.  FT_RENDER_POOL_SIZE is 16384 (ftoption.h:429) and
 * sizeof(TCell) is 24 on LP64 (int,int,int,pad,pointer) -- verified by
 * compiling the struct in the reference container.  sizeof(PCell) is 8. */
const FT_RENDER_POOL_SIZE = 16384;
const SIZEOF_TCELL        = 24;
const SIZEOF_PCELL        = 8;
const FT_MAX_GRAY_POOL    = Math.floor( FT_RENDER_POOL_SIZE / SIZEOF_TCELL ); /* 682 */
const FT_MAX_GRAY_SPANS   = 16;              /* ftgrays.c:475 */

/* gray_convert_glyph splits bands until height <= FT_MAX_GRAY_POOL/8 == 85,
 * so count_ey never exceeds 85; 128 is a safe ycells bound. */
const MAX_COUNT_EY = 128;


/* ---- TRUNC / FRACT / UPSCALE (ftgrays.c:332-343) -----------------------
 * C emulation: TPos is a signed 64-bit long, so `x >> PIXEL_BITS` is an
 * arithmetic shift (floor division by 256) and `x & (ONE_PIXEL-1)` is the
 * non-negative remainder.  JS `>>`/`&` would truncate the operand to int32,
 * so both are written with Math.floor instead. */
function TRUNC( x )  { return Math.floor( x / ONE_PIXEL ) | 0; }
function FRACT( x )  { return x - Math.floor( x / ONE_PIXEL ) * ONE_PIXEL; }
function UPSCALE( x ) { return x * ( ONE_PIXEL >> 6 ); }   /* PIXEL_BITS >= 6 */


/* ---- the worker (ftgrays.c:486-510) ----------------------------------- */
function newWorker() {
  return {
    cbox: { xMin: 0, yMin: 0, xMax: 0, yMax: 0 },

    min_ex: 0, max_ex: 0, min_ey: 0, max_ey: 0, count_ey: 0,

    error: Smooth_Err_Ok,
    cell: 0, cell_free: 0, cell_null: FT_MAX_GRAY_POOL - 1,

    /* the TCell pool, as parallel arrays; `next` holds cell indices, and the
     * null cell (index FT_MAX_GRAY_POOL-1) is both dumpster and list end. */
    cellX:     new Int32Array( FT_MAX_GRAY_POOL ),
    cellCover: new Int32Array( FT_MAX_GRAY_POOL ),
    cellArea:  new Int32Array( FT_MAX_GRAY_POOL ),
    cellNext:  new Int32Array( FT_MAX_GRAY_POOL ),

    ycells: new Int32Array( MAX_COUNT_EY ),

    x: 0, y: 0,

    outline: null,

    targetBuf: null, targetPitch: 0, targetOrigin: 0,

    renderSpan: null, renderSpanData: null,
  };
}

/* ftgrays.c uses a stack-local gray_TWorker per render call; a module
 * singleton is equivalent because the raster is never re-entered. */
let theWorker = null;


/* ---- FT_INTEGRATE (ftgrays.c:527-529) ---------------------------------
 *   ras.cell->cover = ADD_INT( ras.cell->cover, a ),
 *   ras.cell->area  = ADD_INT( ras.cell->area, (a) * (TArea)(b) )
 *
 * C emulation: `cover` and `area` are 32-bit ints, ADD_INT is a deliberately
 * wrapping 32-bit add, and `(a) * (TArea)(b)` is a 32-bit int multiply that
 * may also wrap.  Math.imul is the exact wrapping 32-bit product; the Int32Array
 * store performs the ToInt32 wrap on the sum. */
function FT_INTEGRATE( ras, a, b ) {
  const c = ras.cell;
  ras.cellCover[c] = ras.cellCover[c] + a;
  ras.cellArea[c]  = ras.cellArea[c] + Math.imul( a, b );
}


/* ---- gray_set_cell (ftgrays.c:570-630) --------------------------------
 * The C walks a singly-linked list through a `PCell*` cursor; `prev < 0`
 * models the cursor still pointing at the ycells head slot. */
function gray_set_cell( ras, ex, ey ) {
  const ey_index = ey - ras.min_ey;

  if ( ey_index < 0 || ey_index >= ras.count_ey || ex >= ras.max_ex ) {
    ras.cell = ras.cell_null;
    return;
  }

  if ( ex < ras.min_ex - 1 )
    ex = ras.min_ex - 1;                       /* FT_MAX( ex, ras.min_ex - 1 ) */

  let prev = -1;
  let cell;

  for ( ;; ) {
    cell = prev < 0 ? ras.ycells[ey_index] : ras.cellNext[prev];

    if ( ras.cellX[cell] > ex )
      break;

    if ( ras.cellX[cell] === ex ) {            /* goto Found */
      ras.cell = cell;
      return;
    }

    prev = cell;
  }

  /* insert new cell */
  cell = ras.cell_free;
  if ( cell === ras.cell_null ) {
    ras.error = Smooth_Err_Raster_Overflow;
    ras.cell  = cell;                          /* goto Found, with cell_null */
    return;
  }

  ras.cell_free = cell + 1;

  ras.cellX[cell]     = ex;
  ras.cellArea[cell]  = 0;
  ras.cellCover[cell] = 0;

  ras.cellNext[cell] = prev < 0 ? ras.ycells[ey_index] : ras.cellNext[prev];
  if ( prev < 0 ) ras.ycells[ey_index] = cell;
  else            ras.cellNext[prev]   = cell;

  ras.cell = cell;
}


/* ---- FT_UDIVPREP / FT_UDIV (ftgrays.c:394-397) ------------------------
 *   #define FT_UDIVPREP( c, b )  FT_Int64 b##_r = c ? (FT_Int64)0xFFFFFFFF / (b) : 0
 *   #define FT_UDIV( a, b )      (TCoord)( ((FT_UInt64)(a) * (FT_UInt64)(b##_r)) >> 32 )
 *
 * C emulation #1: the division is C's truncation toward zero, and `b` may be
 * negative, so the reciprocal may be negative.  A plain Math.trunc(N/b) could
 * land on the wrong side of an integer for large |b| (the double quotient
 * carries up to 2^-21 of absolute error while the true value can sit only
 * 1/|b| ~ 2^-27 away from an integer), so the result is corrected exactly;
 * q*|b| stays below 2^32 and is therefore an exact double.
 *
 * C emulation #2: note the token paste.  `FT_UDIV( -prod, -dx )` expands to
 * `(FT_UInt64)(-prod) * (FT_UInt64)(- dx_r)` -- the minus binds to the
 * *reciprocal*, not to dx.  Callers below pass the already-negated reciprocal
 * to match.  At all four call sites both factors are non-negative and the
 * branch guards bound the product by 256 * 0xFFFFFFFF < 2^40, so the plain
 * double multiply is exact and the (FT_UInt64) casts never wrap. */
function FT_UDIVPREP( cond, b ) {
  if ( !cond ) return 0;

  const N  = 4294967295;                       /* (FT_Int64)0xFFFFFFFF */
  const ab = b < 0 ? -b : b;

  let q = Math.floor( N / ab );
  if ( q * ab > N ) q -= 1;
  else if ( ( q + 1 ) * ab <= N ) q += 1;

  return b < 0 ? -q : q;
}

function FT_UDIV( a, b_r ) {
  return Math.floor( a * b_r / 4294967296 ) | 0;
}


/* ---- gray_render_scanline (ftgrays.c:639-728) -------------------------
 * NOT COMPILED in the reference build (`#ifndef FT_INT64`).  Ported for
 * completeness and kept behind the same switch as its caller. */
function gray_render_scanline( ras, ey, x1, y1, x2, y2 ) {
  let ex1 = TRUNC( x1 );
  const ex2 = TRUNC( x2 );

  if ( y1 === y2 ) { gray_set_cell( ras, ex2, ey ); return; }

  let fx1 = FRACT( x1 );
  const fx2 = FRACT( x2 );

  let first, incr, dx, dy, p, delta, mod;

  if ( ex1 === ex2 ) {                          /* goto End */
    FT_INTEGRATE( ras, y2 - y1, fx1 + fx2 );
    return;
  }

  dx = x2 - x1;
  dy = y2 - y1;

  if ( dx > 0 ) { p = ( ONE_PIXEL - fx1 ) * dy; first = ONE_PIXEL; incr =  1; }
  else          { p = fx1 * dy;                 first = 0;         incr = -1; dx = -dx; }

  /* FT_DIV_MOD: C division truncates, then the remainder is forced positive */
  delta = Math.trunc( p / dx );
  mod   = p - delta * dx;
  if ( mod < 0 ) { delta -= 1; mod += dx; }

  FT_INTEGRATE( ras, delta, fx1 + first );
  y1  += delta;
  ex1 += incr;
  gray_set_cell( ras, ex1, ey );

  if ( ex1 !== ex2 ) {
    p = ONE_PIXEL * dy;
    let lift = Math.trunc( p / dx );
    let rem  = p - lift * dx;
    if ( rem < 0 ) { lift -= 1; rem += dx; }

    do {
      delta = lift;
      mod  += rem;
      if ( mod >= dx ) { mod -= dx; delta += 1; }

      FT_INTEGRATE( ras, delta, ONE_PIXEL );
      y1  += delta;
      ex1 += incr;
      gray_set_cell( ras, ex1, ey );
    } while ( ex1 !== ex2 );
  }

  fx1 = ONE_PIXEL - first;

  FT_INTEGRATE( ras, y2 - y1, fx1 + fx2 );      /* End: */
}


/* ---- gray_render_line, !FT_INT64 variant (ftgrays.c:735-865) ----------
 * NOT COMPILED in the reference build. */
function gray_render_line_32( ras, to_x, to_y ) {
  let ey1 = TRUNC( ras.y );
  const ey2 = TRUNC( to_y );

  if ( ( ey1 >= ras.max_ey && ey2 >= ras.max_ey ) ||
       ( ey1 <  ras.min_ey && ey2 <  ras.min_ey ) ) {
    ras.x = to_x; ras.y = to_y; return;
  }

  const fy1 = FRACT( ras.y );
  const fy2 = FRACT( to_y );

  if ( ey1 === ey2 ) {
    gray_render_scanline( ras, ey1, ras.x, fy1, to_x, fy2 );
    ras.x = to_x; ras.y = to_y; return;
  }

  let dx = to_x - ras.x;
  let dy = to_y - ras.y;

  let first, incr, delta, mod, p, x, x2;

  if ( dx === 0 ) {
    const ex     = TRUNC( ras.x );
    const two_fx = FRACT( ras.x ) * 2;

    if ( dy > 0 ) { first = ONE_PIXEL; incr =  1; }
    else          { first = 0;         incr = -1; }

    delta = first - fy1;
    FT_INTEGRATE( ras, delta, two_fx );
    ey1 += incr;
    gray_set_cell( ras, ex, ey1 );

    delta = first + first - ONE_PIXEL;
    while ( ey1 !== ey2 ) {
      FT_INTEGRATE( ras, delta, two_fx );
      ey1 += incr;
      gray_set_cell( ras, ex, ey1 );
    }

    delta = fy2 - ONE_PIXEL + first;
    FT_INTEGRATE( ras, delta, two_fx );

    ras.x = to_x; ras.y = to_y; return;
  }

  if ( dy > 0 ) { p = ( ONE_PIXEL - fy1 ) * dx; first = ONE_PIXEL; incr =  1; }
  else          { p = fy1 * dx;                 first = 0;         incr = -1; dy = -dy; }

  delta = Math.trunc( p / dy );
  mod   = p - delta * dy;
  if ( mod < 0 ) { delta -= 1; mod += dy; }

  x = ras.x + delta;
  gray_render_scanline( ras, ey1, ras.x, fy1, x, first );

  ey1 += incr;
  gray_set_cell( ras, TRUNC( x ), ey1 );

  if ( ey1 !== ey2 ) {
    p = ONE_PIXEL * dx;
    let lift = Math.trunc( p / dy );
    let rem  = p - lift * dy;
    if ( rem < 0 ) { lift -= 1; rem += dy; }

    do {
      delta = lift;
      mod  += rem;
      if ( mod >= dy ) { mod -= dy; delta += 1; }

      x2 = x + delta;
      gray_render_scanline( ras, ey1, x, ONE_PIXEL - first, x2, first );
      x = x2;

      ey1 += incr;
      gray_set_cell( ras, TRUNC( x ), ey1 );
    } while ( ey1 !== ey2 );
  }

  gray_render_scanline( ras, ey1, x, ONE_PIXEL - first, to_x, fy2 );

  ras.x = to_x; ras.y = to_y;
}


/* ---- gray_render_line, FT_INT64 variant (ftgrays.c:873-997) -----------
 * THIS IS THE ONE THE DEFAULT BUILD COMPILES. */
function gray_render_line_64( ras, to_x, to_y ) {
  let ey1 = TRUNC( ras.y );
  const ey2 = TRUNC( to_y );

  /* perform vertical clipping */
  if ( ( ey1 >= ras.max_ey && ey2 >= ras.max_ey ) ||
       ( ey1 <  ras.min_ey && ey2 <  ras.min_ey ) ) {
    ras.x = to_x; ras.y = to_y; return;
  }

  let ex1 = TRUNC( ras.x );
  const ex2 = TRUNC( to_x );

  let fx1 = FRACT( ras.x );
  let fy1 = FRACT( ras.y );
  let fx2, fy2;

  const dx = to_x - ras.x;
  const dy = to_y - ras.y;

  if ( ex1 === ex2 && ey1 === ey2 ) {
    /* inside one cell */
  } else if ( dy === 0 ) {                      /* any horizontal line */
    gray_set_cell( ras, ex2, ey2 );
    ras.x = to_x; ras.y = to_y; return;         /* goto End */
  } else if ( dx === 0 ) {
    if ( dy > 0 ) {                             /* vertical line up */
      do {
        fy2 = ONE_PIXEL;
        FT_INTEGRATE( ras, fy2 - fy1, fx1 * 2 );
        fy1 = 0;
        ey1++;
        gray_set_cell( ras, ex1, ey1 );
      } while ( ey1 !== ey2 );
    } else {                                    /* vertical line down */
      do {
        fy2 = 0;
        FT_INTEGRATE( ras, fy2 - fy1, fx1 * 2 );
        fy1 = ONE_PIXEL;
        ey1--;
        gray_set_cell( ras, ex1, ey1 );
      } while ( ey1 !== ey2 );
    }
  } else {                                      /* any other line */
    /* prod stays bounded by (|dx|+|dy|)*256 and is an exact double. */
    let prod = dx * fy1 - dy * fx1;
    const dx_r = FT_UDIVPREP( ex1 !== ex2, dx );
    const dy_r = FT_UDIVPREP( ey1 !== ey2, dy );

    do {
      if ( prod - dx * ONE_PIXEL > 0 && prod <= 0 ) {                  /* left */
        fx2 = 0;
        fy2 = FT_UDIV( -prod, -dx_r );
        prod -= dy * ONE_PIXEL;
        FT_INTEGRATE( ras, fy2 - fy1, fx1 + fx2 );
        fx1 = ONE_PIXEL;
        fy1 = fy2;
        ex1--;
      } else if ( prod - dx * ONE_PIXEL + dy * ONE_PIXEL > 0 &&
                  prod - dx * ONE_PIXEL <= 0 ) {                       /* up */
        prod -= dx * ONE_PIXEL;
        fx2 = FT_UDIV( -prod, dy_r );
        fy2 = ONE_PIXEL;
        FT_INTEGRATE( ras, fy2 - fy1, fx1 + fx2 );
        fx1 = fx2;
        fy1 = 0;
        ey1++;
      } else if ( prod + dy * ONE_PIXEL >= 0 &&
                  prod - dx * ONE_PIXEL + dy * ONE_PIXEL <= 0 ) {      /* right */
        prod += dy * ONE_PIXEL;
        fx2 = ONE_PIXEL;
        fy2 = FT_UDIV( prod, dx_r );
        FT_INTEGRATE( ras, fy2 - fy1, fx1 + fx2 );
        fx1 = 0;
        fy1 = fy2;
        ex1++;
      } else {                                                          /* down */
        fx2 = FT_UDIV( prod, -dy_r );
        fy2 = 0;
        prod += dx * ONE_PIXEL;
        FT_INTEGRATE( ras, fy2 - fy1, fx1 + fx2 );
        fx1 = fx2;
        fy1 = ONE_PIXEL;
        ey1--;
      }

      gray_set_cell( ras, ex1, ey1 );

    } while ( ex1 !== ex2 || ey1 !== ey2 );
  }

  fx2 = FRACT( to_x );
  fy2 = FRACT( to_y );

  FT_INTEGRATE( ras, fy2 - fy1, fx1 + fx2 );

  ras.x = to_x;
  ras.y = to_y;
}

const gray_render_line = FT_INT64 ? gray_render_line_64 : gray_render_line_32;


/* ---- gray_render_conic, FT_INT64 DDA variant (ftgrays.c:1012-1141) ----
 * THIS IS THE ONE THE DEFAULT BUILD COMPILES.
 *
 * C emulation: px/py are 64-bit accumulators holding p0 << 32, i.e. up to
 * 2^56 -- past 2^53, so a JS double cannot hold them.  They are kept as two
 * 32-bit limbs (hi signed, lo unsigned, value = hi*2^32 + lo); `px >> 32` is
 * then exactly the hi limb.  Setup uses BigInt once per curve (cheap); the
 * inner loop is pure double arithmetic. */
function gray_render_conic_dda( ras, cx, cy, tx, ty ) {
  const p0x = ras.x, p0y = ras.y;
  const p1x = UPSCALE( cx ), p1y = UPSCALE( cy );
  const p2x = UPSCALE( tx ), p2y = UPSCALE( ty );

  /* short-cut the arc that crosses the current band */
  if ( ( TRUNC( p0y ) >= ras.max_ey &&
         TRUNC( p1y ) >= ras.max_ey &&
         TRUNC( p2y ) >= ras.max_ey ) ||
       ( TRUNC( p0y ) <  ras.min_ey &&
         TRUNC( p1y ) <  ras.min_ey &&
         TRUNC( p2y ) <  ras.min_ey ) ) {
    ras.x = p2x; ras.y = p2y; return;
  }

  const bx = p1x - p0x;
  const by = p1y - p0y;
  const ax = p2x - p1x - bx;
  const ay = p2y - p1y - by;

  let dx = ax < 0 ? -ax : ax;
  const dy = ay < 0 ? -ay : ay;
  if ( dx < dy ) dx = dy;

  if ( dx <= ONE_PIXEL / 4 ) {
    gray_render_line( ras, p2x, p2y );
    return;
  }

  /* `dx >>= 2` on a TPos: arithmetic shift of a non-negative 64-bit value */
  let shift = 16;
  do {
    dx = Math.floor( dx / 4 );
    shift--;
  } while ( dx > ONE_PIXEL / 4 );

  let count = 0x10000 >>> shift;

  /* LEFT_SHIFT( a, b ) == (FT_Int64)( (FT_UInt64)(a) << (b) ) */
  const LSH = ( a, b ) =>
    BigInt.asIntN( 64, BigInt.asUintN( 64, BigInt( a ) << BigInt( b ) ) );

  let rxB = LSH( ax, shift + shift );
  let ryB = LSH( ay, shift + shift );
  const qxB = BigInt.asIntN( 64, LSH( bx, shift + 17 ) + rxB );
  const qyB = BigInt.asIntN( 64, LSH( by, shift + 17 ) + ryB );
  rxB = BigInt.asIntN( 64, rxB * 2n );
  ryB = BigInt.asIntN( 64, ryB * 2n );
  const pxB = LSH( p0x, 32 );
  const pyB = LSH( p0y, 32 );

  const HI = v => Number( BigInt.asIntN( 32, v >> 32n ) );
  const LO = v => Number( BigInt.asUintN( 32, v ) );

  let pxH = HI( pxB ), pxL = LO( pxB );
  let pyH = HI( pyB ), pyL = LO( pyB );
  let qxH = HI( qxB ), qxL = LO( qxB );
  let qyH = HI( qyB ), qyL = LO( qyB );
  const rxH = HI( rxB ), rxL = LO( rxB );
  const ryH = HI( ryB ), ryL = LO( ryB );

  const TWO32 = 4294967296;

  do {
    pxL += qxL;
    if ( pxL >= TWO32 ) { pxL -= TWO32; pxH = ( pxH + qxH + 1 ) | 0; }
    else                { pxH = ( pxH + qxH ) | 0; }

    pyL += qyL;
    if ( pyL >= TWO32 ) { pyL -= TWO32; pyH = ( pyH + qyH + 1 ) | 0; }
    else                { pyH = ( pyH + qyH ) | 0; }

    qxL += rxL;
    if ( qxL >= TWO32 ) { qxL -= TWO32; qxH = ( qxH + rxH + 1 ) | 0; }
    else                { qxH = ( qxH + rxH ) | 0; }

    qyL += ryL;
    if ( qyL >= TWO32 ) { qyL -= TWO32; qyH = ( qyH + ryH + 1 ) | 0; }
    else                { qyH = ( qyH + ryH ) | 0; }

    gray_render_line( ras, pxH, pyH );          /* (FT_Pos)( px >> 32 ) */
  } while ( --count );
}


/* ---- gray_split_conic (ftgrays.c:1150-1169) ---------------------------
 * NOT COMPILED in the reference build (the not-FT_INT64 half).
 * `>> 1` / `>> 2` on a TPos are arithmetic shifts, i.e. floor. */
function gray_split_conic( bx, by, i ) {
  let a, b;

  bx[i + 4] = bx[i + 2];
  a = bx[i] + bx[i + 1];
  b = bx[i + 1] + bx[i + 2];
  bx[i + 3] = Math.floor( b / 2 );
  bx[i + 2] = Math.floor( ( a + b ) / 4 );
  bx[i + 1] = Math.floor( a / 2 );

  by[i + 4] = by[i + 2];
  a = by[i] + by[i + 1];
  b = by[i + 1] + by[i + 2];
  by[i + 3] = Math.floor( b / 2 );
  by[i + 2] = Math.floor( ( a + b ) / 4 );
  by[i + 1] = Math.floor( a / 2 );
}

/* gray_render_conic, !FT_INT64 bisecting variant (ftgrays.c:1172-1235).
 * NOT COMPILED in the reference build. */
function gray_render_conic_split( ras, cx, cy, tx, ty ) {
  const bx = new Float64Array( 16 * 2 + 1 );
  const by = new Float64Array( 16 * 2 + 1 );
  let arc = 0;

  bx[0] = UPSCALE( tx ); by[0] = UPSCALE( ty );
  bx[1] = UPSCALE( cx ); by[1] = UPSCALE( cy );
  bx[2] = ras.x;         by[2] = ras.y;

  if ( ( TRUNC( by[0] ) >= ras.max_ey &&
         TRUNC( by[1] ) >= ras.max_ey &&
         TRUNC( by[2] ) >= ras.max_ey ) ||
       ( TRUNC( by[0] ) <  ras.min_ey &&
         TRUNC( by[1] ) <  ras.min_ey &&
         TRUNC( by[2] ) <  ras.min_ey ) ) {
    ras.x = bx[0]; ras.y = by[0]; return;
  }

  let dx = Math.abs( bx[2] + bx[0] - 2 * bx[1] );
  const dy = Math.abs( by[2] + by[0] - 2 * by[1] );
  if ( dx < dy ) dx = dy;

  let draw = 1;
  while ( dx > ONE_PIXEL / 4 ) {
    dx = Math.floor( dx / 4 );
    draw <<= 1;
  }

  do {
    let split = draw & ( -draw );
    while ( ( split >>= 1 ) ) {
      gray_split_conic( bx, by, arc );
      arc += 2;
    }

    gray_render_line( ras, bx[arc], by[arc] );
    arc -= 2;
  } while ( --draw );
}

const gray_render_conic = FT_INT64 ? gray_render_conic_dda
                                   : gray_render_conic_split;


/* ---- gray_split_cubic (ftgrays.c:1248-1277) --------------------------- */
function gray_split_cubic( bx, by, i ) {
  let a, b, c;

  bx[i + 6] = bx[i + 3];
  a = bx[i]     + bx[i + 1];
  b = bx[i + 1] + bx[i + 2];
  c = bx[i + 2] + bx[i + 3];
  bx[i + 5] = Math.floor( c / 2 );
  c += b;
  bx[i + 4] = Math.floor( c / 4 );
  bx[i + 1] = Math.floor( a / 2 );
  a += b;
  bx[i + 2] = Math.floor( a / 4 );
  bx[i + 3] = Math.floor( ( a + c ) / 8 );

  by[i + 6] = by[i + 3];
  a = by[i]     + by[i + 1];
  b = by[i + 1] + by[i + 2];
  c = by[i + 2] + by[i + 3];
  by[i + 5] = Math.floor( c / 2 );
  c += b;
  by[i + 4] = Math.floor( c / 4 );
  by[i + 1] = Math.floor( a / 2 );
  a += b;
  by[i + 2] = Math.floor( a / 4 );
  by[i + 3] = Math.floor( ( a + c ) / 8 );
}


/* ---- gray_render_cubic (ftgrays.c:1280-1336) -------------------------- */
function gray_render_cubic( ras, c1x, c1y, c2x, c2y, tx, ty ) {
  const bx = new Float64Array( 16 * 3 + 1 );
  const by = new Float64Array( 16 * 3 + 1 );
  let arc = 0;

  bx[0] = UPSCALE( tx );  by[0] = UPSCALE( ty );
  bx[1] = UPSCALE( c2x ); by[1] = UPSCALE( c2y );
  bx[2] = UPSCALE( c1x ); by[2] = UPSCALE( c1y );
  bx[3] = ras.x;          by[3] = ras.y;

  if ( ( TRUNC( by[0] ) >= ras.max_ey &&
         TRUNC( by[1] ) >= ras.max_ey &&
         TRUNC( by[2] ) >= ras.max_ey &&
         TRUNC( by[3] ) >= ras.max_ey ) ||
       ( TRUNC( by[0] ) <  ras.min_ey &&
         TRUNC( by[1] ) <  ras.min_ey &&
         TRUNC( by[2] ) <  ras.min_ey &&
         TRUNC( by[3] ) <  ras.min_ey ) ) {
    ras.x = bx[0]; ras.y = by[0]; return;
  }

  for ( ;; ) {
    if ( Math.abs( 2 * bx[arc] - 3 * bx[arc + 1] + bx[arc + 3] ) > ONE_PIXEL / 2 ||
         Math.abs( 2 * by[arc] - 3 * by[arc + 1] + by[arc + 3] ) > ONE_PIXEL / 2 ||
         Math.abs( bx[arc] - 3 * bx[arc + 2] + 2 * bx[arc + 3] ) > ONE_PIXEL / 2 ||
         Math.abs( by[arc] - 3 * by[arc + 2] + 2 * by[arc + 3] ) > ONE_PIXEL / 2 ) {
      gray_split_cubic( bx, by, arc );
      arc += 3;
      continue;
    }

    gray_render_line( ras, bx[arc], by[arc] );

    if ( arc === 0 ) return;

    arc -= 3;
  }
}


/* ---- decomposition callbacks (ftgrays.c:1339-1400) -------------------- */
function gray_move_to( ras, tx, ty ) {
  const x = UPSCALE( tx );
  const y = UPSCALE( ty );

  gray_set_cell( ras, TRUNC( x ), TRUNC( y ) );

  ras.x = x;
  ras.y = y;

  return ras.error;
}

function gray_line_to( ras, tx, ty ) {
  gray_render_line( ras, UPSCALE( tx ), UPSCALE( ty ) );
  return ras.error;
}

function gray_conic_to( ras, cx, cy, tx, ty ) {
  gray_render_conic( ras, cx, cy, tx, ty );
  return ras.error;
}

function gray_cubic_to( ras, c1x, c1y, c2x, c2y, tx, ty ) {
  gray_render_cubic( ras, c1x, c1y, c2x, c2y, tx, ty );
  return ras.error;
}


/* ---- FT_Outline_Decompose (src/base/ftoutln.c:43-300) -----------------
 * The non-standalone build uses THIS copy, not the one inlined in ftgrays.c
 * under STANDALONE_.  The only textual difference is that ftoutln.c has
 * the `v_last = v_start` assignment commented out in the both-ends-conic case; v_last
 * is dead afterwards either way, so behaviour is identical.
 *
 * func_interface has shift == 0 and delta == 0 (ftgrays.c:1690-1700), so
 * SCALED( x ) == x and no scaling is applied here.
 *
 * C emulation: the midpoint averages use `/ 2` on FT_Pos, which TRUNCATES
 * toward zero -- unlike the `>> 1` in gray_split_conic/cubic, which floors.
 * Math.trunc is therefore correct here and Math.floor would be wrong. */
export function FT_Outline_Decompose( ras, outline ) {
  const px = outline.x, py = outline.y;
  const tags = outline.tags, contours = outline.contours;

  let error = Smooth_Err_Ok;
  let last = -1;

  for ( let n = 0; n < outline.n_contours; n++ ) {
    const first = last + 1;
    last = contours[n];
    if ( last < first ) return Smooth_Err_Invalid_Outline;

    let limit = last;

    let vsx = px[first], vsy = py[first];
    const vlx = px[last], vly = py[last];
    let vcx = vsx, vcy = vsy;

    let point = first;
    let tag = tags[first] & 3;

    /* A contour cannot start with a cubic control point! */
    if ( tag === FT_CURVE_TAG_CUBIC ) return Smooth_Err_Invalid_Outline;

    if ( tag === FT_CURVE_TAG_CONIC ) {
      if ( ( tags[last] & 3 ) === FT_CURVE_TAG_ON ) {
        vsx = vlx; vsy = vly;
        limit--;
      } else {
        vsx = Math.trunc( ( vsx + vlx ) / 2 );
        vsy = Math.trunc( ( vsy + vly ) / 2 );
      }
      point--;
    }

    error = gray_move_to( ras, vsx, vsy );
    if ( error ) return error;

    closeContour:
    {
      while ( point < limit ) {
        point++;
        tag = tags[point] & 3;

        if ( tag === FT_CURVE_TAG_ON ) {            /* emit a single line_to */
          error = gray_line_to( ras, px[point], py[point] );
          if ( error ) return error;
          continue;
        }

        if ( tag === FT_CURVE_TAG_CONIC ) {         /* consume conic arcs */
          vcx = px[point]; vcy = py[point];

          for ( ;; ) {                              /* Do_Conic: */
            if ( point < limit ) {
              point++;
              tag = tags[point] & 3;

              const vx = px[point], vy = py[point];

              if ( tag === FT_CURVE_TAG_ON ) {
                error = gray_conic_to( ras, vcx, vcy, vx, vy );
                if ( error ) return error;
                break;
              }

              if ( tag !== FT_CURVE_TAG_CONIC ) return Smooth_Err_Invalid_Outline;

              const vmx = Math.trunc( ( vcx + vx ) / 2 );
              const vmy = Math.trunc( ( vcy + vy ) / 2 );

              error = gray_conic_to( ras, vcx, vcy, vmx, vmy );
              if ( error ) return error;

              vcx = vx; vcy = vy;
              continue;
            }

            error = gray_conic_to( ras, vcx, vcy, vsx, vsy );
            break closeContour;                     /* goto Close */
          }
          continue;
        }

        /* FT_CURVE_TAG_CUBIC */
        if ( point + 1 > limit || ( tags[point + 1] & 3 ) !== FT_CURVE_TAG_CUBIC )
          return Smooth_Err_Invalid_Outline;

        point += 2;

        const v1x = px[point - 2], v1y = py[point - 2];
        const v2x = px[point - 1], v2y = py[point - 1];

        if ( point <= limit ) {
          error = gray_cubic_to( ras, v1x, v1y, v2x, v2y, px[point], py[point] );
          if ( error ) return error;
          continue;
        }

        error = gray_cubic_to( ras, v1x, v1y, v2x, v2y, vsx, vsy );
        break closeContour;                          /* goto Close */
      }

      /* close the contour with a line segment */
      error = gray_line_to( ras, vsx, vsy );
    }

    /* Close: */
    if ( error ) return error;
  }

  return Smooth_Err_Ok;
}


/* ---- FT_FILL_RULE (ftgrays.c:405-412) ---------------------------------
 *   coverage = (int)( area >> ( PIXEL_BITS * 2 + 1 - 8 ) );   // >> 9
 *   if ( coverage & fill )            coverage = ~coverage;
 *   if ( coverage > 255 && fill & INT_MIN ) coverage = 255;
 *
 * `area` is already a 32-bit int here, so JS `>>` and `&` are exactly right. */
function FT_FILL_RULE( area, fill ) {
  let coverage = area >> ( PIXEL_BITS * 2 + 1 - 8 );

  if ( coverage & fill )
    coverage = ~coverage;

  if ( coverage > 255 && ( fill & INT_MIN ) )
    coverage = 255;

  return coverage;
}


/* ---- gray_sweep (ftgrays.c:1728-1775) --------------------------------- */
function gray_sweep( ras ) {
  const fill = ( ras.outline.flags & FT_OUTLINE_EVEN_ODD_FILL ) ? 0x100 : INT_MIN;
  const buf = ras.targetBuf;
  const pitch = ras.targetPitch;

  for ( let y = ras.min_ey; y < ras.max_ey; y++ ) {
    let cell = ras.ycells[y - ras.min_ey];
    let x = ras.min_ex;
    let cover = 0;

    const line = ras.targetOrigin - pitch * y;

    for ( ; cell !== ras.cell_null; cell = ras.cellNext[cell] ) {
      const cx = ras.cellX[cell];

      if ( cover !== 0 && cx > x ) {
        const coverage = FT_FILL_RULE( cover, fill ) & 255;
        buf.fill( coverage, line + x, line + cx );
      }

      /* 32-bit int accumulation, expected to wrap */
      cover = ( cover + Math.imul( ras.cellCover[cell], ONE_PIXEL * 2 ) ) | 0;
      const area = ( cover - ras.cellArea[cell] ) | 0;

      if ( area !== 0 && cx >= ras.min_ex )
        buf[line + cx] = FT_FILL_RULE( area, fill ) & 255;

      x = cx + 1;
    }

    if ( cover !== 0 ) {                        /* only if cropped */
      const coverage = FT_FILL_RULE( cover, fill ) & 255;
      buf.fill( coverage, line + x, line + ras.max_ex );
    }
  }
}


/* ---- gray_sweep_direct (ftgrays.c:1778-1858) -------------------------- */
function gray_sweep_direct( ras ) {
  const fill = ( ras.outline.flags & FT_OUTLINE_EVEN_ODD_FILL ) ? 0x100 : INT_MIN;

  const spanX   = new Int32Array( FT_MAX_GRAY_SPANS );
  const spanLen = new Int32Array( FT_MAX_GRAY_SPANS );
  const spanCov = new Int32Array( FT_MAX_GRAY_SPANS );
  let n = 0;

  for ( let y = ras.min_ey; y < ras.max_ey; y++ ) {
    let cell = ras.ycells[y - ras.min_ey];
    let x = ras.min_ex;
    let cover = 0;

    for ( ; cell !== ras.cell_null; cell = ras.cellNext[cell] ) {
      const cx = ras.cellX[cell];

      if ( cover !== 0 && cx > x ) {
        spanCov[n] = FT_FILL_RULE( cover, fill ) & 255;
        spanX[n]   = x & 0xFFFF;                /* (unsigned short) */
        spanLen[n] = ( cx - x ) & 0xFFFF;

        if ( ++n === FT_MAX_GRAY_SPANS ) {
          ras.renderSpan( y, n, spanX, spanLen, spanCov, ras.renderSpanData );
          n = 0;
        }
      }

      cover = ( cover + Math.imul( ras.cellCover[cell], ONE_PIXEL * 2 ) ) | 0;
      const area = ( cover - ras.cellArea[cell] ) | 0;

      if ( area !== 0 && cx >= ras.min_ex ) {
        spanCov[n] = FT_FILL_RULE( area, fill ) & 255;
        spanX[n]   = cx & 0xFFFF;
        spanLen[n] = 1;

        if ( ++n === FT_MAX_GRAY_SPANS ) {
          ras.renderSpan( y, n, spanX, spanLen, spanCov, ras.renderSpanData );
          n = 0;
        }
      }

      x = cx + 1;
    }

    if ( cover !== 0 ) {
      spanCov[n] = FT_FILL_RULE( cover, fill ) & 255;
      spanX[n]   = x & 0xFFFF;
      spanLen[n] = ( ras.max_ex - x ) & 0xFFFF;
      ++n;
    }

    if ( n ) {
      ras.renderSpan( y, n, spanX, spanLen, spanCov, ras.renderSpanData );
      n = 0;
    }
  }
}


/* ---- gray_convert_glyph_inner (ftgrays.c:1703-1725) ------------------- */
function gray_convert_glyph_inner( ras ) {
  return FT_Outline_Decompose( ras, ras.outline );
}


/* ---- gray_convert_glyph (ftgrays.c:1861-1960) ------------------------- */
function gray_convert_glyph( ras ) {
  let height = ras.cbox.yMax - ras.cbox.yMin;
  let n = Math.floor( FT_MAX_GRAY_POOL / 8 );          /* 85 */
  const bands = new Int32Array( 32 );
  let error = Smooth_Err_Ok;

  /* the null cell: dumpster and list terminator */
  ras.cell_null = FT_MAX_GRAY_POOL - 1;
  ras.cellX[ras.cell_null]     = CELL_MAX_X_VALUE;
  ras.cellArea[ras.cell_null]  = 0;
  ras.cellCover[ras.cell_null] = 0;
  ras.cellNext[ras.cell_null]  = ras.cell_null;

  if ( height > n ) {
    n      = Math.floor( ( height + n - 1 ) / n );     /* two divisions */
    height = Math.floor( ( height + n - 1 ) / n );     /* rounded up    */
  }

  for ( let y = ras.cbox.yMin; y < ras.cbox.yMax; ) {
    ras.min_ey = y;
    y += height;
    ras.max_ey = Math.min( y, ras.cbox.yMax );

    ras.count_ey = ras.max_ey - ras.min_ey;

    let band = 0;                                      /* band = bands */
    bands[1] = ras.cbox.xMin;
    bands[0] = ras.cbox.xMax;

    do {
      ras.min_ex = bands[band + 1];
      ras.max_ex = bands[band];

      for ( let i = 0; i < ras.count_ey; ++i )
        ras.ycells[i] = ras.cell_null;

      /* ycells overlays the head of the cell pool in the C, so the cells it
       * occupies are unavailable; reproduce the exact usable capacity. */
      n = Math.floor( ( ras.count_ey * SIZEOF_PCELL + SIZEOF_TCELL - 1 )
                      / SIZEOF_TCELL );

      ras.cell_free = n;
      ras.cell      = ras.cell_null;
      ras.error     = Smooth_Err_Ok;

      error = gray_convert_glyph_inner( ras );

      if ( !error ) {
        if ( ras.renderSpan ) gray_sweep_direct( ras );
        else                  gray_sweep( ras );
        band--;
        continue;
      } else if ( error !== Smooth_Err_Raster_Overflow ) {
        return error;
      }

      /* render pool overflow; reduce the render band by half */
      const i = Math.floor( ( bands[band] - bands[band + 1] ) / 2 );

      if ( i === 0 )                                   /* rotten glyph */
        return Smooth_Err_Raster_Overflow;

      band++;
      bands[band + 1] = bands[band];
      bands[band]    += i;
    } while ( band >= 0 );
  }

  return Smooth_Err_Ok;
}


/* ---- gray_raster_render (ftgrays.c:1963-2043) -------------------------
 * NOTE: ft_smooth_render calls raster_render directly, so the
 * |cbox| <= 0x1000000 rejection that lives in FT_Outline_Render is NOT part
 * of this path; the size limit comes from ft_glyphslot_preset_bitmap instead
 * (see smooth.js). */
export function grayRasterRender( params ) {
  const outline    = params.source;
  const target_map = params.target;

  if ( !( params.flags & FT_RASTER_FLAG_AA ) )
    return Smooth_Err_Cannot_Render_Glyph;

  if ( !outline )
    return Smooth_Err_Invalid_Outline;

  if ( outline.n_points === 0 || outline.n_contours === 0 )
    return Smooth_Err_Ok;

  if ( !outline.contours || !outline.x )
    return Smooth_Err_Invalid_Outline;

  if ( outline.n_points !== outline.contours[outline.n_contours - 1] + 1 )
    return Smooth_Err_Invalid_Outline;

  if ( theWorker === null ) theWorker = newWorker();
  const ras = theWorker;

  ras.outline = outline;

  if ( params.flags & FT_RASTER_FLAG_DIRECT ) {
    if ( !params.gray_spans ) return Smooth_Err_Ok;

    ras.renderSpan     = params.gray_spans;
    ras.renderSpanData = params.user;

    ras.cbox.xMin = params.clip_box.xMin;
    ras.cbox.yMin = params.clip_box.yMin;
    ras.cbox.xMax = params.clip_box.xMax;
    ras.cbox.yMax = params.clip_box.yMax;
  } else {
    if ( !target_map ) return Smooth_Err_Invalid_Argument;

    if ( !target_map.width || !target_map.rows ) return Smooth_Err_Ok;

    if ( !target_map.buffer ) return Smooth_Err_Invalid_Argument;

    /* pitch < 0 means the buffer already starts at the bottom-left origin */
    ras.targetOrigin = target_map.pitch < 0
                         ? 0
                         : ( target_map.rows - 1 ) * target_map.pitch;
    ras.targetPitch  = target_map.pitch;
    ras.targetBuf    = target_map.buffer;

    ras.renderSpan     = null;
    ras.renderSpanData = null;

    ras.cbox.xMin = 0;
    ras.cbox.yMin = 0;
    ras.cbox.xMax = target_map.width;
    ras.cbox.yMax = target_map.rows;
  }

  if ( ras.cbox.xMin >= ras.cbox.xMax || ras.cbox.yMin >= ras.cbox.yMax )
    return Smooth_Err_Ok;

  return gray_convert_glyph( ras );
}


/* ---- public convenience wrapper (CONTRACT.md) -------------------------
 * renderGray(outline, width, rows) -> Uint8Array coverage, pitch == width.
 * Throws on a non-zero raster error, since the contract's signature has no
 * error channel; smooth.js uses grayRasterRender directly to see the code. */
export function renderGray( outline, width, rows ) {
  const buffer = new Uint8Array( width * rows );

  const error = grayRasterRender( {
    target: { width, rows, pitch: width, buffer },
    source: outline,
    flags:  FT_RASTER_FLAG_AA,
  } );

  if ( error ) {
    const e = new Error( 'renderGray: raster error 0x' + error.toString( 16 ) );
    e.ftError = error;
    throw e;
  }

  return buffer;
}
