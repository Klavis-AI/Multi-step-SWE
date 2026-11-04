/* shaper-khmer.js -- the Khmer shaper.
 *
 * Implemented from ../otspec/txt/sd-khmer.txt.  Khmer is close enough to the
 * Indic model to share its character table (shaper-indic.js) and far enough
 * from it to need its own cluster grammar and its own reordering: there is no
 * base-consonant search and no reph, and the two things that DO move -- a
 * COENG + RO pair and a left matra -- move to the head of the cluster and
 * nowhere else.
 *
 * Section references are line numbers in sd-khmer.txt.
 *
 * WHAT THIS REPLACES.  Khmer used to be shaped by the generic path with a
 * partial hand-written pass in shaper-misc.js whose own comment recorded it
 * as incomplete (subscript type 3 and defective clusters).  The gap was not a
 * missing rule but a missing MODEL: without a cluster grammar there is no
 * BROKEN verdict, so no dotted circle; and without the syllable boundaries a
 * grammar produces, the per-cluster features (l.145-171 applies pref, blwf,
 * abvf, pstf and cfar "one cluster at a time") had nothing to be scoped to.
 */

import { OT, otCategory, gLit as L, gSeq as SEQ, gAlt as ALT, gStar as STAR,
         gOpt as OPT, gAny, makeScanner, insertDottedCircles, forEachSyllable,
         featureMask } from './shaper-indic.js';
import { mergeClusterRange, SCRATCH_HAS_BROKEN_SYLLABLE } from './buffer.js';

/* Mirrors shape-plan.js's F_* (no import: shape-plan.js imports us). */
const F_NONE = 0, F_MANUAL_ZWNJ = 4, F_MANUAL_ZWJ = 8, F_PER_SYLLABLE = 64;
const F_MANUAL_JOINERS = F_MANUAL_ZWNJ | F_MANUAL_ZWJ;

const SYLL = { CONSONANT: 0, BROKEN: 1, NON_KHMER: 2 };


/* ------------------------------------------------------------------ */
/*  The cluster grammar -- sd-khmer.txt l.101-143                       */
/* ------------------------------------------------------------------ */

/* The Khmer signs split into three groups by what may follow them: the
 * "Robatic" signs (U+17CC, U+17C9, U+17CA), which may open a cluster; the
 * X group (U+17C6, U+17CB, U+17CD..U+17D1), which is transparent between the
 * matra pieces; and the Y group (U+17C7, U+17C8, U+17D3, U+17DD), which may
 * only trail.  shaper-indic.js's table carries the assignment. */
const g_c = ALT( L( OT.C ), L( OT.Ra ), L( OT.V ) );
const g_joiner = ALT( L( OT.ZWJ ), L( OT.ZWNJ ) );
const g_cn = SEQ( g_c, OPT( SEQ( OPT( g_joiner ), L( OT.Robatic ) ) ) );
const g_xgroup = STAR( SEQ( STAR( g_joiner ), L( OT.Xgroup ) ) );
const g_ygroup = STAR( L( OT.Ygroup ) );
const g_matra_group = SEQ( OPT( L( OT.VPre ) ), g_xgroup, OPT( L( OT.VBlw ) ),
                           g_xgroup, OPT( SEQ( OPT( g_joiner ), L( OT.VAbv ) ) ),
                           g_xgroup, OPT( L( OT.VPst ) ) );
const g_syllable_tail = SEQ( g_xgroup, g_matra_group, g_xgroup,
                             OPT( SEQ( L( OT.H ), g_c ) ), g_ygroup );
const g_broken_cluster = SEQ( OPT( L( OT.Robatic ) ),
                              STAR( SEQ( L( OT.H ), g_cn ) ),
                              ALT( L( OT.H ), g_syllable_tail ) );
const g_consonant_syllable = SEQ( ALT( g_cn, L( OT.PLACEHOLDER ),
                                       L( OT.DOTTEDCIRCLE ) ),
                                  g_broken_cluster );

const scanKhmer = makeScanner( [
  { f: g_consonant_syllable, t: SYLL.CONSONANT },
  { f: g_broken_cluster, t: SYLL.BROKEN },
  { f: gAny(), t: SYLL.NON_KHMER },
] );

function findSyllables( buffer ) {
  const info = buffer.info;
  const n = Math.min( info.length, buffer.len === undefined ? info.length
                                                           : Math.max( buffer.len, 0 ) ) || info.length;
  if ( !n ) return;
  const cats = new Int16Array( n );
  for ( let i = 0; i < n; i++ )
    cats[ i ] = info[ i ].khmerCategory === undefined ? OT.X : info[ i ].khmerCategory;
  let serial = 1;
  let i = 0;
  while ( i < n ) {
    const hit = scanKhmer( cats, i, n, SYLL.NON_KHMER );
    const val = ( ( serial << 4 ) | hit.type ) & 0xFF;
    for ( let k = i; k < i + hit.len; k++ ) info[ k ].syllable = val;
    if ( hit.type === SYLL.BROKEN && buffer.scratchFlags !== undefined )
      buffer.scratchFlags |= SCRATCH_HAS_BROKEN_SYLLABLE;
    serial++;
    if ( serial === 16 ) serial = 1;
    i += hit.len;
  }
}


/* ------------------------------------------------------------------ */
/*  Features -- sd-khmer.txt l.145-215                                  */
/* ------------------------------------------------------------------ */

/* Applied all at once before reordering, one cluster at a time. */
const BASIC_FEATURES = [ 'pref', 'blwf', 'abvf', 'pstf', 'cfar' ];
/* Applied all at once after the syllables are cleared. */
const OTHER_FEATURES = [ 'pres', 'abvs', 'blws', 'psts' ];

function collectFeatures( b ) {
  /* Do this before any lookups have been applied. */
  b.addGsubPause( setupSyllables );
  b.addGsubPause( reorderKhmer );

  /* Uniscribe does NOT pause between the basic features, which is why they
   * are one stage and not five: with a pause after each, a font whose 'blwf'
   * output is the input of its 'pstf' shapes U+1789,U+17D2,U+1789,U+17BC
   * differently from the reference. */
  b.enableFeature( 'locl', F_PER_SYLLABLE );
  b.enableFeature( 'ccmp', F_PER_SYLLABLE );

  for ( const t of BASIC_FEATURES )
    b.addFeature( t, F_MANUAL_JOINERS | F_PER_SYLLABLE );

  b.addGsubPause( null );          /* syllables are not needed after this */

  for ( const t of OTHER_FEATURES )
    b.enableFeature( t, F_MANUAL_JOINERS );
}

/* l.209: "Apply feature 'clig' to form ligatures that are desired for
 * typographical correctness", and 'liga' is NOT in the Khmer feature list, so
 * the generic collector's global 'liga' has to be switched off again. */
function overrideFeatures( b ) {
  b.enableFeature( 'clig' );
  b.enableFeature( 'liga', F_NONE, 0 );
}

function setupMasks( plan, buffer /* , font */ ) {
  const info = buffer.info;
  const n = Math.min( info.length, buffer.len === undefined ? info.length
                                                           : Math.max( buffer.len, 0 ) ) || info.length;
  for ( let i = 0; i < n; i++ )
    info[ i ].khmerCategory = otCategory( info[ i ].codepoint );
}

function setupSyllables( plan, buffer /* , font */ ) {
  findSyllables( buffer );
  if ( typeof buffer.unsafeToBreak === 'function' )
    forEachSyllable( buffer, ( s, e ) => buffer.unsafeToBreak( s, e ) );
}


/* ------------------------------------------------------------------ */
/*  Reordering -- sd-khmer.txt l.155-181                                */
/* ------------------------------------------------------------------ */

function reorderConsonantSyllable( plan, buffer, start, end ) {
  const info = buffer.info;
  const mBlwf = featureMask( plan, 'blwf' );
  const mAbvf = featureMask( plan, 'abvf' );
  const mPstf = featureMask( plan, 'pstf' );
  const mPref = featureMask( plan, 'pref' );
  const mCfar = featureMask( plan, 'cfar' );

  /* Every glyph after the base is a candidate for the post-base forms; which
   * of the three actually applies is the font's business, not ours. */
  const post = ( mBlwf | mAbvf | mPstf ) >>> 0;
  for ( let i = start + 1; i < end; i++ )
    info[ i ].mask = ( info[ i ].mask | post ) >>> 0;

  let numCoengs = 0;
  for ( let i = start + 1; i < end; i++ ) {
    /* l.161-165: "When a COENG + (Cons | IndV) combination are found (and
     * subscript count is less than two) the character combination is handled
     * according to the subscript type of the character following the COENG.
     * ... Subscript Type 2 -- The COENG + RO characters are reordered to
     * immediately before the base glyph.  Then the COENG + RO characters are
     * assigned to have the 'pref' OpenType feature applied to them." */
    if ( info[ i ].khmerCategory === OT.H && numCoengs <= 2 && i + 1 < end ) {
      numCoengs++;
      if ( info[ i + 1 ].khmerCategory === OT.Ra ) {
        if ( mPref ) {
          info[ i ].mask = ( info[ i ].mask | mPref ) >>> 0;
          info[ i + 1 ].mask = ( info[ i + 1 ].mask | mPref ) >>> 0;
        }
        /* Move the COENG, RO pair to the start of the cluster. */
        mergeClusterRange( buffer, start, i + 2 );
        const t0 = info[ i ], t1 = info[ i + 1 ];
        for ( let k = i + 1; k >= start + 2; k-- ) info[ k ] = info[ k - 2 ];
        info[ start ] = t0;
        info[ start + 1 ] = t1;

        /* 'cfar' marks what FOLLOWS the reordered pair, which is the only
         * thing that tells  U+1784,U+17D2,U+179A,U+17D2,U+1782  from
         * U+1784,U+17D2,U+1782,U+17D2,U+179A  once both have a pre-base RO. */
        if ( mCfar )
          for ( let k = i + 2; k < end; k++ )
            info[ k ].mask = ( info[ k ].mask | mCfar ) >>> 0;

        numCoengs = 2;                       /* done */
      }
    } else if ( info[ i ].khmerCategory === OT.VPre ) {
      /* l.171: the left matra moves to the start of the cluster. */
      mergeClusterRange( buffer, start, i + 1 );
      const t = info[ i ];
      for ( let k = i; k > start; k-- ) info[ k ] = info[ k - 1 ];
      info[ start ] = t;
    }
  }
}

function reorderKhmer( plan, buffer, font ) {
  if ( buffer.scratchFlags === undefined ||
       ( buffer.scratchFlags & SCRATCH_HAS_BROKEN_SYLLABLE ) )
    insertDottedCircles( buffer, font, SYLL.BROKEN, OT.DOTTEDCIRCLE, -1, -1,
                         'khmerCategory', null );
  forEachSyllable( buffer, ( start, end ) => {
    const t = buffer.info[ start ].syllable & 0x0F;
    /* A broken cluster has just been given its dotted circle, so it reorders
     * exactly like the consonant syllable it now is. */
    if ( t === SYLL.CONSONANT || t === SYLL.BROKEN )
      reorderConsonantSyllable( plan, buffer, start, end );
  } );
}


/* ------------------------------------------------------------------ */
/*  Normalization                                                       */
/* ------------------------------------------------------------------ */

/* l.149: the two-part matras have no Unicode decomposition, so the shaper
 * supplies one.  Each is <U+17C1, itself>: the left half is a real character
 * that the cluster grammar then sees as VPre and reorders. */
const SPLIT_MATRA = {
  0x17BE: 0x17C1, 0x17BF: 0x17C1, 0x17C0: 0x17C1,
  0x17C4: 0x17C1, 0x17C5: 0x17C1,
};
function decompose( shaper, ab ) {
  const a = SPLIT_MATRA[ ab ];
  return a ? [ a, ab ] : null;
}


export const khmerShaper = {
  name: 'khmer',
  collectFeatures,
  overrideFeatures,
  setupMasks,
  decompose,
  /* Never short-circuit the normalizer: a precomposed matra the font has a
   * glyph for must still be split, or the halves never reach the reorderer. */
  normalizationPreference: 'no-short-circuit',
  zeroWidthMarks: 'none',
  fallbackPosition: false,
  gposTag: null,
};

/* W19_NO_KHMER routes Khmer back to whatever shaper the module list offers
 * next, so this engine's contribution stays measurable in a tree that several
 * people are editing at once. */
export function shaperForScript( scriptTag ) {
  if ( process.env.W19_NO_KHMER ) return null;
  return scriptTag === 'Khmr' ? khmerShaper : null;
}

export default khmerShaper;
