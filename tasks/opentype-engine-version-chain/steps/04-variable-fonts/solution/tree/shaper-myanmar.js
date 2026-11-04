/* shaper-myanmar.js -- the Myanmar shaper.
 *
 * Implemented from ../otspec/txt/sd-myanmar.txt.  Section references below
 * are line numbers in that file.
 *
 * Myanmar is the one script in this milestone that reorders by SORTING: every
 * character in a cluster is given a visual slot (l.157-205 lists them, from
 * the pre-base medial RA through the base to the tones), and then the cluster
 * is stably sorted by slot.  That is why this shaper needs no base-finding
 * walk beyond locating the first consonant, and why the same routine serves
 * both a well-formed cluster and a broken one that has just been given a
 * dotted circle.
 *
 * This shaper did not exist before; Myanmar fell through to the default
 * path, which left its clusters unsorted and its defective clusters
 * undecorated.
 */

import { OT, OTPOS as POS, otCategory, gLit as L, gSeq as SEQ, gAlt as ALT, gStar as STAR,
         gOpt as OPT, gAny, makeScanner, insertDottedCircles, forEachSyllable,
         sortRange, reverseRange } from './shaper-indic.js';
import { SCRATCH_HAS_BROKEN_SYLLABLE } from './buffer.js';

/* Mirrors shape-plan.js's F_* (no import: shape-plan.js imports us). */
const F_MANUAL_ZWJ = 8, F_PER_SYLLABLE = 64;
const GP_LIGATED = 0x0020;

const SYLL = { CONSONANT: 0, BROKEN: 1, NON_MYANMAR: 2 };


/* ------------------------------------------------------------------ */
/*  The cluster grammar -- sd-myanmar.txt l.101-155                     */
/* ------------------------------------------------------------------ */

const g_j = ALT( L( OT.ZWJ ), L( OT.ZWNJ ) );
const g_k = SEQ( L( OT.Ra ), L( OT.As ), L( OT.H ) );          /* kinzi */
const g_sm = ALT( L( OT.SM ), L( OT.SMPst ) );
const g_c = ALT( L( OT.C ), L( OT.Ra ) );

const g_medial_group = SEQ(
  OPT( L( OT.MY ) ), OPT( L( OT.As ) ), OPT( L( OT.MR ) ),
  OPT( SEQ( ALT( SEQ( L( OT.MW ), OPT( L( OT.MH ) ), OPT( L( OT.ML ) ) ),
                 SEQ( L( OT.MH ), OPT( L( OT.ML ) ) ),
                 L( OT.ML ) ),
            OPT( L( OT.As ) ) ) ) );
const g_main_vowel_group = SEQ(
  STAR( SEQ( L( OT.VPre ), OPT( L( OT.VS ) ) ) ),
  STAR( L( OT.VAbv ) ), STAR( L( OT.VBlw ) ), STAR( L( OT.A ) ),
  OPT( SEQ( L( OT.DB ), OPT( L( OT.As ) ) ) ) );
const g_post_vowel_group = SEQ(
  L( OT.VPst ), OPT( L( OT.MH ) ), OPT( L( OT.ML ) ), STAR( L( OT.As ) ),
  STAR( L( OT.VAbv ) ), STAR( L( OT.A ) ),
  OPT( SEQ( L( OT.DB ), OPT( L( OT.As ) ) ) ) );
const g_tone_group = ALT( g_sm,
  SEQ( L( OT.PT ), STAR( L( OT.A ) ), OPT( L( OT.DB ) ), OPT( L( OT.As ) ) ) );

const g_complex_tail = SEQ( STAR( L( OT.As ) ), g_medial_group,
                            g_main_vowel_group, STAR( g_post_vowel_group ),
                            STAR( g_tone_group ), OPT( g_j ) );
const g_syllable_tail = SEQ(
  STAR( SEQ( L( OT.H ), ALT( g_c, L( OT.IV ) ), OPT( L( OT.VS ) ) ) ),
  ALT( L( OT.H ), g_complex_tail ) );

const g_consonant_syllable = SEQ(
  OPT( ALT( g_k, L( OT.CS ) ) ),
  ALT( g_c, L( OT.IV ), L( OT.GB ), L( OT.DOTTEDCIRCLE ) ),
  OPT( L( OT.VS ) ), g_syllable_tail );
const g_broken_cluster = SEQ( OPT( g_k ), OPT( L( OT.VS ) ), g_syllable_tail );

const scanMyanmar = makeScanner( [
  { f: g_consonant_syllable, t: SYLL.CONSONANT },
  { f: ALT( g_j, L( OT.SMPst ) ), t: SYLL.NON_MYANMAR },
  { f: g_broken_cluster, t: SYLL.BROKEN },
  { f: gAny(), t: SYLL.NON_MYANMAR },
] );

function findSyllables( buffer ) {
  const info = buffer.info;
  const n = Math.min( info.length, buffer.len === undefined ? info.length
                                                           : Math.max( buffer.len, 0 ) ) || info.length;
  if ( !n ) return;
  const cats = new Int16Array( n );
  for ( let i = 0; i < n; i++ )
    cats[ i ] = info[ i ].myanmarCategory === undefined ? OT.X : info[ i ].myanmarCategory;
  let serial = 1;
  let i = 0;
  while ( i < n ) {
    const hit = scanMyanmar( cats, i, n, SYLL.NON_MYANMAR );
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
/*  Features -- sd-myanmar.txt l.207-260                                */
/* ------------------------------------------------------------------ */

/* Applied IN ORDER, one at a time, after reordering, scoped to the cluster --
 * hence a pause after each. */
const BASIC_FEATURES = [ 'rphf', 'pref', 'blwf', 'pstf' ];
const OTHER_FEATURES = [ 'pres', 'abvs', 'blws', 'psts' ];

function collectFeatures( b ) {
  b.addGsubPause( setupSyllables );
  b.enableFeature( 'locl', F_PER_SYLLABLE );
  b.enableFeature( 'ccmp', F_PER_SYLLABLE );
  b.addGsubPause( reorderMyanmar );
  for ( const t of BASIC_FEATURES ) {
    b.enableFeature( t, F_MANUAL_ZWJ | F_PER_SYLLABLE );
    b.addGsubPause( null );
  }
  b.addGsubPause( null );          /* syllables are not needed after this */
  for ( const t of OTHER_FEATURES ) b.enableFeature( t, F_MANUAL_ZWJ );
}

function setupMasks( plan, buffer /* , font */ ) {
  const info = buffer.info;
  const n = Math.min( info.length, buffer.len === undefined ? info.length
                                                           : Math.max( buffer.len, 0 ) ) || info.length;
  for ( let i = 0; i < n; i++ ) {
    info[ i ].myanmarCategory = otCategory( info[ i ].codepoint );
    info[ i ].myanmarPosition = POS.START;
  }
}

function setupSyllables( plan, buffer /* , font */ ) {
  findSyllables( buffer );
  if ( typeof buffer.unsafeToBreak === 'function' )
    forEachSyllable( buffer, ( s, e ) => buffer.unsafeToBreak( s, e ) );
}


/* ------------------------------------------------------------------ */
/*  Reordering -- sd-myanmar.txt l.157-205                              */
/* ------------------------------------------------------------------ */

/* Vowels and placeholders count as consonants when looking for the base: a
 * vowel cannot occur in a consonant syllable, so treating them alike lets one
 * routine serve both.  A LIGATED glyph is no longer any category at all. */
const CONSONANTS = new Set( [ OT.C, OT.CS, OT.Ra, OT.IV, OT.GB, OT.DOTTEDCIRCLE ] );
const isConsonant = ( inf ) =>
  !( inf.props & GP_LIGATED ) && CONSONANTS.has( inf.myanmarCategory );

function reorderConsonantSyllable( buffer, start, end ) {
  const info = buffer.info;
  let base = end;
  let hasReph = false;
  let limit = start;

  /* l.159: the kinzi, RA + ASAT + VIRAMA, is a reph and sits after the base. */
  if ( start + 3 <= end &&
       info[ start ].myanmarCategory === OT.Ra &&
       info[ start + 1 ].myanmarCategory === OT.As &&
       info[ start + 2 ].myanmarCategory === OT.H ) {
    limit += 3;
    base = start;
    hasReph = true;
  }
  if ( !hasReph ) base = limit;
  for ( let i = limit; i < end; i++ )
    if ( isConsonant( info[ i ] ) ) { base = i; break; }

  /* Assign every character its visual slot. */
  let i = start;
  for ( ; i < start + ( hasReph ? 3 : 0 ); i++ )
    info[ i ].myanmarPosition = POS.AFTER_MAIN;
  for ( ; i < base; i++ ) info[ i ].myanmarPosition = POS.PRE_C;
  if ( i < end ) { info[ i ].myanmarPosition = POS.BASE_C; i++; }
  let pos = POS.AFTER_MAIN;
  for ( ; i < end; i++ ) {
    const c = info[ i ].myanmarCategory;
    if ( c === OT.MR ) { info[ i ].myanmarPosition = POS.PRE_C; continue; }
    if ( c === OT.VPre ) { info[ i ].myanmarPosition = POS.PRE_M; continue; }
    if ( c === OT.VS ) {
      info[ i ].myanmarPosition = info[ i - 1 ].myanmarPosition;
      continue;
    }
    if ( pos === POS.AFTER_MAIN && c === OT.VBlw ) {
      pos = POS.BELOW_C;
      info[ i ].myanmarPosition = pos;
      continue;
    }
    if ( pos === POS.BELOW_C && c === OT.A ) {
      info[ i ].myanmarPosition = POS.BEFORE_SUB;
      continue;
    }
    if ( pos === POS.BELOW_C && c === OT.VBlw ) {
      info[ i ].myanmarPosition = pos;
      continue;
    }
    if ( pos === POS.BELOW_C && c !== OT.A ) {
      pos = POS.AFTER_SUB;
      info[ i ].myanmarPosition = pos;
      continue;
    }
    info[ i ].myanmarPosition = pos;
  }

  sortRange( buffer, start, end,
             ( a, b ) => a.myanmarPosition - b.myanmarPosition );

  /* A run of left matras comes out of the sort in logical order; visually it
   * is the other way round, except that a variation selector stays attached
   * to the matra it follows -- hence the reverse and the per-matra
   * re-reverse. */
  let firstLeft = end, lastLeft = end;
  for ( let k = start; k < end; k++ )
    if ( info[ k ].myanmarPosition === POS.PRE_M ) {
      if ( firstLeft === end ) firstLeft = k;
      lastLeft = k;
    }
  if ( firstLeft < lastLeft ) {
    reverseRange( buffer, firstLeft, lastLeft + 1 );
    let a = firstLeft;
    for ( let j = a; j <= lastLeft; j++ )
      if ( info[ j ].myanmarCategory === OT.VPre ) {
        reverseRange( buffer, a, j + 1 );
        a = j + 1;
      }
  }
}

function reorderMyanmar( plan, buffer, font ) {
  if ( buffer.scratchFlags === undefined ||
       ( buffer.scratchFlags & SCRATCH_HAS_BROKEN_SYLLABLE ) )
    insertDottedCircles( buffer, font, SYLL.BROKEN, OT.DOTTEDCIRCLE, -1, -1,
                         'myanmarCategory', 'myanmarPosition' );
  forEachSyllable( buffer, ( start, end ) => {
    const t = buffer.info[ start ].syllable & 0x0F;
    if ( t === SYLL.CONSONANT || t === SYLL.BROKEN )
      reorderConsonantSyllable( buffer, start, end );
  } );
}


export const myanmarShaper = {
  name: 'myanmar',
  collectFeatures,
  setupMasks,
  normalizationPreference: 'no-short-circuit',
  zeroWidthMarks: 'by-gdef-early',
  fallbackPosition: false,
  gposTag: null,
};

/* The Zawgyi encoding reuses the Myanmar code points with entirely different
 * meanings, so every automatic step -- normalization, cluster finding,
 * reordering, mark zeroing -- has to be switched off for it.  It is reached
 * through the private-use script tag 'Qaag'. */
export const myanmarZawgyiShaper = {
  name: 'myanmar-zawgyi',
  normalizationPreference: 'none',
  zeroWidthMarks: 'none',
  fallbackPosition: false,
  gposTag: null,
};

/* hb_ot_shaper_categorize(): a font that declares only 'DFLT' or 'latn' takes
 * the default shaper, and so does one that declares 'mymr' -- the tag from
 * before the Myanmar shaping specification existed, whose fonts expect no
 * reordering.  The specification's own tag is 'mym2'. */
/* W19_NO_MYANMAR leaves Myanmar to the default path, as it was before this
 * shaper existed; W19_NO_GSUBSCRIPT ignores what the font declares. */
export function shaperForScript( scriptTag, ctx ) {
  if ( process.env.W19_NO_MYANMAR ) return null;
  if ( scriptTag === 'Qaag' ) return myanmarZawgyiShaper;
  if ( scriptTag !== 'Mymr' ) return null;
  const gs = process.env.W19_NO_GSUBSCRIPT ? null : ( ctx && ctx.gsubScript );
  if ( gs === 'DFLT' || gs === 'latn' || gs === 'mymr' ) return null;
  return myanmarShaper;
}

export default myanmarShaper;
