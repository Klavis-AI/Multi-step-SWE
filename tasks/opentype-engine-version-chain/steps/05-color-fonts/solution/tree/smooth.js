/* smooth.js -- port of FreeType 2.14.3 `ft_smooth_render`
 * (src/smooth/ftsmooth.c:557) restricted to FT_RENDER_MODE_NORMAL, together
 * with the pieces it calls:
 *   - ft_glyphslot_preset_bitmap  (src/base/ftobjs.c)
 *   - FT_Outline_Get_CBox         (src/base/ftoutln.c)
 *   - FT_Outline_Translate        (src/base/ftoutln.c)
 *   - ft_smooth_raster_overlap /  ft_smooth_overlap_spans (ftsmooth.c:466-553)
 *
 * FT_CONFIG_OPTION_SUBPIXEL_RENDERING is NOT defined in the reference build
 * (ftoption.h:126 is commented out), but that only selects between two LCD
 * implementations, which NORMAL mode never reaches.
 */

import {
  grayRasterRender,
  FT_RASTER_FLAG_AA,
  FT_RASTER_FLAG_DIRECT,
  FT_OUTLINE_OVERLAP,
  Smooth_Err_Ok,
  Smooth_Err_Raster_Overflow,
} from './raster.js';

export { Smooth_Err_Ok, Smooth_Err_Raster_Overflow };


/* ---- FT_Outline_Get_CBox (src/base/ftoutln.c) ------------------------- */
export function computeCBox( outline ) {
  let xMin, yMin, xMax, yMax;

  if ( outline.n_points === 0 ) {
    xMin = 0; yMin = 0; xMax = 0; yMax = 0;
  } else {
    const px = outline.x, py = outline.y;

    xMin = xMax = px[0];
    yMin = yMax = py[0];

    for ( let i = 1; i < outline.n_points; i++ ) {
      const x = px[i];
      if ( x < xMin ) xMin = x;
      if ( x > xMax ) xMax = x;

      const y = py[i];
      if ( y < yMin ) yMin = y;
      if ( y > yMax ) yMax = y;
    }
  }

  return { xMin, yMin, xMax, yMax };
}


/* ---- FT_Outline_Translate (src/base/ftoutln.c) ------------------------
 *   vec->x = ADD_LONG( vec->x, xOffset );
 * ADD_LONG is a wrapping 64-bit add; unreachable at these magnitudes.
 * Mutates the outline in place, exactly like the C. */
export function FT_Outline_Translate( outline, xOffset, yOffset ) {
  if ( !outline ) return;

  const px = outline.x, py = outline.y;

  for ( let n = 0; n < outline.n_points; n++ ) {
    px[n] = px[n] + xOffset;
    py[n] = py[n] + yOffset;
  }
}


/* ---- ft_glyphslot_preset_bitmap, NORMAL branch (src/base/ftobjs.c) ----
 * `origin` is NULL for FT_Render_Glyph, so x_shift == y_shift == 0.
 *
 * C emulation: `cbox.xMin >> 6` is an arithmetic shift of a signed 64-bit
 * FT_Pos (floor), and `cbox.xMin & 63` is the non-negative remainder mod 64.
 * A JS `>>`/`&` would first truncate to int32, so both are spelled out with
 * Math.floor.
 *
 * `opts` may carry { x_ppem, y_ppem }.  The C guards the ppem clause with
 * `slot->face &&`, so omitting them reproduces the face-less case exactly. */
export function presetBitmapNormal( outline, opts = {} ) {
  const cbox = computeCBox( outline );

  /* rough estimate of pixel box */
  const pbox = {
    xMin: Math.floor( cbox.xMin / 64 ),
    yMin: Math.floor( cbox.yMin / 64 ),
    xMax: Math.floor( cbox.xMax / 64 ),
    yMax: Math.floor( cbox.yMax / 64 ),
  };

  /* tiny remainder box */
  const rxMin = cbox.xMin - Math.floor( cbox.xMin / 64 ) * 64;
  const ryMin = cbox.yMin - Math.floor( cbox.yMin / 64 ) * 64;
  const rxMax = cbox.xMax - Math.floor( cbox.xMax / 64 ) * 64;
  const ryMax = cbox.yMax - Math.floor( cbox.yMax / 64 ) * 64;

  /* Adjust: (FT_RENDER_MODE_NORMAL / LIGHT / default) */
  pbox.xMin += Math.floor( rxMin / 64 );
  pbox.yMin += Math.floor( ryMin / 64 );
  pbox.xMax += Math.floor( ( rxMax + 63 ) / 64 );
  pbox.yMax += Math.floor( ( ryMax + 63 ) / 64 );

  const x_left = pbox.xMin;
  const y_top  = pbox.yMax;

  const width  = pbox.xMax - pbox.xMin;
  const height = pbox.yMax - pbox.yMin;

  const hasFace = opts.x_ppem !== undefined && opts.y_ppem !== undefined;

  const overflow = !!(
    width >= 0x10000 || height >= 0x10000 ||
    pbox.xMin < -0x1000000 || pbox.xMax >= 0x1000000 ||
    pbox.yMin < -0x1000000 || pbox.yMax >= 0x1000000 ||
    ( hasFace && ( width  > 10 * opts.x_ppem ||
                   height > 10 * opts.y_ppem ) )
  );

  /* FT_PIXEL_MODE_GRAY: pitch = width */
  return { width, rows: height, pitch: width, left: x_left, top: y_top, overflow };
}


/* ---- ft_smooth_overlap_spans (ftsmooth.c:466-494) ---------------------
 * Direct-mode callback used only when FT_OUTLINE_OVERLAP is set.  Receives
 * the span arrays produced by gray_sweep_direct. */
const SCALE = 1 << 2;

function ft_smooth_overlap_spans( y, count, spanX, spanLen, spanCov, target ) {
  const buf = target.buf;

  /* y >= clip_box.yMin == 0, so the C int division truncates == floors */
  const dst = target.origin - Math.floor( y / SCALE ) * target.pitch;

  for ( let i = 0; i < count; i++ ) {
    const cover = Math.floor( ( spanCov[i] + SCALE * SCALE / 2 )
                              / ( SCALE * SCALE ) );

    for ( let x = 0; x < spanLen[i]; x++ ) {
      const idx = dst + Math.floor( ( spanX[i] + x ) / SCALE );
      const sum = buf[idx] + cover;

      /* (unsigned char)( sum - ( sum >> 8 ) ): clamps 256 down to 255 */
      buf[idx] = ( sum - ( sum >> 8 ) ) & 255;
    }
  }
}


/* ---- ft_smooth_raster_overlap (ftsmooth.c:497-553) -------------------- */
export function ft_smooth_raster_overlap( outline, bitmap ) {
  /* Reject outlines that are too wide for 16-bit FT_Span. */
  if ( bitmap.width * SCALE > 0x7FFF )
    return Smooth_Err_Raster_Overflow;

  const target = {
    buf:    bitmap.buffer,
    pitch:  bitmap.pitch,
    origin: bitmap.pitch < 0 ? 0 : ( bitmap.rows - 1 ) * bitmap.pitch,
  };

  const params = {
    source:     outline,
    flags:      FT_RASTER_FLAG_AA | FT_RASTER_FLAG_DIRECT,
    gray_spans: ft_smooth_overlap_spans,
    user:       target,
    clip_box:   { xMin: 0, yMin: 0,
                  xMax: bitmap.width * SCALE, yMax: bitmap.rows * SCALE },
  };

  const px = outline.x, py = outline.y;

  /* inflate outline */
  for ( let i = 0; i < outline.n_points; i++ ) {
    px[i] = px[i] * SCALE;
    py[i] = py[i] * SCALE;
  }

  const error = grayRasterRender( params );

  /* deflate outline -- C's `/=` truncates, but the values are exact
   * multiples of SCALE here, so this is an exact inverse. */
  for ( let i = 0; i < outline.n_points; i++ ) {
    px[i] = Math.trunc( px[i] / SCALE );
    py[i] = Math.trunc( py[i] / SCALE );
  }

  return error;
}


/* ---- ft_smooth_render, FT_RENDER_MODE_NORMAL (ftsmooth.c:557-662) -----
 * Returns { width, rows, pitch, left, top, buffer, error }.
 *
 * `buffer` is null when the bitmap is empty (rows == 0 or pitch == 0), which
 * the C reports as success with a NULL bitmap->buffer, and also on error,
 * which the C reports by freeing the buffer it had allocated.
 *
 * Like the C, the outline is translated in place and translated back before
 * returning, so the caller's outline is unchanged on exit. */
export function renderNormal( outline, opts = {} ) {
  const preset = presetBitmapNormal( outline, opts );
  const { width, rows, pitch, left, top } = preset;

  if ( preset.overflow )
    return { width, rows, pitch, left, top,
             buffer: null, error: Smooth_Err_Raster_Overflow };

  if ( !rows || !pitch )
    return { width, rows, pitch, left, top, buffer: null, error: Smooth_Err_Ok };

  const buffer = new Uint8Array( rows * pitch );
  const bitmap = { width, rows, pitch, buffer };

  const x_shift = 64 * -left;
  const y_shift = 64 * -top + 64 * rows;

  if ( x_shift || y_shift )
    FT_Outline_Translate( outline, x_shift, y_shift );

  let error;
  try {
    if ( outline.flags & FT_OUTLINE_OVERLAP ) {
      error = ft_smooth_raster_overlap( outline, bitmap );
    } else {
      error = grayRasterRender( {
        target: bitmap,
        source: outline,
        flags:  FT_RASTER_FLAG_AA,
      } );
    }
  } finally {
    if ( x_shift || y_shift )
      FT_Outline_Translate( outline, -x_shift, -y_shift );
  }

  return { width, rows, pitch, left, top,
           buffer: error ? null : buffer, error };
}
