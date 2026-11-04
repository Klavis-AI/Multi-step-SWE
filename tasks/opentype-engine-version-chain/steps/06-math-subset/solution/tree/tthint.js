/* tthint.js -- the glue that ../freetype/src/truetype/ttgload.c and ttobjs.c
 * do around the bytecode interpreter.  Port of:
 *
 *   ttobjs.c  tt_size_init_bytecode, tt_size_run_fpgm, tt_size_run_prep,
 *             tt_size_reset (the ttmetrics part)
 *   ttgload.c tt_loader_init, load_truetype_glyph, TT_Load_Simple_Glyph,
 *             TT_Load_Composite_Glyph, TT_Process_Simple_Glyph,
 *             TT_Process_Composite_Component, TT_Process_Composite_Glyph,
 *             TT_Hint_Glyph, tt_prepare_zone, tt_loader_set_pp,
 *             tt_get_metrics, compute_glyph_metrics, TT_Load_Glyph
 *   ftgloadr.c FT_GlyphLoader_Add / _Prepare / _Rewind
 *
 * TARGET: TT_INTERPRETER_VERSION_35.  The v40-only paths (pp3.x/pp4.x set to
 * advance/2, the backward_compatibility early-return in TT_Hint_Glyph, the
 * ROUND_XY_TO_GRID x-axis curfew) are DELETED, not carried as dead code.
 */

import {
  ERR, newExecContext, newGlyphZone, tt_default_graphics_state,
  TT_Load_Context, TT_Save_Context, TT_Run_Context,
  TT_Set_CodeRange, TT_Clear_CodeRange,
  tt_coderange_font, tt_coderange_cvt, tt_coderange_glyph,
  FT_MulFix, FT_DivFix, FT_PIX_ROUND, FT_Hypot,
  FT_CURVE_TAG_ON, FT_CURVE_TAG_HAS_SCANMODE, FT_CURVE_TAG_TOUCH_BOTH,
} from './ttinterp.js';


/* ttgload.c simple-glyph flags */
const ON_CURVE_POINT = 0x01;
const X_SHORT_VECTOR = 0x02;
const Y_SHORT_VECTOR = 0x04;
const REPEAT_FLAG    = 0x08;
const X_POSITIVE     = 0x10;   /* == SAME_X  */
const Y_POSITIVE     = 0x20;   /* == SAME_Y  */
const OVERLAP_SIMPLE = 0x40;

/* ttgload.c composite-glyph flags */
const ARGS_ARE_WORDS            = 0x0001;
const ARGS_ARE_XY_VALUES        = 0x0002;
const ROUND_XY_TO_GRID          = 0x0004;
const WE_HAVE_A_SCALE           = 0x0008;
const MORE_COMPONENTS           = 0x0020;
const WE_HAVE_AN_XY_SCALE       = 0x0040;
const WE_HAVE_A_2X2             = 0x0080;
const WE_HAVE_INSTR             = 0x0100;
const USE_MY_METRICS            = 0x0200;
const OVERLAP_COMPOUND          = 0x0400;
const SCALED_COMPONENT_OFFSET   = 0x0800;
const UNSCALED_COMPONENT_OFFSET = 0x1000;

/* ftimage.h FT_OUTLINE_* */
const FT_OUTLINE_IGNORE_DROPOUTS = 0x8;
const FT_OUTLINE_SMART_DROPOUTS  = 0x10;
const FT_OUTLINE_INCLUDE_STUBS   = 0x20;
const FT_OUTLINE_OVERLAP         = 0x40;
const FT_OUTLINE_HIGH_PRECISION  = 0x100;


/* ------------------------------------------------------------------ */
/*  table access -- works against a minimal W2 face (data + tables)     */
/* ------------------------------------------------------------------ */

function tbl( face, tag ) {
  const t = face.tables;
  if ( !t ) return null;
  const e = ( typeof t.get === 'function' ) ? t.get( tag ) : t[tag];
  return e || null;
}

function dv( face ) {
  if ( !face._w4dv )
    face._w4dv = new DataView( face.data.buffer, face.data.byteOffset, face.data.byteLength );
  return face._w4dv;
}

const u8  = ( f, o ) => dv( f ).getUint8( o );
const u16 = ( f, o ) => dv( f ).getUint16( o );
const i16 = ( f, o ) => dv( f ).getInt16( o );
const u32 = ( f, o ) => dv( f ).getUint32( o );


/* ------------------------------------------------------------------ */
/*  face-level derived data (cached on the face, additively)            */
/* ------------------------------------------------------------------ */


/* sfnt.js hands `face.cvt' / `face.fpgm' / `face.prep' as TABLE RECORDS
 * ({offset,length}), not as data.  Accept a record, a typed array, or
 * nothing at all, so that either flavour of face object works. */
function asProgram( face, v, tag ) {
  if ( ArrayBuffer.isView( v ) ) return v;
  const rec = ( v && typeof v.offset === 'number' ) ? v : tbl( face, tag );
  if ( !rec || !rec.length ) return new Uint8Array( 0 );
  return face.data.subarray( rec.offset, rec.offset + rec.length );
}

/* ttpload.c tt_face_load_cvt stores cvt[i] = (FT_Short)value * 64 */
function asCvt( face, v ) {
  if ( ArrayBuffer.isView( v ) && !( v instanceof Uint8Array ) ) return v;
  const rec = ( v && typeof v.offset === 'number' ) ? v : tbl( face, 'cvt ' );
  if ( !rec || !rec.length ) return new Int32Array( 0 );
  const n = rec.length >> 1;
  const a = new Int32Array( n );
  for ( let i = 0; i < n; i++ ) a[i] = i16( face, rec.offset + 2 * i ) * 64;
  return a;
}

export function faceInfo( face ) {
  if ( face._w4 ) return face._w4;

  const head = tbl( face, 'head' );
  const maxp = tbl( face, 'maxp' );
  const hhea = tbl( face, 'hhea' );
  const os2  = tbl( face, 'OS/2' );
  const vhea = tbl( face, 'vhea' );
  const vmtx = tbl( face, 'vmtx' );
  const hdmx = tbl( face, 'hdmx' );
  const cvtT = tbl( face, 'cvt ' );
  const fpgmT = tbl( face, 'fpgm' );
  const prepT = tbl( face, 'prep' );
  const post = tbl( face, 'post' );

  const info = {
    unitsPerEM: face.unitsPerEM ?? ( head ? u16( face, head.offset + 18 ) : 1000 ),
    headFlags:  head ? u16( face, head.offset + 16 ) : 0,
    indexToLocFormat: face.indexToLocFormat ?? ( head ? i16( face, head.offset + 50 ) : 0 ),
    numGlyphs:  face.numGlyphs ?? ( maxp ? u16( face, maxp.offset + 4 ) : 0 ),

    maxPoints:            maxp ? u16( face, maxp.offset + 6 ) : 0,
    maxContours:          maxp ? u16( face, maxp.offset + 8 ) : 0,
    maxCompositePoints:   maxp ? u16( face, maxp.offset + 10 ) : 0,
    maxCompositeContours: maxp ? u16( face, maxp.offset + 12 ) : 0,
    maxTwilightPoints:    maxp ? u16( face, maxp.offset + 16 ) : 0,
    maxStorage:           maxp ? u16( face, maxp.offset + 18 ) : 0,
    maxFunctionDefs:      maxp ? u16( face, maxp.offset + 20 ) : 0,
    maxInstructionDefs:   maxp ? u16( face, maxp.offset + 22 ) : 0,
    maxStackElements:     maxp ? u16( face, maxp.offset + 24 ) : 0,
    maxComponentDepth:    maxp ? u16( face, maxp.offset + 30 ) : 0,

    numberOfHMetrics: hhea ? u16( face, hhea.offset + 34 ) : 0,
    hheaAscender:     hhea ? i16( face, hhea.offset + 4 ) : 0,
    hheaDescender:    hhea ? i16( face, hhea.offset + 6 ) : 0,

    os2Version:     os2 ? u16( face, os2.offset ) : 0xFFFF,
    sTypoAscender:  os2 ? i16( face, os2.offset + 68 ) : 0,
    sTypoDescender: os2 ? i16( face, os2.offset + 70 ) : 0,

    vertical_info: !!( vhea && vmtx ),
    numberOfVMetrics: vhea ? u16( face, vhea.offset + 34 ) : 0,
    vmtx,

    /* post.isFixedPitch is at offset 12 of the post table */
    isFixedPitch: post ? u32( face, post.offset + 12 ) !== 0 : false,

    glyf: tbl( face, 'glyf' ),
    loca: tbl( face, 'loca' ),
    hmtxT: tbl( face, 'hmtx' ),
    hdmx,
  };

  info.cvt  = asCvt( face, face.cvt );
  info.fpgm = asProgram( face, face.fpgm, 'fpgm' );
  info.prep = asProgram( face, face.prep, 'prep' );

  /* hdmx header: version uint16, numRecords int16, sizeDeviceRecord uint32 */
  info.hdmxRecords = [];
  if ( hdmx && hdmx.length >= 8 ) {
    const nRec = i16( face, hdmx.offset + 2 );
    const sz   = u32( face, hdmx.offset + 4 );
    if ( nRec > 0 && sz > 0 && 8 + nRec * sz <= hdmx.length )
      for ( let i = 0; i < nRec; i++ ) info.hdmxRecords.push( hdmx.offset + 8 + i * sz );
  }

  face._w4 = info;
  return info;
}

/* ttpload.c tt_face_get_device_metrics: binary search over sorted records.
 * Returns the file offset of the per-glyph width array, or -1. */
function getDeviceMetrics( face, info, ppem ) {
  let min = 0, max = info.hdmxRecords.length;
  while ( min < max ) {
    const mid = ( min + max ) >> 1;
    const p = u8( face, info.hdmxRecords[mid] );
    if ( p > ppem ) max = mid;
    else if ( p < ppem ) min = mid + 1;
    else return info.hdmxRecords[mid] + 2;   /* skip ppem + maxWidth */
  }
  return -1;
}

function glyphRange( face, info, gid ) {
  if ( typeof face.loca === 'function' ) return face.loca( gid );
  const lo = info.loca;
  if ( !lo ) return { offset: 0, length: 0 };
  let a, b;
  if ( info.indexToLocFormat === 0 ) {
    if ( 2 * gid + 4 > lo.length ) return { offset: 0, length: 0 };
    a = u16( face, lo.offset + 2 * gid ) * 2;
    b = u16( face, lo.offset + 2 * gid + 2 ) * 2;
  }
  else {
    if ( 4 * gid + 8 > lo.length ) return { offset: 0, length: 0 };
    a = u32( face, lo.offset + 4 * gid );
    b = u32( face, lo.offset + 4 * gid + 4 );
  }
  return { offset: a, length: b > a ? b - a : 0 };
}

function getHMetrics( face, info, gid ) {
  if ( typeof face.hmtx === 'function' ) return face.hmtx( gid );
  const h = info.hmtxT;
  if ( !h || !info.numberOfHMetrics ) return { advance: 0, lsb: 0 };
  const n = info.numberOfHMetrics;
  if ( gid < n )
    return { advance: u16( face, h.offset + 4 * gid ), lsb: i16( face, h.offset + 4 * gid + 2 ) };
  const adv = u16( face, h.offset + 4 * ( n - 1 ) );
  const off = h.offset + 4 * n + 2 * ( gid - n );
  const lsb = ( off + 2 <= h.offset + h.length ) ? i16( face, off ) : 0;
  return { advance: adv, lsb };
}

/* ttgload.c TT_Get_VMetrics */
function getVMetrics( face, info, gid, yMax ) {
  if ( info.vertical_info && info.numberOfVMetrics ) {
    const v = info.vmtx, n = info.numberOfVMetrics;
    let ah, tsb;
    if ( gid < n ) {
      ah  = u16( face, v.offset + 4 * gid );
      tsb = i16( face, v.offset + 4 * gid + 2 );
    }
    else {
      ah = u16( face, v.offset + 4 * ( n - 1 ) );
      const off = v.offset + 4 * n + 2 * ( gid - n );
      tsb = ( off + 2 <= v.offset + v.length ) ? i16( face, off ) : 0;
    }
    return { tsb, ah };
  }
  if ( info.os2Version !== 0xFFFF )
    return { tsb: info.sTypoAscender - yMax,
             ah: Math.abs( info.sTypoAscender - info.sTypoDescender ) };
  return { tsb: info.hheaAscender - yMax,
           ah: Math.abs( info.hheaAscender - info.hheaDescender ) };
}


/* ------------------------------------------------------------------ */
/*  size object: ttobjs.c tt_size_reset / init_bytecode / fpgm / prep    */
/* ------------------------------------------------------------------ */

function emptyZone() {
  const z = newGlyphZone( 0, 0 );
  z.n_points = 0; z.n_contours = 0;
  return z;
}

function sizeKey( m ) { return m.x_ppem + '/' + m.y_ppem + '/' + m.x_scale + '/' + m.y_scale; }

export function getSize( face, sm ) {
  const info = faceInfo( face );
  const key = sizeKey( sm );
  if ( !face._w4sizes ) face._w4sizes = new Map();
  const hit = face._w4sizes.get( key );
  if ( hit ) return hit;

  /* --- tt_size_reset: derive ttmetrics from the size metrics --- */
  const ttm = { x_ratio: 0, y_ratio: 0, ratio: 0, scale: 0, ppem: 0,
                rotated: false, stretched: false };
  if ( sm.x_ppem >= sm.y_ppem ) {
    ttm.scale = sm.x_scale; ttm.ppem = sm.x_ppem;
    ttm.x_ratio = 0x10000;
    ttm.y_ratio = FT_DivFix( sm.y_ppem, sm.x_ppem );
  }
  else {
    ttm.scale = sm.y_scale; ttm.ppem = sm.y_ppem;
    ttm.x_ratio = FT_DivFix( sm.x_ppem, sm.y_ppem );
    ttm.y_ratio = 0x10000;
  }

  const nTwilight = info.maxTwilightPoints + 4;   /* 4 phantom points */

  const size = {
    face, info,
    metrics: { x_ppem: sm.x_ppem, y_ppem: sm.y_ppem,
               x_scale: sm.x_scale, y_scale: sm.y_scale },
    ttmetrics: ttm,
    /* ftlint calls FT_Set_Char_Size(ptsize<<6, ..., 72, 72) */
    point_size: sm.y_ppem * 64,
    GS: tt_default_graphics_state(),
    twilight: newGlyphZone( nTwilight, 0 ),
    twilightAlloc: nTwilight,
    widthp: getDeviceMetrics( face, info, sm.x_ppem ),
    context: null,
    bytecode_ready: -1,
    cvt_ready: -1,
    error: 0,
  };

  /* --- tt_size_init_bytecode --- */
  const exc = newExecContext( {
    maxFDefs: info.maxFunctionDefs,
    maxIDefs: info.maxInstructionDefs,
    /* 50% more than declared, with a minimum safety margin of 128 */
    stackSize: info.maxStackElements +
               Math.max( Math.floor( info.maxStackElements / 2 ), 128 ),
    storeSize: info.maxStorage,
    cvtSize: info.cvt.length,
    pointSize: size.point_size,
    pedantic: false,
  } );
  exc.grayscale = true;    /* ftlint renders FT_RENDER_MODE_NORMAL */
  size.context = exc;

  if ( face.num_glyphs === undefined ) face.num_glyphs = info.numGlyphs;

  /* --- tt_size_run_fpgm --- */
  TT_Load_Context( exc, face, size );
  TT_Clear_CodeRange( exc, tt_coderange_cvt );
  TT_Clear_CodeRange( exc, tt_coderange_glyph );

  let error = 0;
  if ( info.fpgm.length > 0 ) {
    TT_Set_CodeRange( exc, tt_coderange_font, info.fpgm, info.fpgm.length );
    exc.pts = emptyZone();
    error = TT_Run_Context( exc, size );
  }
  size.bytecode_ready = error;
  if ( error ) { size.error = error; face._w4sizes.set( key, size ); return size; }
  TT_Save_Context( exc, size );

  /* --- tt_size_run_prep --- */
  error = runPrep( face, size );
  size.cvt_ready = error;
  if ( error ) size.error = error;

  face._w4sizes.set( key, size );
  return size;
}

/* ttobjs.c tt_size_run_prep */
export function runPrep( face, size ) {
  const exc = size.context, info = size.info;

  /* set default GS, twilight points and storage before the CV program can
   * modify them */
  size.GS = tt_default_graphics_state();
  size.twilight.orgX.fill( 0 ); size.twilight.orgY.fill( 0 );
  size.twilight.curX.fill( 0 ); size.twilight.curY.fill( 0 );

  TT_Load_Context( exc, face, size );

  exc.baseStorage.fill( 0 );

  /* Scale the cvt values to the new ppem; by default the y ppem is used.
   * NOTE: this is very sensitive to rounding -- the integer division by 64
   * MUST be applied to the first argument (ttobjs.c says so explicitly). */
  for ( let i = 0; i < exc.cvtSize; i++ )
    exc.baseCvt[i] = FT_MulFix( Math.trunc( info.cvt[i] / 64 ), size.ttmetrics.scale );

  TT_Clear_CodeRange( exc, tt_coderange_glyph );

  let error = 0;
  if ( info.prep.length > 0 ) {
    TT_Set_CodeRange( exc, tt_coderange_cvt, info.prep, info.prep.length );
    exc.pts = emptyZone();
    error = TT_Run_Context( exc, size );
  }

  if ( !error ) TT_Save_Context( exc, size );
  return error;
}


/* ------------------------------------------------------------------ */
/*  glyph loader (FT_GlyphLoader image)                                 */
/* ------------------------------------------------------------------ */

function newGloader() {
  return {
    cap: 0, ccap: 0,
    pointsX: new Int32Array( 0 ), pointsY: new Int32Array( 0 ),
    orgX: new Int32Array( 0 ),    orgY: new Int32Array( 0 ),
    orusX: new Int32Array( 0 ),   orusY: new Int32Array( 0 ),
    tags: new Uint8Array( 0 ),
    contours: new Int32Array( 0 ),
    n_points: 0, n_contours: 0,
    num_subglyphs: 0, subglyphs: [],
    flags: 0,
  };
}

function ensurePoints( g, n ) {
  if ( n <= g.cap ) return;
  let c = Math.max( 64, g.cap );
  while ( c < n ) c *= 2;
  const gx = ( a ) => { const b = new a.constructor( c ); b.set( a ); return b; };
  g.pointsX = gx( g.pointsX ); g.pointsY = gx( g.pointsY );
  g.orgX = gx( g.orgX );       g.orgY = gx( g.orgY );
  g.orusX = gx( g.orusX );     g.orusY = gx( g.orusY );
  g.tags = gx( g.tags );
  g.cap = c;
}

function ensureContours( g, n ) {
  if ( n <= g.ccap ) return;
  let c = Math.max( 16, g.ccap );
  while ( c < n ) c *= 2;
  const b = new Int32Array( c ); b.set( g.contours );
  g.contours = b; g.ccap = c;
}

/* ftgloadr.c FT_GlyphLoader_Add: the contour end indices of the newly added
 * outline are biased by the base point count, THEN the counts are bumped. */
function loaderAdd( g, curPoints, curContours ) {
  const base = g.n_points;
  for ( let i = 0; i < curContours; i++ ) g.contours[g.n_contours + i] += base;
  g.n_points   += curPoints;
  g.n_contours += curContours;
  g.num_subglyphs += g.cur_num_subglyphs || 0;
  g.cur_num_subglyphs = 0;
}


/* ttgload.c tt_prepare_zone.  `absPoint'/`absContour' are the absolute
 * offsets into the loader buffers; `firstPoint' is the C `first_point'
 * field, i.e. the start_point *relative to the load being prepared*. */
function prepareZone( zone, g, zoneNPoints, zoneNContours, absPoint, absContour, firstPoint ) {
  zone.n_points   = zoneNPoints;
  zone.n_contours = zoneNContours;
  zone.orgX  = g.orgX.subarray( absPoint );
  zone.orgY  = g.orgY.subarray( absPoint );
  zone.curX  = g.pointsX.subarray( absPoint );
  zone.curY  = g.pointsY.subarray( absPoint );
  zone.orusX = g.orusX.subarray( absPoint );
  zone.orusY = g.orusY.subarray( absPoint );
  zone.tags  = g.tags.subarray( absPoint );
  zone.contours = g.contours.subarray( absContour );
  zone.first_point = firstPoint;
  return zone;
}


/* ------------------------------------------------------------------ */
/*  ttgload.c TT_Hint_Glyph                                             */
/* ------------------------------------------------------------------ */

function TT_Hint_Glyph( loader, is_composite ) {
  const zone = loader.zone, exc = loader.exec, size = loader.size;
  const n_ins = exc.glyphSize;
  const n = zone.n_points;

  /* save original point positions in `org'.  This happens BEFORE the
   * phantoms are rounded below, so org/orus keep the unrounded values. */
  if ( n_ins > 0 ) {
    zone.orgX.set( zone.curX.subarray( 0, n ) );
    zone.orgY.set( zone.curY.subarray( 0, n ) );
  }

  /* XXX UNDOCUMENTED!  Hinting instructions of a composite glyph refer to
   * the (already) hinted subglyphs. */
  if ( is_composite ) {
    exc.metrics.x_scale = 1 << 16;
    exc.metrics.y_scale = 1 << 16;
    zone.orusX.set( zone.curX.subarray( 0, n ) );
    zone.orusY.set( zone.curY.subarray( 0, n ) );
  }
  else {
    exc.metrics.x_scale = size.metrics.x_scale;
    exc.metrics.y_scale = size.metrics.y_scale;
  }

  /* round phantom points */
  zone.curX[n - 4] = FT_PIX_ROUND( zone.curX[n - 4] );
  zone.curX[n - 3] = FT_PIX_ROUND( zone.curX[n - 3] );
  zone.curY[n - 2] = FT_PIX_ROUND( zone.curY[n - 2] );
  zone.curY[n - 1] = FT_PIX_ROUND( zone.curY[n - 1] );

  if ( n_ins > 0 ) {
    TT_Set_CodeRange( exc, tt_coderange_glyph, exc.glyphIns, n_ins );
    exc.is_composite = is_composite;
    exc.pts = zone;

    const error = TT_Run_Context( exc, size );
    if ( error && exc.pedantic_hinting ) return error;

    /* store drop-out mode in bits 5-7; set bit 2 also as a marker */
    loader.gloader.tags[loader.curPointBase] |=
      ( exc.GS.scan_type << 5 ) | FT_CURVE_TAG_HAS_SCANMODE;
  }

  /* v35 always saves the (possibly modified) phantom points. */
  loader.pp1 = { x: zone.curX[n - 4], y: zone.curY[n - 4] };
  loader.pp2 = { x: zone.curX[n - 3], y: zone.curY[n - 3] };
  loader.pp3 = { x: zone.curX[n - 2], y: zone.curY[n - 2] };
  loader.pp4 = { x: zone.curX[n - 1], y: zone.curY[n - 1] };

  return 0;
}


/* ------------------------------------------------------------------ */
/*  ttgload.c TT_Load_Simple_Glyph                                      */
/* ------------------------------------------------------------------ */

function TT_Load_Simple_Glyph( loader, p, limit ) {
  const face = loader.face, g = loader.gloader;
  const n_contours = loader.n_contours;
  const base = g.n_points, cbase = g.n_contours;

  ensureContours( g, cbase + n_contours );

  if ( n_contours >= 0xFFF || p + 2 * n_contours + 2 > limit )
    return ERR.Invalid_Outline;

  /* contour endpoints, strictly increasing */
  let last = -1;
  for ( let i = 0; i < n_contours; i++ ) {
    const c = u16( face, p ); p += 2;
    if ( c <= last ) return ERR.Invalid_Outline;
    last = c;
    g.contours[cbase + i] = c;
  }

  const n_points = last + 1;

  /* four phantom points are added later */
  ensurePoints( g, base + n_points + 4 );

  const n_ins = u16( face, p ); p += 2;

  if ( p + n_ins > limit ) return ERR.Too_Many_Hints;

  if ( loader.hinted ) {
    const exc = loader.exec;
    exc.glyphIns = n_ins ? face.data.slice( p, p + n_ins ) : null;
    exc.glyphSize = n_ins;
  }
  p += n_ins;

  /* point tags */
  const tags = g.tags;
  let f = 0;
  for ( let i = 0; i < n_points; ) {
    if ( p + 1 > limit ) return ERR.Invalid_Outline;
    const c = face.data[p++];
    tags[base + i++] = c;
    if ( c & REPEAT_FLAG ) {
      if ( p + 1 > limit ) return ERR.Invalid_Outline;
      let count = face.data[p++];
      if ( i + count > n_points ) return ERR.Invalid_Outline;
      for ( ; count > 0; count-- ) tags[base + i++] = c;
    }
  }

  /* retain the overlap flag (read before the tags are masked below) */
  if ( n_points && ( tags[base] & OVERLAP_SIMPLE ) ) g.flags |= FT_OUTLINE_OVERLAP;

  /* X coordinates */
  let x = 0;
  for ( let i = 0; i < n_points; i++ ) {
    let delta = 0;
    const ff = tags[base + i];
    if ( ff & X_SHORT_VECTOR ) {
      if ( p + 1 > limit ) return ERR.Invalid_Outline;
      delta = face.data[p++];
      if ( !( ff & X_POSITIVE ) ) delta = -delta;
    }
    else if ( !( ff & X_POSITIVE ) ) {   /* SAME_X shares the bit */
      if ( p + 2 > limit ) return ERR.Invalid_Outline;
      delta = i16( face, p ); p += 2;
    }
    x += delta;
    g.pointsX[base + i] = x;
  }

  /* Y coordinates */
  let y = 0;
  for ( let i = 0; i < n_points; i++ ) {
    let delta = 0;
    const ff = tags[base + i];
    if ( ff & Y_SHORT_VECTOR ) {
      if ( p + 1 > limit ) return ERR.Invalid_Outline;
      delta = face.data[p++];
      if ( !( ff & Y_POSITIVE ) ) delta = -delta;
    }
    else if ( !( ff & Y_POSITIVE ) ) {   /* SAME_Y shares the bit */
      if ( p + 2 > limit ) return ERR.Invalid_Outline;
      delta = i16( face, p ); p += 2;
    }
    y += delta;
    g.pointsY[base + i] = y;
    /* the cast is for stupid compilers */
    tags[base + i] = ff & ON_CURVE_POINT;
  }

  loader.cur_n_points = n_points;
  loader.cur_n_contours = n_contours;
  return 0;
}


/* ------------------------------------------------------------------ */
/*  ttgload.c TT_Load_Composite_Glyph                                   */
/* ------------------------------------------------------------------ */

function TT_Load_Composite_Glyph( loader, p, limit ) {
  const face = loader.face, g = loader.gloader;
  const num_glyphs = loader.info.numGlyphs;
  let num_subglyphs = 0;
  let flags = 0;

  g.cur_subglyph_base = g.subglyphs.length;

  do {
    if ( p + 4 > limit ) return ERR.Invalid_Composite;

    const sub = { flags: 0, index: 0, arg1: 0, arg2: 0,
                  transform: { xx: 0x10000, xy: 0, yx: 0, yy: 0x10000 } };

    sub.flags = u16( face, p ); p += 2;
    sub.index = u16( face, p ); p += 2;
    flags = sub.flags;

    /* reject components with invalid glyph indices */
    if ( sub.index >= num_glyphs ) return ERR.Invalid_Composite;

    let count = 2;
    if ( flags & ARGS_ARE_WORDS ) count += 2;
    if ( flags & WE_HAVE_A_SCALE ) count += 2;
    else if ( flags & WE_HAVE_AN_XY_SCALE ) count += 4;
    else if ( flags & WE_HAVE_A_2X2 ) count += 8;
    if ( p + count > limit ) return ERR.Invalid_Composite;

    if ( flags & ARGS_ARE_XY_VALUES ) {
      if ( flags & ARGS_ARE_WORDS ) {
        sub.arg1 = i16( face, p ); p += 2;
        sub.arg2 = i16( face, p ); p += 2;
      }
      else {
        sub.arg1 = ( face.data[p] << 24 ) >> 24; p += 1;   /* FT_NEXT_CHAR */
        sub.arg2 = ( face.data[p] << 24 ) >> 24; p += 1;
      }
    }
    else {
      if ( flags & ARGS_ARE_WORDS ) {
        sub.arg1 = u16( face, p ); p += 2;
        sub.arg2 = u16( face, p ); p += 2;
      }
      else {
        sub.arg1 = face.data[p++];
        sub.arg2 = face.data[p++];
      }
    }

    /* F2Dot14 scales are widened to 16.16 by multiplying by 4 */
    let xx = 0x10000, yy = 0x10000, xy = 0, yx = 0;
    if ( flags & WE_HAVE_A_SCALE ) { xx = i16( face, p ) * 4; p += 2; yy = xx; }
    else if ( flags & WE_HAVE_AN_XY_SCALE ) {
      xx = i16( face, p ) * 4; p += 2;
      yy = i16( face, p ) * 4; p += 2;
    }
    else if ( flags & WE_HAVE_A_2X2 ) {
      xx = i16( face, p ) * 4; p += 2;
      yx = i16( face, p ) * 4; p += 2;
      xy = i16( face, p ) * 4; p += 2;
      yy = i16( face, p ) * 4; p += 2;
    }
    sub.transform = { xx, xy, yx, yy };

    g.subglyphs.push( sub );
    num_subglyphs++;
  } while ( flags & MORE_COMPONENTS );

  g.cur_num_subglyphs = num_subglyphs;

  /* the composite instructions (if any) start right here */
  loader.ins_pos = p;
  return 0;
}


/* ------------------------------------------------------------------ */
/*  ttgload.c TT_Process_Simple_Glyph                                   */
/* ------------------------------------------------------------------ */

function TT_Process_Simple_Glyph( loader ) {
  const g = loader.gloader;
  const base = g.n_points;
  let n_points = loader.cur_n_points;

  ensurePoints( g, base + n_points + 4 );

  /* set phantom points (still in font units) */
  g.pointsX[base + n_points]     = loader.pp1.x; g.pointsY[base + n_points]     = loader.pp1.y;
  g.pointsX[base + n_points + 1] = loader.pp2.x; g.pointsY[base + n_points + 1] = loader.pp2.y;
  g.pointsX[base + n_points + 2] = loader.pp3.x; g.pointsY[base + n_points + 2] = loader.pp3.y;
  g.pointsX[base + n_points + 3] = loader.pp4.x; g.pointsY[base + n_points + 3] = loader.pp4.y;

  n_points += 4;

  if ( loader.hinted ) {
    /* tt_prepare_zone( zone, &gloader->current, 0, 0 ): the current window
     * starts at the absolute index `base', and its contours are still
     * glyph-relative, hence first_point = 0. */
    prepareZone( loader.zone, g, n_points, loader.cur_n_contours, base, g.n_contours, 0 );
    /* orus holds the UNSCALED coordinates, copied before scaling below */
    loader.zone.orusX.set( loader.zone.curX.subarray( 0, n_points ) );
    loader.zone.orusY.set( loader.zone.curY.subarray( 0, n_points ) );
  }

  /* scale the glyph (phantom points included) */
  const x_scale = loader.size.metrics.x_scale, y_scale = loader.size.metrics.y_scale;
  for ( let i = 0; i < n_points; i++ ) {
    g.pointsX[base + i] = FT_MulFix( g.pointsX[base + i], x_scale );
    g.pointsY[base + i] = FT_MulFix( g.pointsY[base + i], y_scale );
  }

  loader.pp1 = { x: g.pointsX[base + n_points - 4], y: g.pointsY[base + n_points - 4] };
  loader.pp2 = { x: g.pointsX[base + n_points - 3], y: g.pointsY[base + n_points - 3] };
  loader.pp3 = { x: g.pointsX[base + n_points - 2], y: g.pointsY[base + n_points - 2] };
  loader.pp4 = { x: g.pointsX[base + n_points - 1], y: g.pointsY[base + n_points - 1] };

  if ( loader.hinted ) {
    loader.curPointBase = base;
    return TT_Hint_Glyph( loader, 0 );
  }
  return 0;
}


/* ------------------------------------------------------------------ */
/*  ttgload.c TT_Process_Composite_Component                            */
/* ------------------------------------------------------------------ */

function TT_Process_Composite_Component( loader, sub, start_point, num_base_points ) {
  const g = loader.gloader;
  const first = num_base_points;
  const nCur = g.n_points - num_base_points;
  let x, y;

  const have_scale = !!( sub.flags & ( WE_HAVE_A_SCALE | WE_HAVE_AN_XY_SCALE | WE_HAVE_A_2X2 ) );

  /* FT_Outline_Transform on the current component */
  if ( have_scale ) {
    const m = sub.transform;
    for ( let i = first; i < first + nCur; i++ ) {
      const px = g.pointsX[i], py = g.pointsY[i];
      g.pointsX[i] = FT_MulFix( px, m.xx ) + FT_MulFix( py, m.xy );
      g.pointsY[i] = FT_MulFix( px, m.yx ) + FT_MulFix( py, m.yy );
    }
  }

  if ( !( sub.flags & ARGS_ARE_XY_VALUES ) ) {
    /* match the l-th point of the new component to the k-th point of the
     * previously loaded components */
    const num_points = g.n_points;
    const k = sub.arg1 + start_point;
    const l = sub.arg2 + num_base_points;
    if ( k >= num_base_points || l >= num_points ) return ERR.Invalid_Composite;
    x = g.pointsX[k] - g.pointsX[l];
    y = g.pointsY[k] - g.pointsY[l];
  }
  else {
    x = sub.arg1;
    y = sub.arg2;
    if ( !x && !y ) return 0;

    /* TT_CONFIG_OPTION_COMPONENT_OFFSET_SCALED is #undef'd in this build,
     * so the test is on SCALED_COMPONENT_OFFSET. */
    if ( have_scale && ( sub.flags & SCALED_COMPONENT_OFFSET ) ) {
      const mac_xscale = FT_Hypot( sub.transform.xx, sub.transform.xy );
      const mac_yscale = FT_Hypot( sub.transform.yy, sub.transform.yx );
      x = FT_MulFix( x, mac_xscale );
      y = FT_MulFix( y, mac_yscale );
    }

    x = FT_MulFix( x, loader.size.metrics.x_scale );
    y = FT_MulFix( y, loader.size.metrics.y_scale );

    if ( ( sub.flags & ROUND_XY_TO_GRID ) && loader.hinted ) {
      /* v35: both axes are rounded (no backward-compatibility curfew) */
      x = FT_PIX_ROUND( x );
      y = FT_PIX_ROUND( y );
    }
  }

  if ( x || y )
    for ( let i = first; i < first + nCur; i++ ) {
      g.pointsX[i] += x;
      g.pointsY[i] += y;
    }

  return 0;
}


/* ------------------------------------------------------------------ */
/*  ttgload.c TT_Process_Composite_Glyph                                */
/* ------------------------------------------------------------------ */

function TT_Process_Composite_Glyph( loader, start_point, start_contour ) {
  const face = loader.face, g = loader.gloader, exc = loader.exec;

  ensurePoints( g, g.n_points + 4 );

  g.pointsX[g.n_points]     = loader.pp1.x; g.pointsY[g.n_points]     = loader.pp1.y;
  g.pointsX[g.n_points + 1] = loader.pp2.x; g.pointsY[g.n_points + 1] = loader.pp2.y;
  g.pointsX[g.n_points + 2] = loader.pp3.x; g.pointsY[g.n_points + 2] = loader.pp3.y;
  g.pointsX[g.n_points + 3] = loader.pp4.x; g.pointsY[g.n_points + 3] = loader.pp4.y;

  /* TT_Load_Composite_Glyph only gave us the offset of the instructions */
  const ip = loader.ins_pos;
  if ( ip + 2 > face.data.length ) return 0;
  const n_ins = u16( face, ip );

  exc.glyphIns = null;
  exc.glyphSize = 0;

  if ( !n_ins ) return 0;

  /* don't trust maxSizeOfInstructions; only a rough safety check */
  if ( n_ins > loader.byte_len ) return ERR.Too_Many_Hints;

  exc.glyphIns = face.data.slice( ip + 2, ip + 2 + n_ins );
  exc.glyphSize = n_ins;

  prepareZone( loader.zone, g,
               g.n_points + 4 - start_point, g.n_contours - start_contour,
               start_point, start_contour, start_point );

  /* Some points are likely touched during execution of instructions on
   * components.  So let's untouch them. */
  for ( let i = 0; i < loader.zone.n_points - 4; i++ )
    loader.zone.tags[i] &= ~FT_CURVE_TAG_TOUCH_BOTH;

  loader.curPointBase = start_point;
  return TT_Hint_Glyph( loader, 1 );
}


/* ------------------------------------------------------------------ */
/*  ttgload.c tt_loader_set_pp / tt_get_metrics                         */
/* ------------------------------------------------------------------ */

function tt_get_metrics( loader, gid ) {
  const h = getHMetrics( loader.face, loader.info, gid );
  const v = getVMetrics( loader.face, loader.info, gid, loader.bbox.yMax );

  loader.left_bearing = h.lsb;
  loader.advance      = h.advance;
  loader.top_bearing  = v.tsb;
  loader.vadvance     = v.ah;

  if ( !loader.linear_def ) { loader.linear_def = 1; loader.linear = h.advance; }
}

/* v35: pp3.x / pp4.x stay 0 (the advance/2 rule is v40-only). */
function tt_loader_set_pp( loader ) {
  loader.pp1 = { x: loader.bbox.xMin - loader.left_bearing, y: 0 };
  loader.pp2 = { x: loader.pp1.x + loader.advance, y: 0 };
  loader.pp3 = { x: 0, y: loader.bbox.yMax + loader.top_bearing };
  loader.pp4 = { x: 0, y: loader.pp3.y - loader.vadvance };
}


/* ------------------------------------------------------------------ */
/*  ttgload.c load_truetype_glyph                                       */
/* ------------------------------------------------------------------ */

function load_truetype_glyph( loader, gid, recurse_count, header_only ) {
  const face = loader.face, info = loader.info, g = loader.gloader;

  if ( recurse_count > 100 ) return ERR.Invalid_Composite;

  loader.glyph_index = gid;

  const x_scale = loader.size.metrics.x_scale;
  const y_scale = loader.size.metrics.y_scale;

  const range = glyphRange( face, info, gid );
  loader.byte_len = range.length;

  const glyfBase = info.glyf ? info.glyf.offset : 0;
  let p = glyfBase + range.offset;

  if ( loader.byte_len > 0 ) {
    if ( !info.glyf ) return ERR.Invalid_Composite;
    if ( p + 10 > face.data.length ) return ERR.Invalid_Outline;
    loader.n_contours = i16( face, p );
    loader.bbox = {
      xMin: i16( face, p + 2 ), yMin: i16( face, p + 4 ),
      xMax: i16( face, p + 6 ), yMax: i16( face, p + 8 ),
    };
  }

  /* a space glyph */
  if ( loader.byte_len === 0 || loader.n_contours === 0 )
    loader.bbox = { xMin: 0, yMin: 0, xMax: 0, yMax: 0 };

  /* metrics must be computed after the header, since yMax may be needed */
  tt_get_metrics( loader, gid );

  if ( header_only ) return 0;

  tt_loader_set_pp( loader );

  /* shortcut for empty glyphs */
  if ( loader.byte_len === 0 || loader.n_contours === 0 ) {
    /* scale phantom points; they get rounded in TT_Hint_Glyph */
    loader.pp1.x = FT_MulFix( loader.pp1.x, x_scale );
    loader.pp2.x = FT_MulFix( loader.pp2.x, x_scale );
    /* pp1.y and pp2.y are always zero */
    loader.pp3.x = FT_MulFix( loader.pp3.x, x_scale );
    loader.pp3.y = FT_MulFix( loader.pp3.y, y_scale );
    loader.pp4.x = FT_MulFix( loader.pp4.x, x_scale );
    loader.pp4.y = FT_MulFix( loader.pp4.y, y_scale );
    return 0;
  }

  const limit = p + loader.byte_len;
  p += 10;

  if ( loader.n_contours > 0 ) {
    let err = TT_Load_Simple_Glyph( loader, p, limit );
    if ( err ) return err;
    err = TT_Process_Simple_Glyph( loader );
    if ( err ) return err;
    loaderAdd( g, loader.cur_n_points, loader.cur_n_contours );
    return 0;
  }

  /* ---- composite ---- */
  loader.n_contours = -1;

  const start_point   = g.n_points;
  const start_contour = g.n_contours;

  let err = TT_Load_Composite_Glyph( loader, p, limit );
  if ( err ) return err;

  const ins_pos = loader.ins_pos;
  const num_subglyphs = g.cur_num_subglyphs;
  const num_base_subgs = g.num_subglyphs;
  const subBase = g.cur_subglyph_base;

  /* scale phantom points; they get rounded in TT_Hint_Glyph */
  loader.pp1.x = FT_MulFix( loader.pp1.x, x_scale );
  loader.pp2.x = FT_MulFix( loader.pp2.x, x_scale );
  loader.pp3.x = FT_MulFix( loader.pp3.x, x_scale );
  loader.pp3.y = FT_MulFix( loader.pp3.y, y_scale );
  loader.pp4.x = FT_MulFix( loader.pp4.x, x_scale );
  loader.pp4.y = FT_MulFix( loader.pp4.y, y_scale );

  let num_points = start_point;
  const old_byte_len = loader.byte_len;

  /* FT_GlyphLoader_Add for the composite itself (0 points, moves the
   * subglyph count from `current' into `base') */
  loaderAdd( g, 0, 0 );

  let sub = null;
  for ( let n = 0; n < num_subglyphs; n++ ) {
    sub = g.subglyphs[subBase + n];

    const pp = [ loader.pp1, loader.pp2, loader.pp3, loader.pp4 ];
    const linear_hadvance = loader.linear;
    const linear_vadvance = loader.vadvance;

    const num_base_points = g.n_points;

    err = load_truetype_glyph( loader, sub.index, recurse_count + 1, false );
    if ( err ) return err;

    /* restore phantom points if necessary */
    if ( !( sub.flags & USE_MY_METRICS ) ) {
      loader.pp1 = pp[0]; loader.pp2 = pp[1];
      loader.pp3 = pp[2]; loader.pp4 = pp[3];
      loader.linear = linear_hadvance;
      loader.vadvance = linear_vadvance;
    }

    num_points = g.n_points;
    if ( num_points === num_base_points ) continue;

    err = TT_Process_Composite_Component( loader, sub, start_point, num_base_points );
    if ( err ) return err;
  }

  loader.byte_len = old_byte_len;
  loader.ins_pos = ins_pos;

  if ( loader.hinted && sub && ( sub.flags & WE_HAVE_INSTR ) &&
       num_points > start_point ) {
    err = TT_Process_Composite_Glyph( loader, start_point, start_contour );
    if ( err ) return err;
  }

  /* retain the overlap flag */
  if ( g.num_subglyphs && ( g.subglyphs[0].flags & OVERLAP_COMPOUND ) )
    g.flags |= FT_OUTLINE_OVERLAP;

  return 0;
}


/* ------------------------------------------------------------------ */
/*  public entry point (CONTRACT.md)                                    */
/* ------------------------------------------------------------------ */

/* loadGlyfOutlineHinted( face, gid, size ) -> { outline (26.6), advance }
 *
 * `size' is { x_ppem, y_ppem, x_scale, y_scale } as produced by
 * ftscale.js requestCharSize(). */
export function loadGlyfOutlineHinted( face, gid, sizeMetrics ) {
  /* ttdriver.c tt_glyph_load: the hinted path runs on `ttsize->hinted_metrics',
   * which is where tt_size_reset's `head.Flags & 8' integer-ppem rederivation
   * of x_scale/y_scale lands -- NOT the base metrics the unhinted path uses.
   * ftscale.js exposes those as `.hinted'; accept either shape so that both
   * `requestCharSize(...)' and `requestCharSize(...).hinted' work. */
  const sm = ( sizeMetrics && sizeMetrics.hinted ) ? sizeMetrics.hinted : sizeMetrics;
  const size = getSize( face, sm );
  if ( size.error ) {
    const e = new Error( 'tthint: fpgm/prep failed' );
    e.ftError = size.error;
    throw e;
  }

  const info = size.info;
  const exc  = size.context;

  /* --- tt_loader_init --- */
  let hinted = true;

  /* check whether the cvt program has disabled hinting */
  if ( size.GS.instruct_control & 1 ) hinted = false;
  /* check whether GS modifications should be reverted */
  if ( size.GS.instruct_control & 2 ) size.GS = tt_default_graphics_state();

  TT_Load_Context( exc, face, size );

  /* Use the hdmx table unless FT_LOAD_COMPUTE_METRICS is set (it isn't for
   * ftlint) or the face is fixed-pitch. */
  const widthp = ( hinted && !info.isFixedPitch ) ? size.widthp : -1;

  const loader = {
    face, info, size, exec: exc, hinted,
    gloader: newGloader(),
    zone: newGlyphZone( 0, 0 ),
    bbox: { xMin: 0, yMin: 0, xMax: 0, yMax: 0 },
    n_contours: 0,
    byte_len: 0, ins_pos: 0,
    left_bearing: 0, advance: 0, top_bearing: 0, vadvance: 0,
    linear: 0, linear_def: 0,
    pp1: { x: 0, y: 0 }, pp2: { x: 0, y: 0 },
    pp3: { x: 0, y: 0 }, pp4: { x: 0, y: 0 },
    cur_n_points: 0, cur_n_contours: 0,
    curPointBase: 0,
  };

  const err = load_truetype_glyph( loader, gid, 0, false );

  const g = loader.gloader;
  const n_points = err ? 0 : g.n_points;
  const n_contours = err ? 0 : g.n_contours;

  /* --- TT_Load_Glyph tail --- */
  let flags = g.flags;

  /* This is _critical_ for correct monochrome output at small sizes. */
  if ( size.metrics.y_ppem < 24 ) flags |= FT_OUTLINE_HIGH_PRECISION;

  if ( hinted ) {
    if ( exc.GS.scan_control ) {
      switch ( exc.GS.scan_type ) {
      case 0: flags |= FT_OUTLINE_INCLUDE_STUBS; break;
      case 1: break;   /* the default rendering mode */
      case 4: flags |= FT_OUTLINE_SMART_DROPOUTS | FT_OUTLINE_INCLUDE_STUBS; break;
      case 5: flags |= FT_OUTLINE_SMART_DROPOUTS; break;
      default: flags |= FT_OUTLINE_IGNORE_DROPOUTS; break;
      }
    }
    else flags |= FT_OUTLINE_IGNORE_DROPOUTS;
  }

  const x = new Int32Array( n_points );
  const y = new Int32Array( n_points );
  const tags = new Uint8Array( n_points );
  const contours = new Int32Array( n_contours );

  /* Translate the array so that (0,0) is the glyph's origin. */
  const dx = loader.pp1.x;
  for ( let i = 0; i < n_points; i++ ) {
    x[i] = g.pointsX[i] - ( dx ? dx : 0 );
    y[i] = g.pointsY[i];
    tags[i] = g.tags[i];
  }
  for ( let i = 0; i < n_contours; i++ ) contours[i] = g.contours[i];

  /* compute_glyph_metrics: the hinted horizontal advance */
  let advance;
  if ( widthp >= 0 ) advance = u8( face, widthp + gid ) * 64;
  else advance = loader.pp2.x - loader.pp1.x;

  return {
    outline: { n_points, n_contours, x, y, tags, contours, flags },
    advance,
    error: err,
  };
}
