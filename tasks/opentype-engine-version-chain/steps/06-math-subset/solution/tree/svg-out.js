/****************************************************************************
 *
 * svg-out.js  --  W3
 *
 *   Port of HarfBuzz 14.4.0's vector SVG serializer, i.e. of
 *
 *     src/hb-vector-buf.hh    hb_vector_buf_t::append_num / append_svg_color
 *     src/hb-vector-draw.cc   hb_vector_draw_render_svg + the SVG draw callbacks
 *     src/hb-vector-draw.hh   hb_vector_draw_t (path/body buffers, flush_path_svg)
 *
 *   This file owns ONLY the serialization.  Shaping, outline extraction and
 *   extents live in bin/hb-vector.js.
 *
 *   The two things that are easy to get wrong and are ported literally:
 *
 *   1. `append_num' is NOT printf.  It has a fast integer path that rounds
 *      half-to-even, strips trailing fractional zeros, and snaps values
 *      below a precision-dependent threshold to zero.  It also operates on
 *      a C `float' (32-bit), so we round inputs through Math.fround.
 *
 *   2. Coordinates are divided by the scale factor on the way out.  hb-vector
 *      sets SUBPIXEL_BITS = 6, so the font is scaled to upem*64 and the draw
 *      callbacks receive font-units*64; append_xy divides by 64 again, which
 *      is why path data comes out in font units.
 *
 ****************************************************************************/

/* hb-vector-buf.hh: static const double _hb_vector_buf_pow10[13] */
const POW10 = [ 1, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6,
                1e7, 1e8, 1e9, 1e10, 1e11, 1e12 ];

/**
 * hb_vector_buf_t::append_num (float v, unsigned p)
 *
 * `v' is a C float; the caller is responsible for having produced it with
 * float arithmetic.  We Math.fround it here so the threshold test and the
 * scaled product match the C bit-for-bit.
 */
export function appendNum( v, p ) {
  if ( p > 12 ) p = 12;

  v = Math.fround( v );

  /* float rounded_zero_threshold = 0.5f; repeated *= 0.1f  -- in FLOAT,
   * so the accumulated error is part of the contract.                    */
  let thr = Math.fround( 0.5 );
  for ( let i = 0; i < p; i++ ) thr = Math.fround( thr * Math.fround( 0.1 ) );
  if ( Math.abs( v ) < thr ) v = 0;

  if ( !( v === v ) || !isFinite( v ) ) return '0';

  /* Fast path: fixed-point decimal via integer math, rounding half-to-even,
   * which is what snprintf("%.pf") does. */
  const scaled = v * POW10[p];               /* (double)v * pow10 */
  if ( Math.abs( scaled ) < 9.0e18 ) {
    const fl = Math.floor( scaled );
    const diff = scaled - fl;
    let n = fl;
    /* if (diff > 0.5 || (diff == 0.5 && (n & 1))) n++;  -- n&1 is the
     * two's-complement low bit, i.e. "n is odd", for negatives too.      */
    if ( diff > 0.5 || ( diff === 0.5 && ( n % 2 !== 0 ) ) ) n++;

    let u = Math.abs( n );
    let frac = p;
    while ( frac && u % 10 === 0 ) { u = u / 10; frac--; }

    let s = '';
    let d = 0;
    do {
      s = String( u % 10 ) + s;
      u = Math.floor( u / 10 );
      if ( ++d === frac ) s = '.' + s;
    } while ( u || d < frac + 1 );

    if ( v < 0 ) s = '-' + s;
    return s;
  }

  /* Slow path: printf then strip trailing zeros. */
  let out = v.toFixed( p );
  if ( out.indexOf( '.' ) >= 0 ) {
    out = out.replace( /0+$/, '' );
    if ( out.endsWith( '.' ) ) out = out.slice( 0, -1 );
  }
  return out;
}

/* hb_color_t is BGRA-packed; we just carry {r,g,b,a}. */
export const COLOR = ( r, g, b, a ) => ( { r, g, b, a } );

const HEX = '0123456789ABCDEF';

/**
 * hb_vector_buf_t::append_svg_color.
 * Emits #RGB when each channel's nibbles match, else #RRGGBB.  With
 * `withAlpha' and a != 255 it closes the attribute and opens fill-opacity,
 * exactly as the C does (the quoting is deliberately asymmetric).
 */
export function appendSvgColor( c, withAlpha ) {
  let s = '#';
  if ( ( ( c.r >> 4 ) === ( c.r & 0xF ) ) &&
       ( ( c.g >> 4 ) === ( c.g & 0xF ) ) &&
       ( ( c.b >> 4 ) === ( c.b & 0xF ) ) ) {
    s += HEX[ c.r & 0xF ] + HEX[ c.g & 0xF ] + HEX[ c.b & 0xF ];
  } else {
    s += HEX[ ( c.r >> 4 ) & 15 ] + HEX[ c.r & 15 ] +
         HEX[ ( c.g >> 4 ) & 15 ] + HEX[ c.g & 15 ] +
         HEX[ ( c.b >> 4 ) & 15 ] + HEX[ c.b & 15 ];
  }
  if ( withAlpha && c.a !== 255 )
    s += '" fill-opacity="' + appendNum( c.a / 255, 4 );
  return s;
}


/**
 * hb_vector_draw_t, SVG format only.
 *
 * Usage mirrors the C: set scale factor / extents / precision / colors,
 * then for each glyph set the transform, call newPath() and feed the draw
 * callbacks, then render().
 */
export class SvgDraw {
  constructor() {
    this.precision = 2;
    this.xScaleFactor = 1;
    this.yScaleFactor = 1;
    /* hb_transform_t {xx, yx, xy, yy, x0, y0} */
    this.transform = { xx: 1, yx: 0, xy: 0, yy: 1, x0: 0, y0: 0 };
    this.extents = null;              /* {x, y, width, height} in output space */
    this.foreground = COLOR( 0, 0, 0, 255 );
    this.background = COLOR( 0, 0, 0, 0 );
    this.body = [];
    this.path = [];
    this.defs = [];
  }

  setScaleFactor( x, y ) {
    this.xScaleFactor = x > 0 ? x : 1;
    this.yScaleFactor = y > 0 ? y : 1;
  }

  /* hb_vector_draw_set_extents: input-space extents divided by the scale
   * factors, normalised so width/height are positive, then unioned.      */
  setExtents( e ) {
    if ( !e ) { this.extents = null; return; }
    if ( e.width === 0 || e.height === 0 ) return;

    const x0 = e.x / this.xScaleFactor;
    const y0 = e.y / this.yScaleFactor;
    const x1 = x0 + e.width / this.xScaleFactor;
    const y1 = y0 + e.height / this.yScaleFactor;
    const n = { x: Math.min( x0, x1 ), y: Math.min( y0, y1 ),
                width: Math.abs( x1 - x0 ), height: Math.abs( y1 - y0 ) };

    if ( this.extents ) {
      const ax0 = Math.min( this.extents.x, n.x );
      const ay0 = Math.min( this.extents.y, n.y );
      const ax1 = Math.max( this.extents.x + this.extents.width, n.x + n.width );
      const ay1 = Math.max( this.extents.y + this.extents.height, n.y + n.height );
      this.extents = { x: ax0, y: ay0, width: ax1 - ax0, height: ay1 - ay0 };
    } else {
      this.extents = n;
    }
  }

  setTransform( xx, yx, xy, yy, dx, dy ) {
    this.transform = { xx, yx, xy, yy, x0: dx, y0: dy };
  }

  /* hb_vector_transform_point + the /scale_factor divide */
  _xy( x, y ) {
    const t = this.transform;
    /* hb_transform_t::transform_point */
    const nx = t.xx * x + t.xy * y + t.x0;
    const ny = t.yx * x + t.yy * y + t.y0;
    return [ Math.fround( nx / this.xScaleFactor ),
             Math.fround( ny / this.yScaleFactor ) ];
  }

  _appendXY( x, y ) {
    const [ tx, ty ] = this._xy( x, y );
    this.path.push( appendNum( tx, this.precision ) );
    this.path.push( ',' );
    this.path.push( appendNum( ty, this.precision ) );
  }

  moveTo( x, y )  { this.path.push( 'M' ); this._appendXY( x, y ); }
  lineTo( x, y )  { this.path.push( 'L' ); this._appendXY( x, y ); }
  quadraticTo( cx, cy, x, y ) {
    this.path.push( 'Q' ); this._appendXY( cx, cy );
    this.path.push( ' ' ); this._appendXY( x, y );
  }
  cubicTo( c1x, c1y, c2x, c2y, x, y ) {
    this.path.push( 'C' ); this._appendXY( c1x, c1y );
    this.path.push( ' ' ); this._appendXY( c2x, c2y );
    this.path.push( ' ' ); this._appendXY( x, y );
  }
  closePath() { this.path.push( 'Z' ); }

  /* hb_vector_draw_t::flush_path_svg */
  flushPath() {
    if ( !this.path.length ) return;
    this.body.push( '<path d="' + this.path.join( '' ) + '" fill="' +
                    appendSvgColor( this.foreground, true ) + '"/>\n' );
    this.path = [];
  }

  newPath() { this.flushPath(); }

  setForeground( c ) { this.flushPath(); this.foreground = c; }
  setBackground( c ) { this.background = c; }

  /* hb_vector_draw_render_svg */
  render() {
    this.flushPath();
    if ( !this.extents ) return null;

    const P = ( v ) => appendNum( v, this.precision );

    /* Extents are Y-up (font space); SVG is Y-down, and the global
     * scale(1,-1) wrapper flips the content, so the viewBox maps the
     * flipped range. */
    const vbX = this.extents.x;
    const vbY = -( this.extents.y + this.extents.height );
    const vbW = this.extents.width;
    const vbH = this.extents.height;

    let out = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="' +
              P( vbX ) + ' ' + P( vbY ) + ' ' + P( vbW ) + ' ' + P( vbH ) +
              '" width="' + P( vbW ) + '" height="' + P( vbH ) + '">\n';

    if ( this.defs.length )
      out += '<defs>\n' + this.defs.join( '' ) + '</defs>\n';

    if ( this.background.a ) {
      out += '<rect x="' + P( vbX ) + '" y="' + P( vbY ) +
             '" width="' + P( vbW ) + '" height="' + P( vbH ) +
             '" fill="rgb(' + this.background.r + ',' + this.background.g +
             ',' + this.background.b + ')"';
      if ( this.background.a < 255 )
        out += ' fill-opacity="' + appendNum( this.background.a / 255, 4 ) + '"';
      out += '/>\n';
    }

    if ( this.body.length )
      out += '<g transform="scale(1,-1)">\n' + this.body.join( '' ) + '</g>\n';

    out += '</svg>\n';
    return out;
  }
}


/**
 * hb_draw_funcs_t's state machine (hb-draw.hh) wrapped around a sink.
 *
 * This layer is NOT optional decoration: it is where several observable
 * details of the output come from.
 *
 *   - move_to() EMITS NOTHING.  It closes any open path and records the
 *     current point; the `M' is emitted lazily by start_path() when the
 *     first drawing operation arrives.
 *   - close_path() emits a `line_to(path_start)' first whenever the
 *     current point differs from the contour start, then the `Z'.  This
 *     is why HarfBuzz's CFF/CFF2 path data carries an explicit closing
 *     line that a naive serializer omits.
 *   - the session destructor closes any still-open path.
 */
export class DrawSession {
  constructor( sink ) {
    this.sink = sink;
    this.pathOpen = false;
    this.startX = 0; this.startY = 0;
    this.curX = 0; this.curY = 0;
  }
  _startPath() {
    this.sink.moveTo( this.curX, this.curY );
    this.pathOpen = true;
    this.startX = this.curX; this.startY = this.curY;
  }
  moveTo( x, y ) {
    if ( this.pathOpen ) this.closePath();
    this.curX = x; this.curY = y;
  }
  lineTo( x, y ) {
    if ( !this.pathOpen ) this._startPath();
    this.sink.lineTo( x, y );
    this.curX = x; this.curY = y;
  }
  quadraticTo( cx, cy, x, y ) {
    if ( !this.pathOpen ) this._startPath();
    this.sink.quadraticTo( cx, cy, x, y );
    this.curX = x; this.curY = y;
  }
  cubicTo( c1x, c1y, c2x, c2y, x, y ) {
    if ( !this.pathOpen ) this._startPath();
    this.sink.cubicTo( c1x, c1y, c2x, c2y, x, y );
    this.curX = x; this.curY = y;
  }
  closePath() {
    if ( this.pathOpen ) {
      if ( this.startX !== this.curX || this.startY !== this.curY )
        this.sink.lineTo( this.startX, this.startY );
      this.sink.closePath();
    }
    this.pathOpen = false;
    this.startX = this.curX = this.startY = this.curY = 0;
  }
  finish() { this.closePath(); }
}


/* ==================================================================== *
 * PAINT PATH  --  port of src/hb-vector-paint-svg.cc + hb-vector-paint.hh
 *
 * This is a SECOND serializer, not an extension of SvgDraw.  The
 * differences that matter, all verified against recorded output:
 *
 *   - the <svg> header carries xmlns:xlink, and <g transform="scale(1,-1)">
 *     is emitted unconditionally (SvgDraw emits it only for a non-empty body).
 *   - ids in <defs> use hb_vector_paint_t::id_prefix, which util/ never
 *     sets, so the prefix is empty: p0, clip-p0, c0, cp0, gr0.
 *   - path/clip-glyph geometry is in font units, but clip RECTANGLES and
 *     gradient coordinates are divided by the scale factor (sx/sy),
 *     because they are emitted inside the push_font_transform(64) group
 *     while clip paths are referenced from inside the inverse group.
 *   - hb_vector_paint_t::set_precision sets only defs.precision and
 *     path.precision.  The group_stack body buffers and the output buffer
 *     keep hb_vector_buf_t's default precision of 2, so a transform
 *     matrix's dx,dy and the viewBox always print at 2 while xx..yy print
 *     at scale_precision() = max(p,7).  Unobservable at --precision=2,
 *     which every scored case uses, but ported faithfully.
 * ==================================================================== */

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

/* hb_vector_buf_t::append_base64 */
export function appendBase64( data, len ) {
  if ( len === undefined ) len = data.length;
  let out = '';
  let i = 0;
  while ( i + 2 < len ) {
    const v = ( data[i] << 16 ) | ( data[i + 1] << 8 ) | data[i + 2];
    out += B64[ ( v >> 18 ) & 63 ] + B64[ ( v >> 12 ) & 63 ] +
           B64[ ( v >> 6 ) & 63 ] + B64[ v & 63 ];
    i += 3;
  }
  if ( i < len ) {
    let v = data[i] << 16;
    if ( i + 1 < len ) v |= data[i + 1] << 8;
    out += B64[ ( v >> 18 ) & 63 ] + B64[ ( v >> 12 ) & 63 ] +
           ( i + 1 < len ? B64[ ( v >> 6 ) & 63 ] : '=' ) + '=';
  }
  return out;
}

/* hb_paint_extend_t */
export const EXTEND_PAD = 0, EXTEND_REPEAT = 1, EXTEND_REFLECT = 2;

function svgExtendModeStr( e ) {
  return e === EXTEND_REPEAT ? 'repeat' : e === EXTEND_REFLECT ? 'reflect' : 'pad';
}

/* hb_paint_composite_mode_t, in declaration order */
export const COMPOSITE = {
  CLEAR: 0, SRC: 1, DEST: 2, SRC_OVER: 3, DEST_OVER: 4, SRC_IN: 5,
  DEST_IN: 6, SRC_OUT: 7, DEST_OUT: 8, SRC_ATOP: 9, DEST_ATOP: 10,
  XOR: 11, PLUS: 12, SCREEN: 13, OVERLAY: 14, DARKEN: 15, LIGHTEN: 16,
  COLOR_DODGE: 17, COLOR_BURN: 18, HARD_LIGHT: 19, SOFT_LIGHT: 20,
  DIFFERENCE: 21, EXCLUSION: 22, MULTIPLY: 23, HSL_HUE: 24,
  HSL_SATURATION: 25, HSL_COLOR: 26, HSL_LUMINOSITY: 27
};

/* hb_vector_svg_composite_mode_str: Porter-Duff modes have no SVG
 * mix-blend-mode equivalent and return null (group emitted bare). */
const BLEND_NAME = {
  12: 'screen', 11: 'difference', 3: 'normal', 13: 'screen', 14: 'overlay',
  15: 'darken', 16: 'lighten', 17: 'color-dodge', 18: 'color-burn',
  19: 'hard-light', 20: 'soft-light', 21: 'difference', 22: 'exclusion',
  23: 'multiply', 24: 'hue', 25: 'saturation', 26: 'color', 27: 'luminosity'
};
function svgCompositeModeStr( m ) {
  return Object.prototype.hasOwnProperty.call( BLEND_NAME, m ) ? BLEND_NAME[m] : null;
}

/* hb_color_lerp: byte-channel lerp with +0.5 truncation */
export function colorLerp( c0, c1, t ) {
  const L = ( a, b ) => ( ( a + t * ( b - a ) + 0.5 ) | 0 );
  return COLOR( L( c0.r, c1.r ), L( c0.g, c1.g ), L( c0.b, c1.b ), L( c0.a, c1.a ) );
}

/* hb_paint_normalize_color_line: sort by offset, rescale to [0,1],
 * report the original (min,max) so the caller can shift the geometry. */
export function normalizeColorLine( stops ) {
  if ( !stops.length ) return [ 0, 0 ];
  stops.sort( ( a, b ) => ( a.offset > b.offset ) - ( a.offset < b.offset ) );
  let mn = stops[0].offset, mx = stops[0].offset;
  for ( let i = 1; i < stops.length; i++ ) {
    if ( stops[i].offset < mn ) mn = stops[i].offset;
    if ( stops[i].offset > mx ) mx = stops[i].offset;
  }
  if ( mn !== mx )
    for ( let i = 0; i < stops.length; i++ )
      stops[i].offset = Math.fround( ( stops[i].offset - mn ) / ( mx - mn ) );
  return [ mn, mx ];
}

/* hb_paint_reduce_linear_anchors: COLR's 3 anchors -> SVG's 2 points */
export function reduceLinearAnchors( x0, y0, x1, y1, x2, y2 ) {
  const q2x = x2 - x0, q2y = y2 - y0;
  const s = q2x * q2x + q2y * q2y;
  if ( s < 1e-6 ) return [ x0, y0, x1, y1 ];
  const q1x = x1 - x0, q1y = y1 - y0;
  const k = ( q2x * q1x + q2y * q1y ) / s;
  return [ x0, y0, x1 - k * q2x, y1 - k * q2y ];
}

const HB_PI = Math.fround( Math.PI );
const HB_2_PI = Math.fround( Math.PI * 2 );
const MAX_SWEEP_TILES = 256;

/* hb_paint_sweep_gradient_tiles */
export function sweepGradientTiles( stops, extend, startAngle, endAngle, emit ) {
  const n = stops.length;
  if ( !n ) return;

  if ( startAngle === endAngle ) {
    if ( extend === EXTEND_PAD ) {
      if ( startAngle > 0 ) emit( 0, stops[0].color, startAngle, stops[0].color );
      if ( endAngle < HB_2_PI )
        emit( endAngle, stops[n - 1].color, HB_2_PI, stops[n - 1].color );
    }
    return;
  }

  if ( endAngle < startAngle ) {
    const t = startAngle; startAngle = endAngle; endAngle = t;
    stops = stops.slice().reverse().map( ( s ) => ( { offset: 1 - s.offset, color: s.color } ) );
  }

  const angles = new Array( n ), colors = new Array( n );
  for ( let i = 0; i < n; i++ ) {
    angles[i] = startAngle + stops[i].offset * ( endAngle - startAngle );
    colors[i] = stops[i].color;
  }

  if ( extend === EXTEND_PAD ) {
    let pos;
    let color0 = colors[0];
    for ( pos = 0; pos < n; pos++ ) {
      if ( angles[pos] >= 0 ) {
        if ( pos > 0 ) {
          const f = ( 0 - angles[pos - 1] ) / ( angles[pos] - angles[pos - 1] );
          color0 = colorLerp( colors[pos - 1], colors[pos], f );
        }
        break;
      }
    }
    if ( pos === n ) { color0 = colors[n - 1]; emit( 0, color0, HB_2_PI, color0 ); return; }
    emit( 0, color0, angles[pos], colors[pos] );
    for ( pos++; pos < n; pos++ ) {
      if ( angles[pos] <= HB_2_PI )
        emit( angles[pos - 1], colors[pos - 1], angles[pos], colors[pos] );
      else {
        const f = ( HB_2_PI - angles[pos - 1] ) / ( angles[pos] - angles[pos - 1] );
        emit( angles[pos - 1], colors[pos - 1], HB_2_PI, colorLerp( colors[pos - 1], colors[pos], f ) );
        break;
      }
    }
    if ( pos === n ) {
      const c = colors[n - 1];
      emit( angles[n - 1], c, HB_2_PI, c );
    }
    return;
  }

  let span = angles[n - 1] - angles[0];
  if ( !( Math.abs( span ) >= 1e-6 ) ) return;
  span = Math.abs( span );

  if ( span < HB_2_PI / MAX_SWEEP_TILES ) {
    let r = 0, g = 0, b = 0, a = 0;
    for ( let i = 1; i < n; i++ ) {
      const w = ( angles[i] - angles[i - 1] ) / span * 0.5;
      r += ( colors[i - 1].r + colors[i].r ) * w;
      g += ( colors[i - 1].g + colors[i].g ) * w;
      b += ( colors[i - 1].b + colors[i].b ) * w;
      a += ( colors[i - 1].a + colors[i].a ) * w;
    }
    const avg = COLOR( ( r + 0.5 ) | 0, ( g + 0.5 ) | 0, ( b + 0.5 ) | 0, ( a + 0.5 ) | 0 );
    emit( 0, avg, HB_2_PI, avg );
    return;
  }

  /* REPEAT / REFLECT: tile the period across 0..2pi */
  const reflect = extend === EXTEND_REFLECT;
  let k = Math.floor( -angles[0] / span );
  let base = angles[0] + k * span;
  let guard = 0;
  while ( base < HB_2_PI && guard++ < 4 * MAX_SWEEP_TILES ) {
    const flip = reflect && ( ( ( k % 2 ) + 2 ) % 2 ) === 1;
    for ( let i = 1; i < n; i++ ) {
      const i0 = flip ? n - 1 - ( i - 1 ) : i - 1;
      const i1 = flip ? n - 1 - i : i;
      const o0 = flip ? ( angles[n - 1] - angles[i0] ) : ( angles[i0] - angles[0] );
      const o1 = flip ? ( angles[n - 1] - angles[i1] ) : ( angles[i1] - angles[0] );
      const a0 = base + o0, a1 = base + o1;
      if ( a1 <= 0 || a0 >= HB_2_PI ) continue;
      let s0 = a0, s1 = a1, c0 = colors[i0], c1 = colors[i1];
      if ( s0 < 0 ) { c0 = colorLerp( c0, c1, ( 0 - a0 ) / ( a1 - a0 ) ); s0 = 0; }
      if ( s1 > HB_2_PI ) { c1 = colorLerp( c0, c1, ( HB_2_PI - a0 ) / ( a1 - a0 ) ); s1 = HB_2_PI; }
      if ( s1 > s0 ) emit( s0, c0, s1, c1 );
    }
    base += span; k++;
  }
}

/* hb_vector_path_sink_t + the SVG path callbacks (hb-vector-path.cc).
 * Unlike SvgDraw this applies NO transform, only the scale divide. */
export class PathSink {
  constructor( precision, xScale, yScale ) {
    this.path = []; this.precision = precision;
    this.xScale = xScale; this.yScale = yScale;
  }
  _xy( x, y ) {
    this.path.push( appendNum( Math.fround( x / this.xScale ), this.precision ) );
    this.path.push( ',' );
    this.path.push( appendNum( Math.fround( y / this.yScale ), this.precision ) );
  }
  moveTo( x, y ) { this.path.push( 'M' ); this._xy( x, y ); }
  lineTo( x, y ) { this.path.push( 'L' ); this._xy( x, y ); }
  quadraticTo( cx, cy, x, y ) {
    this.path.push( 'Q' ); this._xy( cx, cy ); this.path.push( ' ' ); this._xy( x, y );
  }
  cubicTo( a, b, c, d, x, y ) {
    this.path.push( 'C' ); this._xy( a, b ); this.path.push( ' ' );
    this._xy( c, d ); this.path.push( ' ' ); this._xy( x, y );
  }
  closePath() { this.path.push( 'Z' ); }
  toString() { return this.path.join( '' ); }
}

const MAX_NESTING_LEVEL = 64;

/**
 * hb_vector_paint_t, SVG format.  The caller injects `drawGlyphFn(gid, sink)'
 * so this file stays free of font-table code.
 */
export class PaintSession {
  constructor() {
    this.precision = 2;
    this.xScaleFactor = 1;
    this.yScaleFactor = 1;
    this.transform = { xx: 1, yx: 0, xy: 0, yy: 1, x0: 0, y0: 0 };
    this.extents = null;
    this.foreground = COLOR( 0, 0, 0, 255 );
    this.background = COLOR( 0, 0, 0, 0 );
    this.palette = 0;
    this.customPaletteColors = new Map();
    this.idPrefix = '';
    this.defs = [];
    this.groupStack = [ [] ];
    this.transformStack = [];
    this.transformGroupOverflowDepth = 0;
    this.clipRectCounter = 0;
    this.clipPathCounter = 0;
    this.gradientCounter = 0;
    this.pathDefCount = 0;
    this.colorGlyphDepth = 0;
    this.activeColorGlyphs = new Set();
    this.clipPathSink = null;
    this.drawGlyphFn = null;
  }

  setScaleFactor( x, y ) {
    this.xScaleFactor = x > 0 ? x : 1;
    this.yScaleFactor = y > 0 ? y : 1;
  }
  setTransform( xx, yx, xy, yy, dx, dy ) {
    this.transform = { xx, yx, xy, yy, x0: dx, y0: dy };
  }
  setExtents( e ) {
    if ( !e ) { this.extents = null; return; }
    if ( e.width === 0 || e.height === 0 ) return;
    const x0 = e.x / this.xScaleFactor, y0 = e.y / this.yScaleFactor;
    const x1 = x0 + e.width / this.xScaleFactor, y1 = y0 + e.height / this.yScaleFactor;
    const n = { x: Math.min( x0, x1 ), y: Math.min( y0, y1 ),
                width: Math.abs( x1 - x0 ), height: Math.abs( y1 - y0 ) };
    if ( this.extents ) {
      const ax0 = Math.min( this.extents.x, n.x ), ay0 = Math.min( this.extents.y, n.y );
      const ax1 = Math.max( this.extents.x + this.extents.width, n.x + n.width );
      const ay1 = Math.max( this.extents.y + this.extents.height, n.y + n.height );
      this.extents = { x: ax0, y: ay0, width: ax1 - ax0, height: ay1 - ay0 };
    } else this.extents = n;
  }

  sx( v ) { return Math.fround( v / this.xScaleFactor ); }
  sy( v ) { return Math.fround( v / this.yScaleFactor ); }
  /* hb_vector_buf_t::scale_precision */
  scalePrecision() { return this.precision < 7 ? 7 : this.precision; }
  currentBody() { return this.groupStack[ this.groupStack.length - 1 ]; }
  /* group_stack bodies keep hb_vector_buf_t's default precision of 2 */
  bodyNum( v ) { return appendNum( v, 2 ); }

  _newSink() { return new PathSink( this.precision, this.xScaleFactor, this.yScaleFactor ); }

  /* ---- the 18 hb_paint_funcs_t callbacks ---- */

  pushTransform( xx, yx, xy, yy, dx, dy ) {
    if ( this.transformGroupOverflowDepth ) { this.transformGroupOverflowDepth++; return; }
    if ( this.transformStack.length >= MAX_NESTING_LEVEL ) {
      this.transformGroupOverflowDepth = 1; return;
    }
    const opened = !( Math.abs( xx - 1 ) < 1e-6 && Math.abs( yx ) < 1e-6 &&
                      Math.abs( xy ) < 1e-6 && Math.abs( yy - 1 ) < 1e-6 &&
                      Math.abs( this.sx( dx ) ) < 1e-6 && Math.abs( this.sy( dy ) ) < 1e-6 );
    this.transformStack.push( opened );
    if ( !opened ) return;
    const sp = this.scalePrecision();
    this.currentBody().push(
      '<g transform="matrix(' + appendNum( xx, sp ) + ',' + appendNum( yx, sp ) + ',' +
      appendNum( xy, sp ) + ',' + appendNum( yy, sp ) + ',' +
      this.bodyNum( this.sx( dx ) ) + ',' + this.bodyNum( this.sy( dy ) ) + ')">\n' );
  }

  popTransform() {
    if ( this.transformGroupOverflowDepth ) { this.transformGroupOverflowDepth--; return; }
    if ( !this.transformStack.length ) return;
    if ( this.transformStack.pop() ) this.currentBody().push( '</g>\n' );
  }

  fillGlyph( gid, isForeground, color ) {
    const sink = this._newSink();
    if ( this.drawGlyphFn ) this.drawGlyphFn( gid, sink );
    this.currentBody().push( '<path d="' + sink.toString() + '" fill="' +
                             appendSvgColor( color, true ) + '"/>\n' );
  }

  pushClipGlyph( gid ) {
    const pfx = this.idPrefix;
    const sink = this._newSink();
    if ( this.drawGlyphFn ) this.drawGlyphFn( gid, sink );
    const id = this.pathDefCount++;
    this.defs.push( '<path id="' + pfx + 'p' + id + '" d="' + sink.toString() + '"/>\n' );
    this.defs.push( '<clipPath id="' + pfx + 'clip-p' + id + '"><use href="#' +
                    pfx + 'p' + id + '"/></clipPath>\n' );
    this.currentBody().push( '<g clip-path="url(#' + pfx + 'clip-p' + id + ')">\n' );
  }

  pushClipRectangle( xmin, ymin, xmax, ymax ) {
    const pfx = this.idPrefix;
    const id = this.clipRectCounter++;
    const P = ( v ) => appendNum( v, this.precision );
    this.defs.push( '<clipPath id="' + pfx + 'c' + id + '"><rect x="' +
      P( this.sx( xmin ) ) + '" y="' + P( this.sy( ymin ) ) + '" width="' +
      P( this.sx( xmax - xmin ) ) + '" height="' + P( this.sy( ymax - ymin ) ) +
      '"/></clipPath>\n' );
    this.currentBody().push( '<g clip-path="url(#' + pfx + 'c' + id + ')">\n' );
  }

  pushClipPathStart() { this.clipPathSink = this._newSink(); return this.clipPathSink; }

  pushClipPathEnd() {
    const pfx = this.idPrefix;
    const id = this.clipPathCounter++;
    const d = this.clipPathSink ? this.clipPathSink.toString() : '';
    this.defs.push( '<clipPath id="' + pfx + 'cp' + id +
                    '"><path transform="scale(1,-1)" d="' + d + '"/></clipPath>\n' );
    this.currentBody().push( '<g clip-path="url(#' + pfx + 'cp' + id + ')">\n' );
    this.clipPathSink = null;
  }

  popClip() { this.currentBody().push( '</g>\n' ); }

  color( isForeground, c ) {
    this.currentBody().push(
      '<rect x="-32767" y="-32767" width="65534" height="65534" fill="' +
      appendSvgColor( c, true ) + '"/>\n' );
  }

  /* only PNG is emitted; every other format returns false so the caller
   * falls through to the next table in the dispatch order */
  image( blob, width, height, format, slant, extents ) {
    if ( format !== 'png ' ) return false;
    if ( !extents || !width || !height ) return false;
    if ( !blob || !blob.length ) return false;
    const b = this.currentBody();
    b.push( '<g transform="translate(' +
      this.bodyNum( this.sx( extents.x_bearing ) ) + ',' +
      this.bodyNum( this.sy( extents.y_bearing ) ) + ') scale(' +
      this.bodyNum( Math.fround( this.sx( extents.width ) / width ) ) + ',' +
      this.bodyNum( Math.fround( this.sy( extents.height ) / height ) ) + ')">\n' );
    b.push( '<image href="data:image/png;base64,' + appendBase64( blob ) +
            '" width="' + this.bodyNum( width ) + '" height="' +
            this.bodyNum( height ) + '"/>\n</g>\n' );
    return true;
  }

  _emitStops( stops ) {
    let s = '';
    for ( const st of stops ) {
      const c = st.color;
      s += '<stop offset="' + appendNum( st.offset, 4 ) + '" stop-color="rgb(' +
           c.r + ',' + c.g + ',' + c.b + ')"';
      if ( c.a !== 255 ) s += ' stop-opacity="' + appendNum( c.a / 255, 4 ) + '"';
      s += '/>\n';
    }
    return s;
  }

  _gradientRect( id ) {
    this.currentBody().push(
      '<rect x="-32767" y="-32767" width="65534" height="65534" fill="url(#' +
      this.idPrefix + 'gr' + id + ')"/>\n' );
  }

  linearGradient( colorLine, x0, y0, x1, y1, x2, y2 ) {
    const stops = colorLine.stops.slice();
    if ( !stops.length ) return;
    const [ mn, mx ] = normalizeColorLine( stops );
    const id = this.gradientCounter++;
    const [ lx0, ly0, lx1, ly1 ] = reduceLinearAnchors( x0, y0, x1, y1, x2, y2 );
    const gx0 = lx0 + mn * ( lx1 - lx0 ), gy0 = ly0 + mn * ( ly1 - ly0 );
    const gx1 = lx0 + mx * ( lx1 - lx0 ), gy1 = ly0 + mx * ( ly1 - ly0 );
    const P = ( v ) => appendNum( v, this.precision );
    this.defs.push( '<linearGradient id="' + this.idPrefix + 'gr' + id +
      '" gradientUnits="userSpaceOnUse" x1="' + P( this.sx( gx0 ) ) +
      '" y1="' + P( this.sy( gy0 ) ) + '" x2="' + P( this.sx( gx1 ) ) +
      '" y2="' + P( this.sy( gy1 ) ) + '" spreadMethod="' +
      svgExtendModeStr( colorLine.extend ) + '">\n' +
      this._emitStops( stops ) + '</linearGradient>\n' );
    this._gradientRect( id );
  }

  radialGradient( colorLine, x0, y0, r0, x1, y1, r1 ) {
    const stops = colorLine.stops.slice();
    if ( !stops.length ) return;
    const [ mn, mx ] = normalizeColorLine( stops );
    const gx0 = x0 + mn * ( x1 - x0 ), gy0 = y0 + mn * ( y1 - y0 ), gr0 = r0 + mn * ( r1 - r0 );
    const gx1 = x0 + mx * ( x1 - x0 ), gy1 = y0 + mx * ( y1 - y0 ), gr1 = r0 + mx * ( r1 - r0 );
    const id = this.gradientCounter++;
    const P = ( v ) => appendNum( v, this.precision );
    let s = '<radialGradient id="' + this.idPrefix + 'gr' + id +
      '" gradientUnits="userSpaceOnUse" cx="' + P( this.sx( gx1 ) ) +
      '" cy="' + P( this.sy( gy1 ) ) + '" r="' + P( this.sx( gr1 ) ) +
      '" fx="' + P( this.sx( gx0 ) ) + '" fy="' + P( this.sy( gy0 ) ) + '"';
    if ( gr0 > 0 ) s += ' fr="' + P( this.sx( gr0 ) ) + '"';
    s += ' spreadMethod="' + svgExtendModeStr( colorLine.extend ) + '">\n' +
         this._emitStops( stops ) + '</radialGradient>\n';
    this.defs.push( s );
    this._gradientRect( id );
  }

  sweepGradient( colorLine, cx, cy, startAngle, endAngle ) {
    const stops = colorLine.stops.slice();
    if ( !stops.length ) return;
    const [ mn, mx ] = normalizeColorLine( stops );
    const ga0 = startAngle + mn * ( endAngle - startAngle );
    const ga1 = startAngle + mx * ( endAngle - startAngle );
    const body = this.currentBody();
    const P = this.precision;
    const ccx = this.sx( cx ), ccy = this.sy( cy ), radius = 32767;
    sweepGradientTiles( stops, colorLine.extend, ga0, ga1, ( a0, c0, a1, c1 ) => {
      sweepPatch( body, P, ccx, ccy, radius, a0, c0, a1, c1 );
    } );
  }

  pushGroup() { this.groupStack.push( [] ); }

  popGroup( mode ) {
    if ( this.groupStack.length < 2 ) return;
    const group = this.groupStack.pop().join( '' );
    const body = this.currentBody();
    const blend = svgCompositeModeStr( mode );
    if ( blend !== null )
      body.push( '<g style="mix-blend-mode:' + blend + '">\n' + group + '</g>\n' );
    else body.push( group );
  }

  colorGlyph( gid, paintFn ) {
    if ( this.colorGlyphDepth >= MAX_NESTING_LEVEL ) return false;
    if ( this.activeColorGlyphs.has( gid ) ) return false;
    this.colorGlyphDepth++;
    this.activeColorGlyphs.add( gid );
    paintFn( gid );
    this.activeColorGlyphs.delete( gid );
    this.colorGlyphDepth--;
    return true;
  }

  customPaletteColor( idx ) {
    return this.customPaletteColors.has( idx ) ? this.customPaletteColors.get( idx ) : null;
  }

  /* hb_vector_paint_render_svg */
  render() {
    if ( !this.extents ) return null;
    const P = ( v ) => appendNum( v, 2 );
    const vbX = this.extents.x;
    const vbY = -( this.extents.y + this.extents.height );
    const vbW = this.extents.width;
    const vbH = this.extents.height;

    let out = '<svg xmlns="http://www.w3.org/2000/svg" ' +
              'xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="' +
              P( vbX ) + ' ' + P( vbY ) + ' ' + P( vbW ) + ' ' + P( vbH ) +
              '" width="' + P( vbW ) + '" height="' + P( vbH ) + '">\n';

    if ( this.defs.length ) out += '<defs>\n' + this.defs.join( '' ) + '</defs>\n';

    if ( this.background.a ) {
      out += '<rect x="' + P( vbX ) + '" y="' + P( vbY ) + '" width="' + P( vbW ) +
             '" height="' + P( vbH ) + '" fill="rgb(' + this.background.r + ',' +
             this.background.g + ',' + this.background.b + ')"';
      if ( this.background.a < 255 )
        out += ' fill-opacity="' + appendNum( this.background.a / 255, 4 ) + '"';
      out += '/>\n';
    }

    /* emitted unconditionally, unlike the draw writer */
    out += '<g transform="scale(1,-1)">\n' + this.groupStack[0].join( '' ) + '</g>\n';
    out += '</svg>\n';
    return out;
  }
}

/* hb_vector_svg_add_sweep_patch: approximate one angular segment with a
 * cubic wedge filled with the segment's mid colour. */
function sweepPatch( body, precision, cx, cy, radius, a0, c0in, a1, c1in ) {
  const maxAngle = HB_PI / 16;
  let numSplits = Math.ceil( Math.abs( a1 - a0 ) / maxAngle );
  if ( numSplits < 1 ) numSplits = 1;
  let p0x = Math.cos( a0 ), p0y = Math.sin( a0 );
  let color0 = c0in;
  const N = ( v ) => appendNum( v, precision );

  for ( let a = 0; a < numSplits; a++ ) {
    const k = ( a + 1 ) / numSplits;
    const angle1 = a0 + ( a1 - a0 ) * k;
    const color1 = colorLerp( c0in, c1in, k );
    const p1x = Math.cos( angle1 ), p1y = Math.sin( angle1 );
    const sp0x = cx + p0x * radius, sp0y = cy + p0y * radius;
    const sp1x = cx + p1x * radius, sp1y = cy + p1y * radius;

    let ax = p0x + p1x, ay = p0y + p1y;
    const alen = Math.sqrt( ax * ax + ay * ay );
    if ( alen === 0 ) { p0x = p1x; p0y = p1y; color0 = color1; continue; }
    ax /= alen; ay /= alen;
    const ux = -ay, uy = ax;
    const up0 = ux * p0x + uy * p0y, up1 = ux * p1x + uy * p1y;
    if ( Math.abs( up0 ) < 1e-6 || Math.abs( up1 ) < 1e-6 ) {
      p0x = p1x; p0y = p1y; color0 = color1; continue;
    }
    const d0 = ( ( p0x - ax ) * p0x + ( p0y - ay ) * p0y ) / up0;
    const d1 = ( ( p1x - ax ) * p1x + ( p1y - ay ) * p1y ) / up1;
    const c0x = ax + ux * d0, c0y = ay + uy * d0;
    const c1x = ax + ux * d1, c1y = ay + uy * d1;
    const sc0x = cx + ( c0x + ( c0x - p0x ) * 0.33333 ) * radius;
    const sc0y = cy + ( c0y + ( c0y - p0y ) * 0.33333 ) * radius;
    const sc1x = cx + ( c1x + ( c1x - p1x ) * 0.33333 ) * radius;
    const sc1y = cy + ( c1y + ( c1y - p1y ) * 0.33333 ) * radius;
    const mid = colorLerp( color0, color1, 0.5 );

    body.push( '<path d="M' + N( cx ) + ',' + N( cy ) +
               'L' + N( sp0x ) + ',' + N( sp0y ) +
               'C' + N( sc0x ) + ',' + N( sc0y ) + ' ' + N( sc1x ) + ',' + N( sc1y ) +
               ' ' + N( sp1x ) + ',' + N( sp1y ) + 'Z" fill="' +
               appendSvgColor( mid, true ) + '"/>\n' );
    p0x = p1x; p0y = p1y; color0 = color1;
  }
}

export default { appendNum, appendSvgColor, SvgDraw, DrawSession, COLOR,
                 PaintSession, PathSink, appendBase64, COMPOSITE,
                 EXTEND_PAD, EXTEND_REPEAT, EXTEND_REFLECT };

/* END */
