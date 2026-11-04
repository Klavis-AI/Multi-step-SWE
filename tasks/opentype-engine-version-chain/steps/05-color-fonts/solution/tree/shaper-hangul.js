/* shaper-hangul.js -- the Hangul shaper.
 *
 * Implemented from ../otspec/txt/sd-hangul.txt.  Hangul needs a shaper for
 * two reasons that have nothing to do with reordering:
 *
 *  - a syllable may be written precomposed (U+AC00..U+D7A3) or as its jamo
 *    (L, V and optionally T), and the font may support either form and not
 *    the other.  The shaper composes or decomposes to whichever the font has,
 *    and when it leaves the jamo apart it sets 'ljmo' / 'vjmo' / 'tjmo' on
 *    them so the font can pick the right positional shape.  A syllable it
 *    composes or decomposes becomes ONE cluster.
 *  - 'calt' must not reach jamo.  Several widely used fonts put their whole
 *    jamo-shaping apparatus in 'calt', and letting the generic 'calt' run
 *    over a jamo run applies it twice over.  The feature gets a mask of its
 *    own here purely so that it can be cleared on the jamo, which leaves the
 *    rest of the run shaping with 'calt' as usual.
 *
 * Section references are line numbers in sd-hangul.txt.
 */

import { isMonotone } from './buffer.js';

/* Mirrors shape-plan.js's F_* (no import: shape-plan.js imports us). */
const F_NONE = 0;

/* Algorithmic composition, from the Unicode Standard's Hangul Syllable
 * Composition (chapter 3.12). */
const LBase = 0x1100, VBase = 0x1161, TBase = 0x11A7;
const LCount = 19, VCount = 21, TCount = 28;
const SBase = 0xAC00, NCount = VCount * TCount, SCount = LCount * NCount;

const inRange = ( u, a, b ) => u >= a && u <= b;
const isCombiningL = ( u ) => inRange( u, LBase, LBase + LCount - 1 );
const isCombiningV = ( u ) => inRange( u, VBase, VBase + VCount - 1 );
const isCombiningT = ( u ) => inRange( u, TBase + 1, TBase + TCount - 1 );
const isCombinedS = ( u ) => inRange( u, SBase, SBase + SCount - 1 );
const isL = ( u ) => inRange( u, 0x1100, 0x115F ) || inRange( u, 0xA960, 0xA97C );
const isV = ( u ) => inRange( u, 0x1160, 0x11A7 ) || inRange( u, 0xD7B0, 0xD7C6 );
const isT = ( u ) => inRange( u, 0x11A8, 0x11FF ) || inRange( u, 0xD7CB, 0xD7FB );
const isHangulTone = ( u ) => inRange( u, 0x302E, 0x302F );

/* Same order as FEATURES below; 0 is "no jamo feature". */
const _JMO = 0, LJMO = 1, VJMO = 2, TJMO = 3;
const FEATURES = [ null, 'ljmo', 'vjmo', 'tjmo' ];

function collectFeatures( b ) {
  for ( let i = LJMO; i < FEATURES.length; i++ ) b.addFeature( FEATURES[ i ], F_NONE );
}

/* 'calt' is added AGAIN, deliberately: the generic collector adds it globally,
 * and adding it here as a non-global feature gives it a mask bit that
 * setupMasks can clear on the jamo.  See the note at the head of this file. */
function overrideFeatures( b ) {
  b.addFeature( 'calt', F_NONE );
}

function oneMask( plan, tag ) {
  const fm = plan && plan.featureMap && plan.featureMap.get( tag );
  if ( !fm ) return 0;
  if ( plan.gsubFound && !plan.gsubFound.has( tag ) ) return 0;
  return ( ( ( 1 << fm.shift ) >>> 0 ) & fm.mask ) >>> 0;
}

const hasGlyph = ( font, u ) => !!( font && font.getNominalGlyph &&
                                    font.getNominalGlyph( u ) );

function isZeroWidthChar( font, u ) {
  if ( !font || !font.getNominalGlyph ) return false;
  const g = font.getNominalGlyph( u );
  return !!g && font.getGlyphHAdvance( g ) === 0;
}

/* hb_buffer_t::merge_out_grapheme_clusters */
function mergeOutGraphemeClusters( buffer, start, end ) {
  if ( typeof buffer.mergeOutClusters !== 'function' ) return;
  if ( !isMonotone( buffer.clusterLevel ) ||
       ( buffer.clusterLevel !== 0 && buffer.clusterLevel !== 3 ) ) {
    if ( typeof buffer.unsafeToBreakFromOutbuffer === 'function' )
      buffer.unsafeToBreakFromOutbuffer( start, end );
    return;
  }
  buffer.mergeOutClusters( start, end );
}

function preprocessText( plan, buffer, font ) {
  const info = buffer.info;
  for ( let i = 0; i < info.length; i++ ) info[ i ].hangulFeature = _JMO;

  buffer.clearOutput();
  let start = 0, end = 0;          /* the most recent syllable, valid if start < end */
  const count = buffer.len;

  for ( buffer.idx = 0; buffer.idx < count && buffer.successful; ) {
    const u = buffer.cur().codepoint;

    if ( isHangulTone( u ) ) {
      if ( start < end && end === buffer.outLen ) {
        /* A tone mark after a valid syllable moves in FRONT of it -- unless
         * it is zero width, in which case it is designed to overstrike and is
         * left where it is. */
        if ( typeof buffer.unsafeToBreakFromOutbuffer === 'function' )
          buffer.unsafeToBreakFromOutbuffer( start, buffer.idx );
        if ( !buffer.nextGlyph() ) break;
        if ( !isZeroWidthChar( font, u ) ) {
          if ( typeof buffer.mergeOutClusters === 'function' )
            buffer.mergeOutClusters( start, end + 1 );
          const out = buffer.out;
          const tone = out[ end ];
          for ( let i = end; i > start; i-- ) out[ i ] = out[ i - 1 ];
          out[ start ] = tone;
        }
      } else if ( hasGlyph( font, 0x25CC ) ) {
        /* No syllable for the tone mark to sit on: give it a dotted circle. */
        const chars = isZeroWidthChar( font, u ) ? [ 0x25CC, u ] : [ u, 0x25CC ];
        buffer.replaceGlyphs( 1, 2, chars );
      } else {
        buffer.nextGlyph();
      }
      start = end = buffer.outLen;
      continue;
    }

    /* Where a syllable would start, if the next few characters turn out to
     * form one. */
    start = buffer.outLen;

    if ( isL( u ) && buffer.idx + 1 < count && isV( buffer.cur( 1 ).codepoint ) ) {
      const l = u, v = buffer.cur( 1 ).codepoint;
      let t = 0, tindex = 0;
      if ( buffer.idx + 2 < count ) {
        t = buffer.cur( 2 ).codepoint;
        if ( isT( t ) ) tindex = t - TBase;
        else t = 0;
      }
      if ( typeof buffer.unsafeToBreak === 'function' )
        buffer.unsafeToBreak( buffer.idx, buffer.idx + ( t ? 3 : 2 ) );

      if ( isCombiningL( l ) && isCombiningV( v ) && ( t === 0 || isCombiningT( t ) ) ) {
        const s = SBase + ( l - LBase ) * NCount + ( v - VBase ) * TCount + tindex;
        if ( hasGlyph( font, s ) ) {
          buffer.replaceGlyphs( t ? 3 : 2, 1, [ s ] );
          end = start + 1;
          continue;
        }
      }

      /* Not composable -- an Old Hangul syllable with no precomposed code
       * point, or one the font has no glyph for.  Leave the jamo apart and
       * label each with its own feature. */
      buffer.cur().hangulFeature = LJMO;
      if ( !buffer.nextGlyph() ) break;
      buffer.cur().hangulFeature = VJMO;
      if ( !buffer.nextGlyph() ) break;
      if ( t ) {
        buffer.cur().hangulFeature = TJMO;
        if ( !buffer.nextGlyph() ) break;
        end = start + 3;
      } else end = start + 2;
      if ( !buffer.successful ) break;
      mergeOutGraphemeClusters( buffer, start, end );
      continue;
    }

    if ( isCombinedS( u ) ) {
      const s = u;
      const have = hasGlyph( font, s );
      const lindex = Math.floor( ( s - SBase ) / NCount );
      const nindex = ( s - SBase ) % NCount;
      const vindex = Math.floor( nindex / TCount );
      const tindex = nindex % TCount;

      if ( !tindex && buffer.idx + 1 < count &&
           isCombiningT( buffer.cur( 1 ).codepoint ) ) {
        /* <LV, T>: try to combine into <LVT>. */
        const newS = s + ( buffer.cur( 1 ).codepoint - TBase );
        if ( hasGlyph( font, newS ) ) {
          buffer.replaceGlyphs( 2, 1, [ newS ] );
          end = start + 1;
          continue;
        }
        if ( typeof buffer.unsafeToBreak === 'function' )
          buffer.unsafeToBreak( buffer.idx, buffer.idx + 2 );
      }

      /* Otherwise decompose, if the font has no glyph for the composed form,
       * or if a non-combining T follows an LV. */
      if ( !have || ( !tindex && buffer.idx + 1 < count &&
                      isT( buffer.cur( 1 ).codepoint ) ) ) {
        const dec = [ LBase + lindex, VBase + vindex, TBase + tindex ];
        if ( hasGlyph( font, dec[ 0 ] ) && hasGlyph( font, dec[ 1 ] ) &&
             ( !tindex || hasGlyph( font, dec[ 2 ] ) ) ) {
          let sLen = tindex ? 3 : 2;
          buffer.replaceGlyphs( 1, sLen, dec.slice( 0, sLen ) );
          /* An LV decomposed because of a non-combining T after it takes that
           * T into the syllable. */
          if ( have && !tindex ) {
            if ( !buffer.nextGlyph() ) break;
            sLen++;
          }
          if ( !buffer.successful ) break;
          const out = buffer.out;
          end = start + sLen;
          let i = start;
          out[ i++ ].hangulFeature = LJMO;
          out[ i++ ].hangulFeature = VJMO;
          if ( i < end ) out[ i++ ].hangulFeature = TJMO;
          mergeOutGraphemeClusters( buffer, start, end );
          continue;
        }
        if ( !tindex && buffer.idx + 1 < count && isT( buffer.cur( 1 ).codepoint ) &&
             typeof buffer.unsafeToBreak === 'function' )
          buffer.unsafeToBreak( buffer.idx, buffer.idx + 2 );
      }

      if ( have ) end = start + 1;
    }

    /* Nothing recognisable: leave end <= start so a following tone mark does
     * not reorder. */
    buffer.nextGlyph();
  }
  buffer.sync();
}

function setupMasks( plan, buffer /* , font */ ) {
  const masks = FEATURES.map( ( t ) => ( t ? oneMask( plan, t ) : 0 ) );
  const calt = oneMask( plan, 'calt' );
  const info = buffer.info;
  const n = Math.min( info.length, buffer.len === undefined ? info.length
                                                           : Math.max( buffer.len, 0 ) ) || info.length;
  for ( let i = 0; i < n; i++ ) {
    const f = info[ i ].hangulFeature || _JMO;
    if ( masks[ f ] ) info[ i ].mask = ( info[ i ].mask | masks[ f ] ) >>> 0;
    const u = info[ i ].codepoint;
    if ( calt && ( isL( u ) || isV( u ) || isT( u ) ) )
      info[ i ].mask = ( info[ i ].mask & ~calt ) >>> 0;
  }
}


export const hangulShaper = {
  name: 'hangul',
  collectFeatures,
  overrideFeatures,
  preprocessText,
  setupMasks,
  /* Hangul composes and decomposes for itself; the generic normalizer must
   * not take a second view of it. */
  normalizationPreference: 'none',
  zeroWidthMarks: 'none',
  fallbackPosition: true,
  gposTag: null,
};

/* W19_NO_HANGUL leaves Hangul on the default path, as it was before this
 * shaper existed. */
export function shaperForScript( scriptTag ) {
  if ( process.env.W19_NO_HANGUL ) return null;
  return scriptTag === 'Hang' ? hangulShaper : null;
}

export default hangulShaper;
