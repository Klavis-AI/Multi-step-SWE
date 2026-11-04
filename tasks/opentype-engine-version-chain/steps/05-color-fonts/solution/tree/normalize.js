/* normalize.js -- canonical decomposition and composition (UAX #15), plus the
 * normalization policy the OpenType shaping model applies before GSUB.
 *
 * Two layers live here:
 *
 *   1. Plain Unicode normalization.  nfd()/nfc() over the canonical
 *      decomposition table in unicode.js, the Full_Composition_Exclusion set,
 *      canonical ordering by combining class, and the UAX #15 blocking rule.
 *      Hangul is algorithmic (UAX #15, "Hangul Syllable Decomposition").
 *
 *   2. otNormalize(), the shaper's preference: decompose, let the font's
 *      layout run, then recompose only where the font actually has a glyph.
 *      Every rule below was measured against hb-shape 14.4.0 rather than
 *      copied; NOTES-S2.md records the probes.  In summary:
 *
 *        - the buffer is walked in groups of one base plus the marks that
 *          follow it (a leading mark is a group of its own);
 *        - if EVERY group is a single character the recompose pass is skipped
 *          altogether, so a lone precomposed character that was decomposed
 *          stays decomposed.  The flag is global, not per group;
 *        - a single-character group may "short-circuit": if the font has a
 *          glyph for the character as written it is kept undecomposed.  The
 *          default, Arabic and Hebrew shapers do this; the Indic shaper does
 *          not (mode COMPOSED_DIACRITICS_NO_SHORT_CIRCUIT);
 *        - multi-character groups always decompose fully before recomposing;
 *        - composition honours the composition exclusions, never recomposes a
 *          singleton decomposition, and follows the full blocking rule;
 *        - the Hebrew shaper is the one exception: it composes the 34
 *          canonical FB1D..FB4E presentation forms even though all of them are
 *          Full_Composition_Exclusion.  Pass composeHebrew as ctx.compose.
 */

import {
  canonicalDecompose, decompositionCount, decompositionAt,
  isCompositionExcluded, combiningClass, isMark,
} from './unicode.js';

/* ------------------------------------------------------------------ *
 * Hangul -- algorithmic, so it is not in the table
 * ------------------------------------------------------------------ */

var SBase = 0xAC00, LBase = 0x1100, VBase = 0x1161, TBase = 0x11A7;
var LCount = 19, VCount = 21, TCount = 28;
var NCount = VCount * TCount;      /* 588   */
var SCount = LCount * NCount;      /* 11172 */

/* One canonical step: LVT -> <LV, T>, LV -> <L, V>. */
export function hangulDecompose( s ) {
  var i = s - SBase;
  if ( i < 0 || i >= SCount ) return null;
  var t = i % TCount;
  if ( t ) return [ s - t, TBase + t ];
  return [ LBase + Math.floor( i / NCount ), VBase + Math.floor( ( i % NCount ) / TCount ) ];
}

export function hangulCompose( a, b ) {
  if ( a >= LBase && a < LBase + LCount && b >= VBase && b < VBase + VCount )
    return SBase + ( ( a - LBase ) * VCount + ( b - VBase ) ) * TCount;
  if ( a >= SBase && a < SBase + SCount && ( a - SBase ) % TCount === 0 &&
       b > TBase && b < TBase + TCount )
    return a + ( b - TBase );
  return 0;
}

/* ------------------------------------------------------------------ *
 * Canonical decomposition / composition
 * ------------------------------------------------------------------ */

/* One canonical decomposition step, or null.  [a] for a singleton. */
export function decomposeCanonical( cp ) {
  var h = hangulDecompose( cp );
  if ( h ) return h;
  return canonicalDecompose( cp );
}

var COMP = null;       /* "a,b -> ab", exclusions removed */
var COMP_ALL = null;   /* same, exclusions kept -- for the Hebrew hook */

function key( a, b ) { return a * 0x110000 + b; }

function buildMaps() {
  COMP = new Map();
  COMP_ALL = new Map();
  var n = decompositionCount();
  for ( var i = 0; i < n; i++ ) {
    var d = decompositionAt( i );          /* [cp, a] or [cp, a, b] */
    if ( d.length !== 3 ) continue;        /* singletons never recompose */
    var k = key( d[ 1 ], d[ 2 ] );
    COMP_ALL.set( k, d[ 0 ] );
    if ( !isCompositionExcluded( d[ 0 ] ) ) COMP.set( k, d[ 0 ] );
  }
}

/* The composed code point, or 0. */
export function composeCanonical( a, b ) {
  var h = hangulCompose( a, b );
  if ( h ) return h;
  if ( !COMP ) buildMaps();
  return COMP.get( key( a, b ) ) || 0;
}

/* The Hebrew shaper's compose hook: the FB1D..FB4F presentation forms are all
 * composition exclusions, yet the reference composes them (measured).  Nothing
 * else excluded is allowed through. */
export function composeHebrew( a, b ) {
  if ( !COMP ) buildMaps();
  var c = COMP_ALL.get( key( a, b ) );
  if ( c !== undefined && c >= 0xFB1D && c <= 0xFB4F ) return c;
  return composeCanonical( a, b );
}

/* ------------------------------------------------------------------ *
 * Whole-string NFD / NFC
 * ------------------------------------------------------------------ */

function decomposeInto( cp, out ) {
  var d = decomposeCanonical( cp );
  if ( !d ) { out.push( cp ); return; }
  decomposeInto( d[ 0 ], out );
  if ( d.length > 1 ) decomposeInto( d[ 1 ], out );
}

/* Stable sort by combining class within each run of non-starters (UAX #15). */
export function canonicalOrder( cps ) {
  for ( var i = 1; i < cps.length; i++ ) {
    var cc = combiningClass( cps[ i ] );
    if ( cc === 0 ) continue;
    var j = i;
    while ( j > 0 ) {
      var prev = combiningClass( cps[ j - 1 ] );
      if ( prev === 0 || prev <= cc ) break;
      var t = cps[ j ]; cps[ j ] = cps[ j - 1 ]; cps[ j - 1 ] = t;
      j--;
    }
  }
  return cps;
}

export function toNFD( cps ) {
  var out = [];
  for ( var i = 0; i < cps.length; i++ ) decomposeInto( cps[ i ], out );
  return canonicalOrder( out );
}

/* UAX #15 composition, including the blocking rule: cp is blocked from the
 * starter L when some character between them has ccc 0 or ccc >= ccc(cp). */
export function composeSequence( cps, compose ) {
  compose = compose || composeCanonical;
  if ( cps.length === 0 ) return cps;
  var out = [ cps[ 0 ] ];
  var starterIdx = combiningClass( cps[ 0 ] ) === 0 ? 0 : -1;
  var lastCC = combiningClass( cps[ 0 ] );
  for ( var i = 1; i < cps.length; i++ ) {
    var cp = cps[ i ], cc = combiningClass( cp ), blocked = true;
    if ( starterIdx >= 0 ) {
      if ( out.length - 1 === starterIdx ) blocked = false;   /* nothing between */
      else blocked = ( lastCC === 0 || lastCC >= cc );
    }
    if ( !blocked ) {
      var c = compose( out[ starterIdx ], cp );
      if ( c ) { out[ starterIdx ] = c; continue; }           /* lastCC unchanged */
    }
    out.push( cp );
    lastCC = cc;
    if ( cc === 0 ) starterIdx = out.length - 1;
  }
  return out;
}

export function toNFC( cps, compose ) { return composeSequence( toNFD( cps ), compose ); }

function toCps( s ) { var a = []; for ( var ch of s ) a.push( ch.codePointAt( 0 ) ); return a; }
function toStr( a ) { return a.map( function ( c ) { return String.fromCodePoint( c ); } ).join( '' ); }

export function nfd( s ) { return toStr( toNFD( toCps( s ) ) ); }
export function nfc( s ) { return toStr( toNFC( toCps( s ) ) ); }

/* ------------------------------------------------------------------ *
 * The shaper's normalization
 * ------------------------------------------------------------------ */

export var NORMALIZATION_MODE = {
  NONE: 0,
  DECOMPOSED: 1,
  COMPOSED_DIACRITICS: 2,                      /* default / Arabic / Hebrew */
  COMPOSED_DIACRITICS_NO_SHORT_CIRCUIT: 3,     /* Indic */
  AUTO: 4,
};

/* otNormalize( items, ctx )
 *
 *   items  [ { u, cluster } ]          input characters, logical order
 *   ctx    { mode,                     a NORMALIZATION_MODE
 *            glyphForChar( u ),        -> glyph id, or 0 when absent
 *            compose( a, b ),          optional shaper hook, default canonical
 *            decompose( cp ) }         optional shaper hook, one step
 *
 * returns [ { u, cluster, glyph } ] in logical order.  Cluster values are
 * carried through unchanged: merging clusters and flagging unsafe-to-break is
 * the buffer's job (S3), not the normalizer's.
 */
export function otNormalize( items, ctx ) {
  var mode = ctx.mode === undefined ? NORMALIZATION_MODE.AUTO : ctx.mode;
  if ( mode === NORMALIZATION_MODE.AUTO ) mode = NORMALIZATION_MODE.COMPOSED_DIACRITICS;
  var glyphFor = ctx.glyphForChar;
  var compose = ctx.compose || composeCanonical;
  var decompose1 = ctx.decompose || decomposeCanonical;

  /* Short-circuit means "keep a character the font already has".  NONE keeps
   * everything; DECOMPOSED and the no-short-circuit mode keep nothing. */
  var alwaysShortCircuit = mode === NORMALIZATION_MODE.NONE;
  var mightShortCircuit = alwaysShortCircuit ||
    ( mode !== NORMALIZATION_MODE.DECOMPOSED &&
      mode !== NORMALIZATION_MODE.COMPOSED_DIACRITICS_NO_SHORT_CIRCUIT );

  var out = [];
  var cluster = 0;
  function emit( u ) { out.push( { u: u, cluster: cluster, glyph: glyphFor( u ) } ); }

  /* Recursive decomposition of one character.  Returns false when the
   * decomposition cannot be used because the font lacks one of the pieces. */
  function decomposeRec( ab, shortest ) {
    var d = decompose1( ab );
    if ( !d ) return false;
    var a = d[ 0 ], b = d.length > 1 ? d[ 1 ] : 0;
    if ( b && !glyphFor( b ) ) return false;
    var aGlyph = glyphFor( a );
    if ( shortest && aGlyph ) { emit( a ); if ( b ) emit( b ); return true; }
    if ( decomposeRec( a, shortest ) ) { if ( b ) emit( b ); return true; }
    if ( aGlyph ) { emit( a ); if ( b ) emit( b ); return true; }
    return false;
  }

  function decomposeCurrent( u, shortest ) {
    var g = glyphFor( u );
    if ( shortest && g ) { emit( u ); return; }
    if ( decomposeRec( u, shortest ) ) return;
    if ( !shortest && g ) { emit( u ); return; }
    /* The space and non-breaking-hyphen fallbacks live in the shaper, which
     * knows the font metrics; leave the character in place for it. */
    emit( u );
  }

  /* --- pass 1: decompose, one base plus its following marks at a time --- */
  var allSimple = true;
  var i = 0, n = items.length;
  while ( i < n ) {
    var end = i + 1;
    while ( end < n && isMark( items[ end ].u ) ) end++;
    var len = end - i;
    if ( len === 1 ) {
      cluster = items[ i ].cluster;
      decomposeCurrent( items[ i ].u, mightShortCircuit );
    } else {
      for ( var k = i; k < end; k++ ) {
        cluster = items[ k ].cluster;
        decomposeCurrent( items[ k ].u, alwaysShortCircuit );
      }
      allSimple = false;
    }
    i = end;
  }

  /* --- pass 2: canonical ordering, in place, clusters travelling along --- */
  for ( var p = 1; p < out.length; p++ ) {
    var cc = combiningClass( out[ p ].u );
    if ( cc === 0 ) continue;
    var q = p;
    while ( q > 0 ) {
      var prev = combiningClass( out[ q - 1 ].u );
      if ( prev === 0 || prev <= cc ) break;
      var t = out[ q ]; out[ q ] = out[ q - 1 ]; out[ q - 1 ] = t;
      q--;
    }
  }

  /* --- pass 3: recompose, but only when some group was multi-character --- */
  var wantCompose = mode === NORMALIZATION_MODE.COMPOSED_DIACRITICS ||
                    mode === NORMALIZATION_MODE.COMPOSED_DIACRITICS_NO_SHORT_CIRCUIT;
  if ( allSimple || !wantCompose ) return out;

  var res = [ out[ 0 ] ];
  var starterIdx = combiningClass( out[ 0 ].u ) === 0 ? 0 : -1;
  var lastCC = combiningClass( out[ 0 ].u );
  for ( var j = 1; j < out.length; j++ ) {
    var it = out[ j ], c2 = combiningClass( it.u ), blocked = true;
    if ( starterIdx >= 0 ) {
      if ( res.length - 1 === starterIdx ) blocked = false;
      else blocked = ( lastCC === 0 || lastCC >= c2 );
    }
    if ( !blocked ) {
      var ab = compose( res[ starterIdx ].u, it.u );
      /* only if the font can actually draw the result */
      if ( ab ) {
        var g2 = glyphFor( ab );
        if ( g2 ) {
          res[ starterIdx ] = {
            u: ab, glyph: g2,
            cluster: Math.min( res[ starterIdx ].cluster, it.cluster ),
          };
          continue;
        }
      }
    }
    res.push( it );
    lastCC = c2;
    if ( c2 === 0 ) starterIdx = res.length - 1;
  }
  return res;
}
