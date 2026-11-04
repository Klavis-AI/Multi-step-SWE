/* glyf.js -- ../freetype/src/truetype/ttgload.c, the UNHINTED path.
 *
 * C function -> JS function map
 *   TT_Load_Glyph_Header            -> readGlyphHeader
 *   TT_Load_Simple_Glyph            -> loadSimpleGlyph
 *   TT_Load_Composite_Glyph         -> loadCompositeGlyph
 *   tt_get_metrics / TT_Get_HMetrics / TT_Get_VMetrics -> ttGetMetrics
 *   tt_loader_set_pp                -> ttLoaderSetPP
 *   TT_Process_Simple_Glyph         -> processSimpleGlyph
 *   TT_Process_Composite_Component  -> processCompositeComponent
 *   load_truetype_glyph             -> loadTrueTypeGlyph
 *   compute_glyph_metrics           -> computeGlyphMetrics
 *   TT_Load_Glyph (+ the tail of FT_Load_Glyph) -> loadGlyfOutlineScaled
 *   FT_GlyphLoader_{Rewind,Add,Prepare}         -> GlyphLoader
 *
 * Hinting (TT_Hint_Glyph, TT_Process_Composite_Glyph, the bytecode
 * interpreter and everything under IS_HINTED) is W4's tthint.js and is NOT
 * reimplemented here; every `IS_HINTED( load_flags )` test below is
 * statically false.
 */

import {
  FT_MulFix, FT_MulDiv, FT_Hypot, FT_PIX_ROUND,
  FT_Vector_Transform, FT_Outline_Transform, FT_Outline_Translate,
  FT_Outline_Get_CBox, FT_DivFix,
} from './ftscale.js';

/* ttgload.c: simple glyph flags */
const ON_CURVE_POINT = 0x01;
const X_SHORT_VECTOR = 0x02;
const Y_SHORT_VECTOR = 0x04;
const REPEAT_FLAG    = 0x08;
const X_POSITIVE     = 0x10;   /* == SAME_X */
const Y_POSITIVE     = 0x20;   /* == SAME_Y */
const OVERLAP_SIMPLE = 0x40;

/* ttgload.c: composite glyph flags */
const ARGS_ARE_WORDS            = 0x0001;
const ARGS_ARE_XY_VALUES        = 0x0002;
const ROUND_XY_TO_GRID          = 0x0004;
const WE_HAVE_A_SCALE           = 0x0008;
const MORE_COMPONENTS           = 0x0020;
const WE_HAVE_AN_XY_SCALE       = 0x0040;
const WE_HAVE_A_2X2             = 0x0080;
const USE_MY_METRICS            = 0x0200;
const OVERLAP_COMPOUND          = 0x0400;
const SCALED_COMPONENT_OFFSET   = 0x0800;
/* TT_CONFIG_OPTION_COMPONENT_OFFSET_SCALED is #undef'd in ftoption.h, so the
 * "scale the offset" test in TT_Process_Composite_Component reads
 * `( subglyph->flags & SCALED_COMPONENT_OFFSET )'. */

/* ftimage.h */
export const FT_OUTLINE_OVERLAP        = 0x40;
export const FT_OUTLINE_HIGH_PRECISION = 0x100;
export const FT_OUTLINE_SINGLE_PASS    = 0x200;

/* fterrdef.h */
export const Err_Invalid_Outline   = 0x14;
export const Err_Invalid_Composite = 0x15;
export const Err_Too_Many_Hints    = 0x16;

class FTError extends Error {
  constructor( code ) { super( 'FT error 0x' + code.toString( 16 ) ); this.code = code; }
}
const THROW = ( c ) => { throw new FTError( c ); };

/* ---- big-endian readers; C's FT_NEXT_* on a frame ---------------------- */
const u8  = ( d, p ) => d[p];
const i8  = ( d, p ) => ( d[p] << 24 ) >> 24;                       /* FT_NEXT_CHAR */
const u16 = ( d, p ) => ( d[p] << 8 ) | d[p + 1];
const i16 = ( d, p ) => ( ( ( d[p] << 8 ) | d[p + 1] ) << 16 ) >> 16; /* FT_NEXT_SHORT */

/* ---- FT_GlyphLoader (../freetype/src/base/ftgloadr.c) ------------------ *
 * base.outline.points/tags/contours are one flat array; `current' is the
 * window that starts at base.n_points / base.n_contours.  FT_GlyphLoader_Add
 * bumps every contour index of `current' by base.n_points and then commits.
 */
class GlyphLoader {
  constructor() {
    this.X = [];          /* base.outline.points[].x  (flat) */
    this.Y = [];
    this.T = [];          /* base.outline.tags[] */
    this.C = [];          /* base.outline.contours[] */
    this.n_points   = 0;  /* base.outline.n_points */
    this.n_contours = 0;  /* base.outline.n_contours */
    this.flags      = 0;  /* base.outline.flags */
    this.subglyphs  = []; /* base.subglyphs */
    this.num_subglyphs = 0;
  }

  /* FT_GlyphLoader_Add: commit `current' (cx/cy/ct/cc, contours relative) */
  add( cx, cy, ct, cc ) {
    const nb = this.n_points;
    for ( let n = 0; n < cc.length; n++ )
      this.C.push( cc[n] + nb );        /* current->contours[n] += base->n_points */
    for ( let n = 0; n < cx.length; n++ ) {
      this.X.push( cx[n] );
      this.Y.push( cy[n] );
      this.T.push( ct[n] );
    }
    this.n_points   += cx.length;
    this.n_contours += cc.length;
  }
}

/* =======================================================================
 *  TT_Load_Glyph_Header
 * ===================================================================== */
function readGlyphHeader( d, p, limit, loader ) {
  if ( p + 10 > limit )
    THROW( Err_Invalid_Outline );

  loader.n_contours = i16( d, p );
  loader.bbox = {
    xMin: i16( d, p + 2 ),
    yMin: i16( d, p + 4 ),
    xMax: i16( d, p + 6 ),
    yMax: i16( d, p + 8 ),
  };
  return p + 10;
}

/* =======================================================================
 *  TT_Load_Simple_Glyph
 * ===================================================================== */
function loadSimpleGlyph( d, p, limit, n_contours ) {
  /* reading the contours' endpoints & number of points */
  if ( n_contours >= 0xFFF || p + 2 * n_contours + 2 > limit )
    THROW( Err_Invalid_Outline );

  const cont = new Array( n_contours );
  let last = -1;
  for ( let i = 0; i < n_contours; i++ ) {
    const c = u16( d, p ); p += 2;
    if ( c <= last )
      THROW( Err_Invalid_Outline );
    cont[i] = c;
    last = c;
  }

  const n_points = last + 1;

  /* space checked above */
  const n_ins = u16( d, p ); p += 2;

  if ( p + n_ins > limit )
    THROW( Err_Too_Many_Hints );

  /* the bytecode itself is only kept when IS_HINTED (W4's job) */
  p += n_ins;

  /* reading the point tags */
  const tags = new Uint8Array( n_points );
  {
    let f = 0;
    while ( f < n_points ) {
      if ( p + 1 > limit ) THROW( Err_Invalid_Outline );
      const c = d[p++];
      tags[f++] = c;
      if ( c & REPEAT_FLAG ) {
        if ( p + 1 > limit ) THROW( Err_Invalid_Outline );
        let count = d[p++];
        if ( f + count > n_points ) THROW( Err_Invalid_Outline );
        for ( ; count > 0; count-- )
          tags[f++] = c;
      }
    }
  }

  /* retain the overlap flag (set on gloader->base.outline.flags) */
  const overlap = ( n_points && ( tags[0] & OVERLAP_SIMPLE ) ) ? FT_OUTLINE_OVERLAP : 0;

  /* reading the X coordinates */
  const X = new Array( n_points );
  const Y = new Array( n_points );
  {
    let x = 0;
    for ( let i = 0; i < n_points; i++ ) {
      let delta = 0;
      const f = tags[i];
      if ( f & X_SHORT_VECTOR ) {
        if ( p + 1 > limit ) THROW( Err_Invalid_Outline );
        delta = d[p++];
        if ( !( f & X_POSITIVE ) ) delta = -delta;
      } else if ( !( f & X_POSITIVE /* SAME_X */ ) ) {
        if ( p + 2 > limit ) THROW( Err_Invalid_Outline );
        delta = i16( d, p ); p += 2;
      }
      x += delta;
      X[i] = x;
    }
  }

  /* reading the Y coordinates */
  {
    let y = 0;
    for ( let i = 0; i < n_points; i++ ) {
      let delta = 0;
      const f = tags[i];
      if ( f & Y_SHORT_VECTOR ) {
        if ( p + 1 > limit ) THROW( Err_Invalid_Outline );
        delta = d[p++];
        if ( !( f & Y_POSITIVE ) ) delta = -delta;
      } else if ( !( f & Y_POSITIVE /* SAME_Y */ ) ) {
        if ( p + 2 > limit ) THROW( Err_Invalid_Outline );
        delta = i16( d, p ); p += 2;
      }
      y += delta;
      Y[i] = y;
      /* C: *flag = (FT_Byte)( f & ON_CURVE_POINT ) -- tags become 0 or 1 */
      tags[i] = f & ON_CURVE_POINT;
    }
  }

  return { X, Y, tags, contours: cont, n_points, n_contours, overlap };
}

/* =======================================================================
 *  TT_Load_Composite_Glyph
 * ===================================================================== */
function loadCompositeGlyph( d, p, limit, num_glyphs ) {
  const subglyphs = [];
  let flags;

  do {
    if ( p + 4 > limit ) THROW( Err_Invalid_Composite );

    const sg = { arg1: 0, arg2: 0, flags: 0, index: 0,
                 transform: { xx: 0, xy: 0, yx: 0, yy: 0 } };

    sg.flags = u16( d, p ); p += 2;
    sg.index = u16( d, p ); p += 2;
    flags = sg.flags;

    /* we reject composites with invalid component glyph indices */
    if ( sg.index >= num_glyphs ) THROW( Err_Invalid_Composite );

    /* check space */
    let count = 2;
    if ( flags & ARGS_ARE_WORDS )          count += 2;
    if ( flags & WE_HAVE_A_SCALE )         count += 2;
    else if ( flags & WE_HAVE_AN_XY_SCALE ) count += 4;
    else if ( flags & WE_HAVE_A_2X2 )      count += 8;

    if ( p + count > limit ) THROW( Err_Invalid_Composite );

    /* read arguments */
    if ( flags & ARGS_ARE_XY_VALUES ) {
      if ( flags & ARGS_ARE_WORDS ) {
        sg.arg1 = i16( d, p ); p += 2;
        sg.arg2 = i16( d, p ); p += 2;
      } else {
        sg.arg1 = i8( d, p ); p += 1;   /* FT_NEXT_CHAR: signed */
        sg.arg2 = i8( d, p ); p += 1;
      }
    } else {
      if ( flags & ARGS_ARE_WORDS ) {
        sg.arg1 = u16( d, p ); p += 2;  /* (FT_Int)FT_NEXT_USHORT */
        sg.arg2 = u16( d, p ); p += 2;
      } else {
        sg.arg1 = u8( d, p ); p += 1;   /* (FT_Int)FT_NEXT_BYTE */
        sg.arg2 = u8( d, p ); p += 1;
      }
    }

    /* read transform: F2Dot14 * 4 == 16.16 */
    let xx = 0x10000, yy = 0x10000, xy = 0, yx = 0;

    if ( flags & WE_HAVE_A_SCALE ) {
      xx = i16( d, p ) * 4; p += 2;
      yy = xx;
    } else if ( flags & WE_HAVE_AN_XY_SCALE ) {
      xx = i16( d, p ) * 4; p += 2;
      yy = i16( d, p ) * 4; p += 2;
    } else if ( flags & WE_HAVE_A_2X2 ) {
      xx = i16( d, p ) * 4; p += 2;
      yx = i16( d, p ) * 4; p += 2;
      xy = i16( d, p ) * 4; p += 2;
      yy = i16( d, p ) * 4; p += 2;
    }

    sg.transform = { xx, xy, yx, yy };
    subglyphs.push( sg );
  } while ( flags & MORE_COMPONENTS );

  return { subglyphs, cursor: p };
}

/* =======================================================================
 *  tt_get_metrics  (TT_Get_HMetrics + TT_Get_VMetrics)
 * ===================================================================== */
function ttGetMetrics( face, loader, gid ) {
  const h = face.hmtx( gid );
  const left_bearing  = h.lsb;
  const advance_width = h.advance;

  let top_bearing = 0, advance_height = 0;
  if ( face.verticalInfo ) {
    const v = face.vmtx( gid );
    top_bearing    = v.lsb;
    advance_height = v.advance;
  } else if ( face.os2 && face.os2.version !== 0xFFFF ) {
    /* C: (FT_Short)( sTypoAscender - yMax ) -- truncate to signed 16 bits */
    top_bearing    = toS16( face.os2.sTypoAscender - loader.bbox.yMax );
    advance_height = toU16( Math.abs( face.os2.sTypoAscender -
                                      face.os2.sTypoDescender ) );
  } else {
    top_bearing    = toS16( face.hhea.Ascender - loader.bbox.yMax );
    advance_height = toU16( Math.abs( face.hhea.Ascender - face.hhea.Descender ) );
  }

  loader.left_bearing = left_bearing;
  loader.advance      = advance_width;
  loader.top_bearing  = top_bearing;
  loader.vadvance     = advance_height;

  if ( !loader.linear_def ) {
    loader.linear_def = 1;
    loader.linear     = advance_width;
  }
}

/* C casts that matter: (FT_Short)x and (FT_UShort)x wrap. */
const toS16 = ( x ) => ( ( x & 0xFFFF ) << 16 ) >> 16;
const toU16 = ( x ) => x & 0xFFFF;

/* =======================================================================
 *  tt_loader_set_pp
 *  (the TT_SUPPORT_SUBPIXEL_HINTING_MINIMAL branch that would set
 *   pp3.x/pp4.x needs loader->exec, which only exists when hinting;
 *   with FT_LOAD_NO_HINTING exec is NULL, so formula (3), x = 0, applies.)
 * ===================================================================== */
function ttLoaderSetPP( loader ) {
  loader.pp1 = { x: loader.bbox.xMin - loader.left_bearing, y: 0 };
  loader.pp2 = { x: loader.pp1.x + loader.advance,          y: 0 };
  loader.pp3 = { x: 0, y: loader.bbox.yMax + loader.top_bearing };
  loader.pp4 = { x: 0, y: loader.pp3.y - loader.vadvance };
}

/* =======================================================================
 *  TT_Process_Simple_Glyph  (unhinted)
 * ===================================================================== */
function processSimpleGlyph( loader, g, x_scale, y_scale, do_scale, isHinted ) {
  const n = g.n_points;

  /* set phantom points -- they live past n_points in the `current' window */
  const X = g.X, Y = g.Y;
  X[n    ] = loader.pp1.x;  Y[n    ] = loader.pp1.y;
  X[n + 1] = loader.pp2.x;  Y[n + 1] = loader.pp2.y;
  X[n + 2] = loader.pp3.x;  Y[n + 2] = loader.pp3.y;
  X[n + 3] = loader.pp4.x;  Y[n + 3] = loader.pp4.y;

  const n_points = n + 4;

  if ( do_scale ) {
    for ( let i = 0; i < n_points; i++ ) {
      X[i] = FT_MulFix( X[i], x_scale );
      Y[i] = FT_MulFix( Y[i], y_scale );
    }
  }

  /* no HVAR/VVAR here, so the `else' branches apply */
  loader.pp1 = { x: X[n_points - 4], y: Y[n_points - 4] };
  loader.pp2 = { x: X[n_points - 3], y: Y[n_points - 3] };
  loader.pp3 = { x: X[n_points - 2], y: Y[n_points - 2] };
  loader.pp4 = { x: X[n_points - 1], y: Y[n_points - 1] };

  /* IS_HINTED -> TT_Hint_Glyph.  The bytecode interpreter itself is W4's
   * ttinterp.js/tthint.js, but TT_Hint_Glyph ALSO rounds the four phantom
   * points to the pixel grid, and it does so BEFORE the `if ( n_ins > 0 )'
   * block -- so for a glyph carrying no instructions this rounding is the
   * only thing hinting does, and reproducing it here makes every
   * uninstructed glyph byte-exact under `-f 8000' too. */
  if ( isHinted ) {
    X[n_points - 4] = FT_PIX_ROUND( X[n_points - 4] );
    X[n_points - 3] = FT_PIX_ROUND( X[n_points - 3] );
    Y[n_points - 2] = FT_PIX_ROUND( Y[n_points - 2] );
    Y[n_points - 1] = FT_PIX_ROUND( Y[n_points - 1] );

    loader.pp1 = { x: X[n_points - 4], y: Y[n_points - 4] };
    loader.pp2 = { x: X[n_points - 3], y: Y[n_points - 3] };
    loader.pp3 = { x: X[n_points - 2], y: Y[n_points - 2] };
    loader.pp4 = { x: X[n_points - 1], y: Y[n_points - 1] };
  }
}

/* =======================================================================
 *  TT_Process_Composite_Component
 * ===================================================================== */
function processCompositeComponent( loader, gl, sg, start_point, num_base_points,
                                    x_scale, y_scale, do_scale, isHinted ) {
  /* `current' = the points the component just contributed */
  const first = num_base_points;
  const n_cur = gl.n_points - num_base_points;

  const have_scale = !!( sg.flags & ( WE_HAVE_A_SCALE |
                                      WE_HAVE_AN_XY_SCALE |
                                      WE_HAVE_A_2X2 ) );

  /* perform the transform required for this subglyph */
  if ( have_scale )
    FT_Outline_Transform( gl.X, gl.Y, first, n_cur, sg.transform );

  let x, y;

  if ( !( sg.flags & ARGS_ARE_XY_VALUES ) ) {
    /* match l-th point of the new component to the k-th point of the
     * previously loaded components */
    const num_points = gl.n_points;
    let k = sg.arg1 >>> 0;    /* C: (FT_UInt)subglyph->arg1 */
    let l = sg.arg2 >>> 0;

    k = ( k + start_point ) >>> 0;
    l = ( l + num_base_points ) >>> 0;

    if ( k >= num_base_points || l >= num_points )
      THROW( Err_Invalid_Composite );

    x = gl.X[k] - gl.X[l];
    y = gl.Y[k] - gl.Y[l];
  } else {
    x = sg.arg1;
    y = sg.arg2;

    if ( !x && !y )
      return;

    /* TT_CONFIG_OPTION_COMPONENT_OFFSET_SCALED is undefined, so: */
    if ( have_scale && ( sg.flags & SCALED_COMPONENT_OFFSET ) ) {
      const mac_xscale = FT_Hypot( sg.transform.xx, sg.transform.xy );
      const mac_yscale = FT_Hypot( sg.transform.yy, sg.transform.yx );

      x = FT_MulFix( x, mac_xscale );
      y = FT_MulFix( y, mac_yscale );
    }

    if ( do_scale ) {
      x = FT_MulFix( x, x_scale );
      y = FT_MulFix( y, y_scale );

      /* C: if ( subglyph->flags & ROUND_XY_TO_GRID && IS_HINTED( load_flags ) )
       * -- so on the UNHINTED path this rounding must NOT happen. */
      if ( ( sg.flags & ROUND_XY_TO_GRID ) && isHinted ) {
        x = FT_PIX_ROUND( x );
        y = FT_PIX_ROUND( y );
      }
    }
  }

  if ( x || y )
    FT_Outline_Translate( gl.X, gl.Y, first, n_cur, x, y );
}

/* =======================================================================
 *  load_truetype_glyph
 * ===================================================================== */
function loadTrueTypeGlyph( face, gl, loader, gid, recurse_count,
                            x_scale, y_scale, do_scale, isHinted ) {
  /* arbitrary recursion limit */
  if ( recurse_count > 100 )
    THROW( Err_Invalid_Composite );

  if ( gid >= face.numGlyphs )
    THROW( 0x10 /* Invalid_Glyph_Index */ );

  loader.glyph_index = gid;

  const d = face.data;
  const loc = face.loca( gid );
  const offset   = loc.offset;
  let   byte_len = loc.length;

  loader.byte_len = byte_len;

  if ( byte_len > 0 ) {
    if ( !face.glyfOffset )
      THROW( 0x08 /* Invalid_Table */ );

    const start = face.glyfOffset + offset;
    readGlyphHeader( d, start, start + byte_len, loader );
  }

  /* a space glyph */
  if ( byte_len === 0 || loader.n_contours === 0 )
    loader.bbox = { xMin: 0, xMax: 0, yMin: 0, yMax: 0 };

  /* metrics must be computed after the header (yMax feeds TT_Get_VMetrics) */
  ttGetMetrics( face, loader, gid );

  ttLoaderSetPP( loader );

  /* shortcut for empty glyphs */
  if ( byte_len === 0 || loader.n_contours === 0 ) {
    if ( do_scale ) {
      loader.pp1.x = FT_MulFix( loader.pp1.x, x_scale );
      loader.pp2.x = FT_MulFix( loader.pp2.x, x_scale );
      /* pp1.y and pp2.y are always zero */
      loader.pp3.x = FT_MulFix( loader.pp3.x, x_scale );
      loader.pp3.y = FT_MulFix( loader.pp3.y, y_scale );
      loader.pp4.x = FT_MulFix( loader.pp4.x, x_scale );
      loader.pp4.y = FT_MulFix( loader.pp4.y, y_scale );
    }
    return;
  }

  const bodyStart = face.glyfOffset + offset + 10;
  const bodyLimit = face.glyfOffset + offset + byte_len;

  if ( loader.n_contours > 0 ) {
    /* ---- simple glyph ---- */
    const g = loadSimpleGlyph( d, bodyStart, bodyLimit, loader.n_contours );

    gl.flags |= g.overlap;

    processSimpleGlyph( loader, g, x_scale, y_scale, do_scale, isHinted );

    /* FT_GlyphLoader_Add -- phantom points are NOT committed */
    gl.add( g.X.slice( 0, g.n_points ), g.Y.slice( 0, g.n_points ),
            g.tags, g.contours );
  } else {
    /* ---- composite glyph ---- */
    loader.n_contours = -1;

    /* infinite recursion detection (FT_List `composites') */
    loader.composites.length = recurse_count;
    if ( loader.composites.indexOf( gid ) >= 0 )
      THROW( Err_Invalid_Composite );
    loader.composites[recurse_count] = gid;

    const start_point   = gl.n_points;
    const start_contour = gl.n_contours;

    const comp = loadCompositeGlyph( d, bodyStart, bodyLimit, face.numGlyphs );
    const subglyphs = comp.subglyphs;

    if ( do_scale ) {
      loader.pp1.x = FT_MulFix( loader.pp1.x, x_scale );
      loader.pp2.x = FT_MulFix( loader.pp2.x, x_scale );
      loader.pp3.x = FT_MulFix( loader.pp3.x, x_scale );
      loader.pp3.y = FT_MulFix( loader.pp3.y, y_scale );
      loader.pp4.x = FT_MulFix( loader.pp4.x, x_scale );
      loader.pp4.y = FT_MulFix( loader.pp4.y, y_scale );
    }

    const num_base_subgs = gl.num_subglyphs;
    for ( const sg of subglyphs ) gl.subglyphs.push( sg );
    gl.num_subglyphs += subglyphs.length;

    /* FT_GlyphLoader_Add for the composite record itself (no points) */

    for ( let n = 0; n < subglyphs.length; n++ ) {
      const sg = gl.subglyphs[num_base_subgs + n];

      const pp = [ loader.pp1, loader.pp2, loader.pp3, loader.pp4 ];
      const linear_hadvance = loader.linear;
      const linear_vadvance = loader.vadvance;

      const num_base_points = gl.n_points;

      loadTrueTypeGlyph( face, gl, loader, sg.index, recurse_count + 1,
                         x_scale, y_scale, do_scale, isHinted );

      /* restore phantom points if necessary */
      if ( !( sg.flags & USE_MY_METRICS ) ) {
        loader.pp1 = pp[0];
        loader.pp2 = pp[1];
        loader.pp3 = pp[2];
        loader.pp4 = pp[3];
        loader.linear   = linear_hadvance;
        loader.vadvance = linear_vadvance;
      }

      const num_points = gl.n_points;
      if ( num_points === num_base_points )
        continue;

      processCompositeComponent( loader, gl, sg, start_point, num_base_points,
                                 x_scale, y_scale, do_scale, isHinted );
    }

    /* IS_HINTED -> TT_Process_Composite_Glyph: not reached when unhinted */
    void start_contour;

    /* retain the overlap flag */
    if ( gl.num_subglyphs && ( gl.subglyphs[0].flags & OVERLAP_COMPOUND ) )
      gl.flags |= FT_OUTLINE_OVERLAP;
  }
}

/* =======================================================================
 *  compute_glyph_metrics
 * ===================================================================== */
function computeGlyphMetrics( face, loader, outline, y_scale, do_scale ) {
  const bbox = FT_Outline_Get_CBox( outline );

  const m = {
    horiBearingX: bbox.xMin,
    horiBearingY: bbox.yMax,
    horiAdvance : loader.pp2.x - loader.pp1.x,
    width       : bbox.xMax - bbox.xMin,
    height      : bbox.yMax - bbox.yMin,
    vertBearingX: 0, vertBearingY: 0, vertAdvance: 0,
  };

  let top, advance;

  if ( face.verticalInfo && face.vhea && face.vhea.number_Of_HMetrics > 0 ) {
    top = toS16( FT_DivFix( loader.pp3.y - bbox.yMax, y_scale ) );
    if ( loader.pp3.y <= loader.pp4.y )
      advance = 0;
    else
      advance = toU16( FT_DivFix( loader.pp3.y - loader.pp4.y, y_scale ) );
  } else {
    const height = toS16( FT_DivFix( bbox.yMax - bbox.yMin, y_scale ) );
    if ( face.os2 && face.os2.version !== 0xFFFF )
      advance = face.os2.sTypoAscender - face.os2.sTypoDescender;
    else
      advance = face.hhea.Ascender - face.hhea.Descender;

    /* C integer division truncates toward zero */
    top = Math.trunc( ( advance - height ) / 2 );
  }

  const linearVertAdvance = advance;

  if ( do_scale ) {
    top     = FT_MulFix( top,     y_scale );
    advance = FT_MulFix( advance, y_scale );
  }

  m.vertBearingX = m.horiBearingX - Math.trunc( m.horiAdvance / 2 );
  m.vertBearingY = top;
  m.vertAdvance  = advance;

  return { metrics: m, linearVertAdvance };
}

/* =======================================================================
 *  TT_Load_Glyph + the outline-specific tail of FT_Load_Glyph
 * ===================================================================== */

/**
 * Load glyph `gid' of `face' at the given 16.16 scales, exactly as
 * FT_Load_Glyph( face, gid, FT_LOAD_NO_HINTING ) does.  Returns the slot's
 * 26.6 outline plus the metrics ftlint needs.
 *
 * @param y_ppem  size->metrics->y_ppem (drives FT_OUTLINE_HIGH_PRECISION)
 */
export function loadGlyfOutlineScaled( face, gid, x_scale, y_scale, y_ppem = 0,
                                       isHinted = false ) {
  const do_scale = true;
  return loadCommon( face, gid, x_scale, y_scale, do_scale, y_ppem, isHinted );
}

/**
 * Contract entry point: outline in FONT UNITS, i.e. the FT_LOAD_NO_SCALE
 * path (x_scale = y_scale = 0x10000 and `do_scale' false).
 */
export function loadGlyfOutline( face, gid ) {
  const r = loadCommon( face, gid, 0x10000, 0x10000, false, 0, false );
  return { outline: r.outline, advance: r.advance, lsb: r.lsb };
}

function loadCommon( face, gid, x_scale, y_scale, do_scale, y_ppem, isHinted ) {
  const gl = new GlyphLoader();

  const loader = {
    n_contours: 0,
    bbox: { xMin: 0, yMin: 0, xMax: 0, yMax: 0 },
    left_bearing: 0, advance: 0, top_bearing: 0, vadvance: 0,
    linear: 0, linear_def: 0,
    pp1: { x: 0, y: 0 }, pp2: { x: 0, y: 0 },
    pp3: { x: 0, y: 0 }, pp4: { x: 0, y: 0 },
    composites: [],
    byte_len: 0,
  };

  loadTrueTypeGlyph( face, gl, loader, gid, 0, x_scale, y_scale, do_scale, isHinted );

  /* glyph->format = FT_GLYPH_FORMAT_OUTLINE (we never set NO_RECURSE) */
  let flags = gl.flags & ~FT_OUTLINE_SINGLE_PASS;

  /* "Set the `high precision' bit flag." */
  if ( do_scale && y_ppem < 24 )
    flags |= FT_OUTLINE_HIGH_PRECISION;

  const n = gl.n_points;
  const outline = {
    n_points  : n,
    n_contours: gl.n_contours,
    x   : Int32Array.from( gl.X ),
    y   : Int32Array.from( gl.Y ),
    tags: Uint8Array.from( gl.T ),
    contours: Int32Array.from( gl.C ),
    flags,
  };

  /* "Translate array so that (0,0) is the glyph's origin." */
  if ( loader.pp1.x )
    FT_Outline_Translate( outline.x, outline.y, 0, n, -loader.pp1.x, 0 );

  const cm = computeGlyphMetrics( face, loader, outline, y_scale, do_scale );

  return {
    outline,
    advance : loader.advance,
    lsb     : loader.left_bearing,
    metrics : cm.metrics,
    linearHoriAdvance: loader.linear,
    linearVertAdvance: cm.linearVertAdvance,
  };
}

export { FTError };
