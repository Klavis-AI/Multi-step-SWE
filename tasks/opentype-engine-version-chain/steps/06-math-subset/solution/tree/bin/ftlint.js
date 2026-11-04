#!/usr/bin/env node
/* bin/ftlint.js -- ../ftdemos/src/ftlint.c, byte-for-byte.
 *
 * C function -> JS function map
 *   main()      -> main()
 *   Examine()   -> Examine()      "%5.2f " shape, or " void "
 *   Explore()   -> Explore()      "%3d+%-3d " X+Y turn counts
 *   Analyze()   -> Analyze()      two "%.4lf " acutance figures
 *   Checksum()  -> Checksum()     32 uppercase hex digits
 *   Error()     -> Error()        "%serror = 0x%04x, %s\n"
 *   Usage()     -> Usage()
 *   getopt()    -> getopt()       GNU-style, permuting
 *   FT_Render_Glyph + ft_smooth_render + FT_Bitmap_Convert -> renderGlyph()
 *
 * Outline loading is dispatched to
 *   glyf.js  (W2, unhinted TrueType)
 *   cff.js   (W3, `CFF ' faces)            -- guarded, optional
 *   tthint.js(W4, when load flags ask for hinting) -- guarded, optional
 * and rasterising to smooth.js (W1) -- guarded, optional.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { openFace } from '../sfnt.js';
import { loadGlyfOutlineScaled } from '../glyf.js';
import { requestCharSize, FT_Outline_Get_CBox,
         FT_MulFix, FT_MulDiv } from '../ftscale.js';
import { md5Hex } from '../md5.js';

/* ---- optional peer modules (guarded: a missing one must not crash) ---- */
let SMOOTH = null, CFF = null, TTHINT = null;
try { SMOOTH = await import( '../smooth.js' );  } catch { SMOOTH = null; }
let RASTER = null;
try { RASTER = await import( '../raster.js' );  } catch { RASTER = null; }
try { CFF    = await import( '../cff.js' );     } catch { CFF = null; }
try { TTHINT = await import( '../tthint.js' );  } catch { TTHINT = null; }

/* ---- freetype.h constants -------------------------------------------- */
const FT_LOAD_NO_SCALE   = 1 << 0;
const FT_LOAD_NO_HINTING = 1 << 1;
const FT_RENDER_MODE_MAX = 6;
const modes = [ 'normal', 'light', 'mono', 'lcd', 'lcd-v', 'sdf' ];

const FT_GLYPH_FORMAT_OUTLINE = 0x6F75746C;   /* 'outl' */
const FT_GLYPH_FORMAT_BITMAP  = 0x62697473;   /* 'bits' */

/* ftimage.h */
const FT_OUTLINE_REVERSE_FILL   = 0x4;
const FT_OUTLINE_HIGH_PRECISION = 0x100;

/* ---- fterrors.h strings (only the ones a glyph load can raise) -------- */
const ERRSTR = {
  0x00: 'no error',
  0x01: 'cannot open resource',
  0x02: 'unknown file format',
  0x03: 'broken file',
  0x04: 'invalid FreeType version',
  0x05: 'module version is too low',
  0x06: 'invalid argument',
  0x07: 'unimplemented feature',
  0x08: 'broken table',
  0x09: 'broken offset within table',
  0x0A: 'array allocation size too large',
  0x0B: 'missing module',
  0x0C: 'missing property',
  0x10: 'invalid glyph index',
  0x11: 'invalid character code',
  0x12: 'unsupported glyph image format',
  0x13: 'cannot render this glyph format',
  0x14: 'invalid outline',
  0x15: 'invalid composite glyph',
  0x16: 'too many hints',
  0x17: 'invalid pixel size',
  0x18: 'invalid SVG document',
  0x20: 'invalid object handle',
  0x21: 'invalid library handle',
  0x22: 'invalid module handle',
  0x23: 'invalid face handle',
  0x24: 'invalid size handle',
  0x25: 'invalid glyph slot handle',
  0x26: 'invalid charmap handle',
  0x27: 'invalid cache manager handle',
  0x28: 'invalid stream handle',
  0x30: 'too many modules',
  0x31: 'too many extensions',
  0x40: 'out of memory',
  0x41: 'unlisted object',
  0x51: 'cannot open stream',
  0x52: 'invalid stream seek',
  0x53: 'invalid stream skip',
  0x54: 'invalid stream read',
  0x55: 'invalid stream operation',
  0x56: 'invalid frame operation',
  0x57: 'nested frame access',
  0x58: 'invalid frame read',
  0x60: 'raster uninitialized',
  0x61: 'raster corrupted',
  0x62: 'raster overflow',
  0x63: 'negative height while rastering',
  0x70: 'too many registered caches',
  0x80: 'invalid opcode',
  0x81: 'too few arguments',
  0x82: 'stack overflow',
  0x83: 'code overflow',
  0x84: 'bad argument',
  0x85: 'division by zero',
  0x86: 'invalid reference',
  0x87: 'found debug opcode',
  0x88: 'found ENDF opcode in execution stream',
  0x89: 'nested DEFS',
  0x8A: 'invalid code range',
  0x8B: 'execution context too long',
  0x8C: 'too many function definitions',
  0x8D: 'too many instruction definitions',
  0x8E: 'SFNT font table missing',
  0x8F: 'horizontal header (hhea) table missing',
  0x90: 'locations (loca) table missing',
  0x91: 'name table missing',
  0x92: 'character map (cmap) table missing',
  0x93: 'horizontal metrics (hmtx) table missing',
  0x94: 'PostScript (post) table missing',
  0x95: 'invalid horizontal metrics',
  0x96: 'invalid character map (cmap) format',
  0x97: 'invalid ppem value',
  0x98: 'invalid vertical metrics',
  0x99: 'could not find context',
  0x9A: 'invalid PostScript (post) table format',
  0x9B: 'invalid PostScript (post) table',
  0x9C: 'found FDEF or IDEF opcode in glyf bytecode',
  0x9D: 'missing bitmap in strike',
  0x9E: 'SVG hooks have not been set',
  0xA0: 'opcode syntax error',
  0xA1: 'argument stack underflow',
  0xA2: 'ignore',
  0xA3: 'no Unicode glyph name found',
  0xA4: 'glyph too big for hinting',
  0xB0: '`STARTFONT\' field missing',
  0xB1: '`FONT\' field missing',
  0xB2: '`SIZE\' field missing',
  0xB3: '`FONTBOUNDINGBOX\' field missing',
  0xB4: '`CHARS\' field missing',
  0xB5: '`STARTCHAR\' field missing',
  0xB6: '`ENCODING\' field missing',
  0xB7: '`BBX\' field missing',
  0xB8: '`BBX\' too big',
  0xB9: 'Font header corrupted or missing fields',
  0xBA: 'Font glyphs corrupted or missing fields',
};

let out = [];
const W = ( s ) => { out.push( s ); if ( out.length > 512 ) flush(); };
const flush = () => { if ( out.length ) { process.stdout.write( out.join( '' ) ); out = []; } };

/* ======================================================================
 *  printf helpers
 * ==================================================================== */
const padL = ( s, n ) => s.length >= n ? s : ' '.repeat( n - s.length ) + s;
const padR = ( s, n ) => s.length >= n ? s : s + ' '.repeat( n - s.length );

/**
 * glibc "%.*f": correctly round the EXACT binary value of the double to `nd'
 * decimals, ties to even.  Number.prototype.toFixed is not specified to do
 * this, so we expand the double exactly with BigInt.
 */
function fmtF( x, nd ) {
  if ( Number.isNaN( x ) ) return 'nan';
  if ( !Number.isFinite( x ) ) return x > 0 ? 'inf' : '-inf';

  const neg = x < 0 || Object.is( x, -0 );
  x = Math.abs( x );

  const dv = new DataView( new ArrayBuffer( 8 ) );
  dv.setFloat64( 0, x );
  const hi = dv.getUint32( 0 ), lo = dv.getUint32( 4 );

  let biasedExp = ( hi >>> 20 ) & 0x7FF;
  let mant = ( BigInt( hi & 0xFFFFF ) << 32n ) | BigInt( lo );
  if ( biasedExp === 0 ) biasedExp = 1;            /* subnormal */
  else mant |= ( 1n << 52n );

  const e = biasedExp - 1075;                      /* value = mant * 2^e */

  let num, den;
  if ( e >= 0 ) { num = mant << BigInt( e ); den = 1n; }
  else          { num = mant;                den = 1n << BigInt( -e ); }

  num *= 10n ** BigInt( nd );

  let q = num / den;
  const r = num % den;
  const twice = r * 2n;
  if ( twice > den || ( twice === den && ( q & 1n ) === 1n ) )
    q += 1n;                                        /* round half to even */

  let s = q.toString();
  if ( nd > 0 ) {
    while ( s.length <= nd ) s = '0' + s;
    s = s.slice( 0, s.length - nd ) + '.' + s.slice( s.length - nd );
  }
  return ( neg ? '-' : '' ) + s;
}

/* ======================================================================
 *  Examine / Explore / Analyze / Checksum
 * ==================================================================== */
const SIGN = ( x ) => ( x > 0 ? 1 : 0 ) - ( x < 0 ? 1 : 0 );

function Examine( slot ) {
  if ( slot.format !== FT_GLYPH_FORMAT_OUTLINE ) {
    const f = slot.format;
    return ' ' + String.fromCharCode( ( f >>> 24 ) & 0xFF, ( f >>> 16 ) & 0xFF,
                                      ( f >>> 8 ) & 0xFF, f & 0xFF ) + ' ';
  }

  const o = slot.outline;
  let taxi = 0;
  let last = -1;

  for ( let c = 0; c < o.n_contours; c++ ) {
    const first = last + 1;
    last = o.contours[c];

    let dx = o.x[last], dy = o.y[last];
    for ( let p = first; p <= last; p++ ) {
      const vx = o.x[p], vy = o.y[p];
      dx -= vx;
      dy -= vy;
      taxi += dx < 0 ? -dx : dx;
      taxi += dy < 0 ? -dy : dy;
      dx = vx;
      dy = vy;
    }
  }

  if ( taxi ) {
    const cbox = FT_Outline_Get_CBox( o );
    const v = 0.5 * taxi / ( cbox.xMax - cbox.xMin + cbox.yMax - cbox.yMin );
    return padL( fmtF( v, 2 ), 5 ) + ' ';
  }
  return ' void ';
}

function Explore( slot ) {
  if ( slot.format !== FT_GLYPH_FORMAT_OUTLINE )
    return '   +    ';

  const o = slot.outline;
  let sx = 0, sy = 0;
  let last = -1;

  for ( let c = 0; c < o.n_contours; c++ ) {
    const first = last + 1;
    last = o.contours[c];

    let bx = 0, by = 0;
    let dx = o.x[last], dy = o.y[last];

    for ( let p = first; p <= last; p++ ) {
      const vx = o.x[p], vy = o.y[p];
      dx -= vx;
      dy -= vy;

      if ( dx ) {
        const s = SIGN( dx );
        sx += ( s === -bx ) ? 1 : 0;      /* count turns */
        bx = s;
      }
      if ( dy ) {
        const s = SIGN( dy );
        sy += ( s === -by ) ? 1 : 0;
        by = s;
      }

      dx = vx;
      dy = vy;
    }

    /* counts must be even unless the first turn was missed */
    sx += sx & 1;
    sy += sy & 1;
  }

  return padL( String( sx ), 3 ) + '+' + padR( String( sy ), 3 ) + ' ';
}

function Analyze( bmp ) {
  const b = bmp.buffer;
  const width = bmp.width, rows = bmp.rows, pitch = bmp.pitch;
  let s;

  /* X-acutance */
  {
    let s1 = 0, s2 = 0, k = 0;
    for ( let i = 0; i < rows; i++ ) {
      let d0 = 0, d1 = 0;
      for ( let j = 0; j < width; j++, k++ ) {
        const v = b ? b[k] : 0;
        d1 -= v;
        s2 += d1 >= d0 ? d1 - d0 : d0 - d1;
        s1 += d1 >= 0 ? d1 : -d1;
        d0 = d1;
        d1 = v;
      }
      s2 += d1 > d0 ? d1 - d0 : d0 - d1;
      s2 += d1;
      s1 += d1;
    }
    s = s1 ? padL( fmtF( s2 / s1, 4 ), 0 ) + ' ' : '  void ';
  }

  /* Y-acutance */
  {
    let s1 = 0, s2 = 0;
    for ( let j = 0; j < width; j++ ) {
      let d0 = 0, d1 = 0;
      let k = j;
      for ( let i = 0; i < rows; i++, k += pitch ) {
        const v = b ? b[k] : 0;
        d1 -= v;
        s2 += d1 >= d0 ? d1 - d0 : d0 - d1;
        s1 += d1 >= 0 ? d1 : -d1;
        d0 = d1;
        d1 = v;
      }
      s2 += d1 > d0 ? d1 - d0 : d0 - d1;
      s2 += d1;
      s1 += d1;
    }
    s += s1 ? fmtF( s2 / s1, 4 ) + ' ' : '  void ';
  }

  return s;
}

function Checksum( bmp ) {
  if ( !bmp.buffer )
    return md5Hex( new Uint8Array( 0 ) );
  const n = bmp.rows * bmp.pitch;
  return md5Hex( bmp.buffer.subarray( 0, n ) );
}

/* ======================================================================
 *  ft_glyphslot_preset_bitmap (base/ftobjs.c), FT_RENDER_MODE_NORMAL only.
 *  Used when W1's smooth.js is not on disk yet, so that at least the
 *  "%3ux%-4u" image size is right.
 *  TODO(W1): drop once smooth.js exposes renderNormal().
 * ==================================================================== */
function presetBitmapNormal( outline, opts = {} ) {
  const cb = FT_Outline_Get_CBox( outline );

  const pbox = {
    xMin: Math.floor( cb.xMin / 64 ), yMin: Math.floor( cb.yMin / 64 ),
    xMax: Math.floor( cb.xMax / 64 ), yMax: Math.floor( cb.yMax / 64 ),
  };
  const rem = ( v ) => v - Math.floor( v / 64 ) * 64;     /* C: v & 63 */
  const cbox = {
    xMin: rem( cb.xMin ), yMin: rem( cb.yMin ),
    xMax: rem( cb.xMax ), yMax: rem( cb.yMax ),
  };

  /* FT_RENDER_MODE_NORMAL -> the `Adjust' label */
  pbox.xMin += Math.floor( cbox.xMin / 64 );
  pbox.yMin += Math.floor( cbox.yMin / 64 );
  pbox.xMax += Math.floor( ( cbox.xMax + 63 ) / 64 );
  pbox.yMax += Math.floor( ( cbox.yMax + 63 ) / 64 );

  const width  = pbox.xMax - pbox.xMin;
  const height = pbox.yMax - pbox.yMin;

  const overflow = !!(
    width >= 0x10000 || height >= 0x10000 ||
    pbox.xMin < -0x1000000 || pbox.xMax >= 0x1000000 ||
    pbox.yMin < -0x1000000 || pbox.yMax >= 0x1000000 ||
    ( opts.x_ppem !== undefined &&
      ( width > 10 * opts.x_ppem || height > 10 * opts.y_ppem ) )
  );

  return {
    width, rows: height, pitch: width,
    left: pbox.xMin, top: pbox.yMax,
    overflow,
    buffer: null,
  };
}

/**
 * FT_Render_Glyph( slot, FT_RENDER_MODE_NORMAL ) followed by
 * FT_Bitmap_Convert( lib, &slot->bitmap, &bitmap, 1 ):
 * alignment 1 leaves width alone and gives pitch == width > 0.
 */
function renderGlyph( slot, size ) {
  /* ft_glyphslot_preset_bitmap's last overflow term needs the size:
   *   width > 10 * metrics.x_ppem || height > 10 * metrics.y_ppem
   * On overflow ft_smooth_render returns FT_THROW( Raster_Overflow ). */
  const opts = { x_ppem: size.x_ppem, y_ppem: size.y_ppem };

  /* Preferred: W1's smooth.js, which owns ft_smooth_render verbatim. */
  if ( SMOOTH && SMOOTH.renderNormal ) {
    const b = SMOOTH.renderNormal( slot.outline, opts );
    return { width: b.width, rows: b.rows, pitch: b.width,
             buffer: ( !b.rows || !b.pitch ) ? null : b.buffer,
             error: b.error || 0 };
  }

  /* Bridge until smooth.js lands: ft_smooth_render's own body.
   * TODO(W1): delete once smooth.js exports renderNormal(). */
  const pre = presetBitmapNormal( slot.outline, opts );

  if ( pre.overflow )
    return { width: pre.width, rows: pre.rows, pitch: pre.width,
             buffer: null, error: 0x62 };

  if ( !RASTER || !RASTER.renderGray || !pre.rows || !pre.pitch )
    return { width: pre.width, rows: pre.rows, pitch: pre.width,
             buffer: null, error: 0 };

  const o = slot.outline;
  const x_shift = 64 * -pre.left;
  const y_shift = 64 * -pre.top + 64 * pre.rows;

  if ( x_shift || y_shift )
    for ( let i = 0; i < o.n_points; i++ ) { o.x[i] += x_shift; o.y[i] += y_shift; }

  let buffer = null, error = 0;
  try {
    buffer = RASTER.renderGray( o, pre.width, pre.rows );
  } catch ( e ) {
    buffer = null;
    error  = e.ftError ?? 0x62;
  }

  if ( x_shift || y_shift )
    for ( let i = 0; i < o.n_points; i++ ) { o.x[i] -= x_shift; o.y[i] -= y_shift; }

  return { width: pre.width, rows: pre.rows, pitch: pre.width, buffer, error };
}

/* ======================================================================
 *  FT_Outline_Check (base/ftoutln.c)
 * ==================================================================== */
function outlineCheck( o ) {
  const n_points = o.n_points, n_contours = o.n_contours;
  if ( n_points === 0 && n_contours === 0 ) return 0;
  if ( n_points === 0 || n_contours === 0 ) return 0x14;

  let end0 = -1;
  for ( let n = 0; n < n_contours; n++ ) {
    const end = o.contours[n];
    if ( end <= end0 || end >= n_points ) return 0x14;
    end0 = end;
  }
  if ( end0 !== n_points - 1 ) return 0x14;
  return 0;
}

/* ======================================================================
 *  getopt (GNU-style, permuting)
 * ==================================================================== */
function getopt( argv, optstring ) {
  /* returns { opts: [[ch, arg], ...], operands: [...] } */
  const opts = [], operands = [];
  let i = 0;
  while ( i < argv.length ) {
    const a = argv[i];
    if ( a === '--' ) { i++; break; }
    if ( a.length > 1 && a[0] === '-' ) {
      let j = 1;
      while ( j < a.length ) {
        const ch = a[j];
        const k = optstring.indexOf( ch );
        if ( k < 0 ) { opts.push( [ '?', null ] ); j++; continue; }
        if ( optstring[k + 1] === ':' ) {
          if ( j + 1 < a.length ) { opts.push( [ ch, a.slice( j + 1 ) ] ); j = a.length; }
          else { opts.push( [ ch, argv[++i] ?? null ] ); j = a.length; }
        } else { opts.push( [ ch, null ] ); j++; }
      }
      i++;
    } else {
      operands.push( a );
      i++;
    }
  }
  for ( ; i < argv.length; i++ ) operands.push( argv[i] );
  return { opts, operands };
}

/* C strtol(s, NULL, 16) */
function strtol16( s ) {
  if ( s == null ) return 0;
  const m = /^\s*([+-]?)(?:0[xX])?([0-9a-fA-F]*)/.exec( s );
  if ( !m || m[2] === '' ) return 0;
  let v = parseInt( m[2], 16 );
  if ( !Number.isFinite( v ) ) v = 0x7FFFFFFF;
  return m[1] === '-' ? -v : v;
}

/* C atoi */
function atoi( s ) {
  if ( s == null ) return 0;
  const m = /^\s*([+-]?\d+)/.exec( s );
  return m ? parseInt( m[1], 10 ) : 0;
}

/* C sscanf(optarg, "%u%*[,:-]%u", &fi, &li) */
function scanRange( s ) {
  const m = /^\s*(\d+)([,:\-]+)(\d+)?/.exec( s ?? '' );
  if ( m && m[3] !== undefined ) return [ 2, +m[1], +m[3] ];
  const m1 = /^\s*(\d+)/.exec( s ?? '' );
  if ( m1 ) return [ 1, +m1[1], 0 ];
  return [ 0, 0, 0 ];
}

function Usage( name ) {
  process.stderr.write(
    '\n' +
    'ftlint: simple font tester -- part of the FreeType project\n' +
    '----------------------------------------------------------\n' +
    '\n' +
    `Usage: ${name} [options] ppem fontname [fontname2..]\n` );
  process.stderr.write(
    '\n' +
    "  -f L    Use hex number L as load flags (see `FT_LOAD_XXX')\n" +
    '  -r N    Set render mode to N\n' +
    '  -i I-J  Range of glyph indices to use (default: all)\n' +
    '  -q      Quiet mode without the rendering analysis\n' +
    '\n' );
  flush();
  process.exit( 1 );
}

function Error_( msg, error ) {
  W( `${msg}error = 0x${padL( ( error >>> 0 ).toString( 16 ), 4 ).replace( / /g, '0' )}, ` +
     `${ERRSTR[error] ?? 'unknown error'}\n` );
}

/* ======================================================================
 *  main
 * ==================================================================== */
const HERE = path.dirname( fileURLToPath( import.meta.url ) );
const REPO = path.resolve( HERE, '..', '..' );

/* The container mounts the repository at /work.  ftlint prints argv[i]
 * verbatim (`printf( "%s:\n", fname )`), so we keep the name as given and
 * only redirect the OPEN when a /work path is used from the host. */
function resolveForOpen( fname ) {
  if ( fs.existsSync( fname ) ) return fname;
  if ( fname.startsWith( '/work/' ) ) {
    const alt = path.join( REPO, fname.slice( 6 ) );
    if ( fs.existsSync( alt ) ) return alt;
  }
  return fname;
}

function main() {
  const execname = 'ftlint';
  const { opts, operands } = getopt( process.argv.slice( 2 ), 'f:r:i:q' );

  let render_mode = 0;                 /* FT_RENDER_MODE_NORMAL */
  let load_flags  = 0;                 /* FT_LOAD_DEFAULT */
  let first_index = 0;
  let last_index  = 0xFFFFFFFF;        /* UINT_MAX */
  let quiet = 0;

  for ( const [ ch, arg ] of opts ) {
    switch ( ch ) {
    case 'f': load_flags = strtol16( arg ); break;
    case 'r': {
      const rm = atoi( arg );
      render_mode = ( rm < 0 || rm >= FT_RENDER_MODE_MAX ) ? 0 : rm;
      break;
    }
    case 'i': {
      const [ j, fi, li ] = scanRange( arg );
      if ( j === 2 ) {
        first_index = fi;
        last_index  = li >= fi ? li : 0xFFFFFFFF;
      } else if ( j === 1 ) {
        first_index = last_index = fi;
      }
      break;
    }
    case 'q': quiet = 1; break;
    default: Usage( execname );
    }
  }

  const argv = operands;
  if ( argv.length < 2 || !/^\s*[+-]?\d/.test( argv[0] ) )
    Usage( execname );

  const ptsize = atoi( argv[0] );

  /* sync target and mode */
  load_flags |= ( render_mode & 15 ) << 16;
  render_mode = ( load_flags & 0xF0000 ) >> 16;

  let face_index = 0;

  for ( let file_index = 1; file_index < argv.length; file_index++ ) {
    const fname = argv[file_index];

    W( `${fname}:\n` );

    let data;
    try {
      data = new Uint8Array( fs.readFileSync( resolveForOpen( fname ) ) );
    } catch {
      Error_( '  opening ', 0x01 /* cannot open resource */ );
      continue;
    }

    for ( ;; ) {                       /* Next_Face: */
      let face;
      try {
        face = openFace( data, face_index );
      } catch ( e ) {
        Error_( '  opening ', e.code ?? 0x02 );
        break;
      }

      /* cff_face_init parses the whole CFF at FT_New_Face time, so strictly
       * a broken `CFF ' is an OPENING error.  We only do that for a PURE CFF
       * face, where num_glyphs/unitsPerEM/the names are unobtainable without
       * it: on fuzzed SFNT-wrapped fonts cff.js rejects several files that
       * FreeType still opens (it only fails per glyph), and refusing the face
       * would lose a font FreeType loads -- the worse error of the two. */
      /* sfnt.js defers "this face has no outline source" so that shape-only
       * faces stay openable; ftlint must still report it exactly where
       * FT_New_Face would have failed. */
      if ( face.outlineError ) {
        Error_( '  opening ', face.outlineError );
        break;
      }

      if ( face.pureCFF ) {
        try {
          completeCffFace( face );
        } catch ( e ) {
          Error_( '  opening ', e.code ?? 0x02 /* Unknown_File_Format */ );
          break;
        }
      } else if ( face.isCFF && CFF && CFF.getCffFont &&
                  ( face.tables.has( 'CFF ' ) || face.tables.has( 'CFF2' ) ) ) {
        /* cff_face_init parses the CFF at FT_New_Face time, so a `CFF '-
         * flavoured `OTTO' whose CFF will not parse is refused before any
         * glyph is touched.  Both flavours are probed now that cff.js reads
         * CFF2: measured over every CFF2 face in the corpora, it parses the
         * five FreeType loads and throws on exactly the three it refuses, so
         * the probe stays narrow (load verdicts remain 65/65, not 60/65). */
        try {
          CFF.getCffFont( face );
        } catch ( e ) {
          Error_( '  opening ', cffOpenError( e ) );
          break;
        }
      }

      const fam = face.familyName === null ? '(null)' : face.familyName;
      const sty = face.styleName  === null ? '(null)' : face.styleName;

      W( `  ${fam} ${sty}, ${ptsize} ppem, ` +
         padL( ( load_flags >>> 0 ).toString( 16 ).toUpperCase(), 8 ).replace( / /g, '0' ) +
         `, ${modes[render_mode] ?? 'normal'}${quiet ? ':' : '\n'}` );

      const size = requestCharSize( face, ptsize );

      if ( face.numGlyphs ) {
        const fi = first_index > 0 ? first_index : 0;
        const li = last_index < face.numGlyphs ? last_index : face.numGlyphs - 1;

        if ( !quiet ) {
          W( '\n GID  shape X+Yturn imgsize  Xacut  Yacut  MD5 hashsum' );
          W( '\n-------------------------------------------------------------------\n' );
        }

        let Fail = 0;
        for ( let id = fi; id <= li; id++ ) {
          let slot;
          try {
            slot = loadGlyph( face, id, load_flags, size );
          } catch ( e ) {
            if ( !quiet ) {
              W( padL( String( id ), 5 ) + ' ' );
              Error_( 'loading ', e.code ?? 0x14 );
            }
            Fail++;
            continue;
          }
          if ( quiet ) continue;

          W( padL( String( id ), 5 ) + ' ' );
          W( Examine( slot ) );
          W( Explore( slot ) );

          const bmp = renderGlyph( slot, size );

          /* C: if ( error && face->glyph->format != FT_GLYPH_FORMAT_BITMAP ) */
          if ( bmp.error && slot.format !== FT_GLYPH_FORMAT_BITMAP ) {
            Error_( 'rendering ', bmp.error );
            Fail++;
            continue;
          }

          W( padL( String( bmp.width ), 3 ) + 'x' + padR( String( bmp.rows ), 4 ) + ' ' );
          W( Analyze( bmp ) );
          W( Checksum( bmp ) );
          W( '\n' );
        }

        if ( Fail === 0 )      W( '  OK.\n' );
        else if ( Fail === 1 ) W( '  1 fail.\n' );
        else                   W( `  ${Fail} fails.\n` );
      }

      if ( ++face_index === face.numFaces ) face_index = 0;
      if ( !face_index ) break;
    }
  }

  flush();
  process.exit( 0 );
}

/* FT_Load_Glyph dispatch */
function loadGlyph( face, gid, load_flags, size ) {
  /* ttdriver.c tt_glyph_load: FT_LOAD_NO_SCALE forces FT_LOAD_NO_HINTING. */
  const scaled = !( load_flags & FT_LOAD_NO_SCALE );
  const hinted = !( load_flags & ( FT_LOAD_NO_HINTING | FT_LOAD_NO_SCALE ) );

  let r;
  if ( face.isCFF ) {
    r = loadCffGlyph( face, gid, size, scaled );
  } else if ( hinted ) {
    /* ttdriver.c: with hinting the size metrics are `ttsize->hinted_metrics',
     * which is where tt_size_reset's `head.Flags & 8' integer-ppem rederivation
     * lands -- NOT the base metrics the unhinted path uses. */
    const hm = size.hinted;

    let done = false;
    if ( TTHINT && TTHINT.loadGlyfOutlineHinted ) {
      try {
        r = TTHINT.loadGlyfOutlineHinted( face, gid, size );
        done = true;
      } catch {
        done = false;   /* W4's module is still a checkpoint: degrade below */
      }
    }

    /* Graceful degradation while tthint.js is incomplete: load unhinted but
     * with the HINTED scales.  For a glyph with no instructions that is what
     * FreeType itself produces, because TT_Hint_Glyph with n_ins == 0 only
     * rounds the phantom points. */
    if ( !done )
      r = loadGlyfOutlineScaled( face, gid, hm.x_scale, hm.y_scale, hm.y_ppem,
                                 true /* isHinted */ );
  } else {
    r = loadGlyfOutlineScaled( face, gid, size.x_scale, size.y_scale, size.y_ppem );
  }

  const err = outlineCheck( r.outline );
  if ( err ) throw Object.assign( new Error( 'bad outline' ), { code: err } );

  return { format: FT_GLYPH_FORMAT_OUTLINE, outline: r.outline, metrics: r.metrics };
}

/**
 * cff_face_init's own work: load the CFF font, and for a PURE CFF face (an
 * `OTTO' file with no usable `head' table) take num_faces, num_glyphs,
 * units_per_EM and the family/style names from the CFF instead of the SFNT.
 */
function completeCffFace( face ) {
  if ( !CFF || !CFF.getCffFont )
    throw Object.assign( new Error( 'cff.js missing' ), { code: 0x13 } );

  const cff = CFF.getCffFont( face );        /* throws on a broken CFF */

  if ( !face.pureCFF )
    return;

  const dict = cff.top_font.font_dict;

  /* cffobjs.c: num_faces = cff->num_faces = name_index.count */
  face.numFaces   = cff.name_index.count || 1;
  face.numGlyphs  = cff.numGlyphs;
  face.unitsPerEM = dict.units_per_em;
  face.maxpFields = { numGlyphs: cff.numGlyphs };

  /* family: the SID `family_name' if present, else cff_index_get_name() --
   * the font's entry in the CFF Name INDEX -- with a subset prefix removed. */
  let family = cffIndexGetName( cff, face.faceIndex & 0xFFFF );
  if ( family !== null )
    family = removeSubsetPrefix( family );

  face.familyName = family;
  /* cffobjs.c derives the style from the `FullName' SID and otherwise falls
   * back to "Regular"; resolving a SID needs cff.js's string INDEX reader,
   * which it does not export, so we use FreeType's documented default. */
  face.styleName = family ? 'Regular' : null;
}

/* cff.js reports parse failures as plain Errors carrying no FreeType code, so
 * map its message onto the code cff_face_init / cff_font_load would return:
 *   - a bad CFF/CFF2 *header* never reaches cff_font_load; cff_face_init ends
 *     at Unknown_File_Format (0x02);
 *   - a missing FDArray fails in the index layer  -> Invalid_Table (0x08);
 *   - every other cff_font_load failure           -> Invalid_File_Format (0x03).
 * The default refuses the face, which is the correct decision even for a
 * message this table does not know.
 * TODO(W3): cff.js attaching `ftError' to its throws would remove this seam. */
function cffOpenError( e ) {
  const m = String( ( e && e.message ) || '' );
  if ( m.includes( 'font header' ) ) return 0x02 /* Unknown_File_Format  */;
  if ( m.includes( 'FDArray' ) )     return 0x08 /* Invalid_Table        */;
  return 0x03 /* Invalid_File_Format */;
}

/* cff_index_get_name(): element `n' of the CFF Name INDEX, as latin-1. */
function cffIndexGetName( cff, n ) {
  const idx = cff.name_index;
  const d   = cff.data;
  if ( !idx || idx.count <= n ) return null;

  const rd = ( at, sz ) => {
    let v = 0;
    for ( let i = 0; i < sz; i++ ) v = ( v * 256 ) + d[at + i];
    return v;
  };
  const o1 = rd( idx.start + idx.hdrSize + n * idx.offSize, idx.offSize );
  const o2 = rd( idx.start + idx.hdrSize + ( n + 1 ) * idx.offSize, idx.offSize );
  if ( !o1 || o2 <= o1 ) return null;

  let s = '';
  for ( let i = idx.dataOffset + o1 - 1; i < idx.dataOffset + o2 - 1; i++ )
    s += String.fromCharCode( d[i] );
  return s;
}

/* cffobjs.c remove_subset_prefix(): strip a leading "ABCDEF+". */
function removeSubsetPrefix( name ) {
  let idx = 0, i = 0;
  for ( ;; ) {
    let k = 0;
    while ( k < 6 && name[i + k] >= 'A' && name[i + k] <= 'Z' ) k++;
    if ( k < 6 || name[i + 6] !== '+' ) break;
    i += 7;
    idx = i;
  }
  return idx ? name.slice( idx ) : name;
}

/**
 * The tail of cff_slot_load (../freetype/src/cff/cffgload.c) that W3's
 * cff.js deliberately stops short of: it returns the outline in FONT UNITS
 * with the FontMatrix and font offset already applied, and leaves the
 * `vec = FT_MulFix( vec, x_scale )' loop to us.
 */
function loadCffGlyph( face, gid, size, scaled ) {
  if ( !CFF || !CFF.loadCffOutline )
    throw Object.assign( new Error( 'cff.js missing' ),
                         { code: 0x13 /* Cannot_Render_Glyph */ } );

  const r = CFF.loadCffOutline( face, gid );
  if ( r.error )
    /* cff.js numbers its charstring errors privately; FreeType surfaces every
     * one of them out of the cf2 interpreter as Invalid_File_Format. */
    throw Object.assign( new Error( 'cff charstring' ), { code: 0x03 } );

  const outline = r.outline;

  /* glyph->x_scale / glyph->y_scale */
  let x_scale = scaled ? size.x_scale : 0x10000;
  let y_scale = scaled ? size.y_scale : 0x10000;

  /* CID subfont whose FDArray unitsPerEm differs from the top dict's:
   *   glyph->x_scale = FT_MulDiv( glyph->x_scale, top_upm, sub_upm );
   *   force_scaling  = TRUE;                                              */
  let force_scaling = false;
  if ( r.forceScaling ) {
    x_scale = FT_MulDiv( x_scale, r.topUpm, r.subUpm );
    y_scale = FT_MulDiv( y_scale, r.topUpm, r.subUpm );
    force_scaling = true;
  }

  /* glyph->root.outline.flags = FT_OUTLINE_REVERSE_FILL (set by cff.js);
   *   if ( size && size->root.metrics.y_ppem < 24 )
   *     flags |= FT_OUTLINE_HIGH_PRECISION;                               */
  outline.flags = FT_OUTLINE_REVERSE_FILL;
  if ( size.y_ppem < 24 )
    outline.flags |= FT_OUTLINE_HIGH_PRECISION;

  /* metrics: from `hmtx' when the face has any, else the charstring width.
   * ftlint prints none of these, but the slot should still be right. */
  const metrics = { horiAdvance: 0, horiBearingX: 0, horiBearingY: 0,
                    vertAdvance: 0, vertBearingX: 0, vertBearingY: 0,
                    width: 0, height: 0 };

  if ( face.hhea.number_Of_HMetrics ) {
    const h = face.hmtx( gid );
    metrics.horiAdvance  = h.advance;
    metrics.horiBearingX = h.lsb;
  } else {
    metrics.horiAdvance = r.cffWidth;
  }

  const has_vertical_info = !!( face.verticalInfo && face.vhea &&
                                face.vhea.number_Of_HMetrics > 0 );
  if ( has_vertical_info ) {
    const v = face.vmtx( gid );
    metrics.vertBearingY = v.lsb;
    metrics.vertAdvance  = v.advance;
  } else if ( face.os2 && face.os2.version !== 0xFFFF ) {
    metrics.vertAdvance = face.os2.sTypoAscender - face.os2.sTypoDescender;
  } else {
    metrics.vertAdvance = face.hhea.Ascender - face.hhea.Descender;
  }

  /* if ( scaled || force_scaling ) -- scale the outline, then the metrics. */
  if ( scaled || force_scaling ) {
    const X = outline.x, Y = outline.y;
    for ( let n = 0; n < outline.n_points; n++ ) {
      X[n] = FT_MulFix( X[n], x_scale );
      Y[n] = FT_MulFix( Y[n], y_scale );
    }
    metrics.horiAdvance = FT_MulFix( metrics.horiAdvance, x_scale );
    metrics.vertAdvance = FT_MulFix( metrics.vertAdvance, y_scale );
  }

  /* compute the other metrics */
  const cbox = FT_Outline_Get_CBox( outline );
  metrics.width        = cbox.xMax - cbox.xMin;
  metrics.height       = cbox.yMax - cbox.yMin;
  metrics.horiBearingX = cbox.xMin;
  metrics.horiBearingY = cbox.yMax;

  if ( has_vertical_info ) {
    metrics.vertBearingX = metrics.horiBearingX -
                           Math.trunc( metrics.horiAdvance / 2 );
    metrics.vertBearingY = FT_MulFix( metrics.vertBearingY, y_scale );
  }

  return { outline, advance: metrics.horiAdvance, metrics };
}

main();
