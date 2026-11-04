#ifndef STYLE_H
#define STYLE_H
typedef enum { LEN_AUTO = 0, LEN_PX, LEN_PCT, LEN_NONE,
               LEN_STRETCH,   /* css-sizing-3 `stretch` */
               LEN_CALC       /* css-values-3 calc(): v px + pct %% */
             } len_kind;
/* `pct` is used only by LEN_CALC: calc() does not reduce to one number
 * until the containing block is known, so both parts are carried. */
typedef struct { len_kind kind; double v; double pct; } length;
typedef enum { D_NONE = 0, D_BLOCK, D_INLINE, D_INLINE_BLOCK, D_LIST_ITEM,
               D_TABLE, D_TABLE_ROW, D_TABLE_CELL, D_TABLE_ROW_GROUP,
               D_TABLE_HEADER_GROUP, D_TABLE_FOOTER_GROUP, D_TABLE_COLUMN,
               D_TABLE_COLUMN_GROUP, D_TABLE_CAPTION, D_INLINE_TABLE,
               /* css-flexbox-1 §3: flex containers, appended after the
                  CSS 2.1 values. */
               D_FLEX, D_INLINE_FLEX,
               /* css-grid-1 §3: grid containers, appended after the flex
                  values. */
               D_GRID, D_INLINE_GRID } display_t;
/* css-grid-1 §7.2 track sizes.  `fr` has no <length> equivalent, so a track is
   a tagged union rather than a length. */
typedef enum { TK_AUTO = 0, TK_LEN, TK_FR, TK_MIN_CONTENT, TK_MAX_CONTENT } track_kind;
typedef struct { track_kind kind; length len; double fr; } track;
/* css-grid-1 §8 placement: a line number, a span, or auto. */
typedef enum { GP_AUTO = 0, GP_LINE, GP_SPAN } gridpos_kind;
typedef struct { gridpos_kind kind; int v; } gridpos;
typedef enum { GAF_ROW = 0, GAF_COLUMN, GAF_ROW_DENSE, GAF_COLUMN_DENSE } autoflow_t;
/* css-flexbox-1 §5.1 flex-direction, §5.2 flex-wrap */
typedef enum { FD_ROW = 0, FD_ROW_REVERSE, FD_COLUMN, FD_COLUMN_REVERSE } flexdir_t;
typedef enum { FW_NOWRAP = 0, FW_WRAP, FW_WRAP_REVERSE } flexwrap_t;
/* css-flexbox-1 §8.2 justify-content, §8.3 align-items/align-self,
   §8.4 align-content.  One enum serves all three; not every value is legal
   on every property, and the parser is what rejects the illegal ones. */
typedef enum { AL_AUTO = 0, AL_FLEX_START, AL_FLEX_END, AL_CENTER,
               AL_BASELINE, AL_STRETCH, AL_SPACE_BETWEEN, AL_SPACE_AROUND,
               AL_SPACE_EVENLY
             } align_t;
typedef enum { P_STATIC = 0, P_RELATIVE, P_ABSOLUTE, P_FIXED } position_t;
typedef enum { F_NONE = 0, F_LEFT, F_RIGHT } float_t;
typedef enum { C_NONE = 0, C_LEFT, C_RIGHT, C_BOTH } clear_t;
typedef enum { OV_VISIBLE = 0, OV_HIDDEN, OV_SCROLL, OV_AUTO } overflow_t;
typedef enum { BS_CONTENT = 0, BS_BORDER } boxsizing_t;
typedef enum { DIR_LTR = 0, DIR_RTL } direction_t;
typedef enum { TA_LEFT = 0, TA_RIGHT, TA_CENTER, TA_JUSTIFY } textalign_t;
typedef enum { VA_BASELINE = 0, VA_TOP, VA_BOTTOM, VA_MIDDLE, VA_SUB, VA_SUPER,
               VA_TEXT_TOP, VA_TEXT_BOTTOM, VA_LENGTH } valign_t;
typedef enum { WS_NORMAL = 0, WS_PRE, WS_NOWRAP, WS_PRE_WRAP, WS_PRE_LINE } whitespace_t;
/* The font faces this engine has metrics for.  FF_DEFAULT is the UA's
   standard font, which is what every unrecognised family falls back to. */
typedef enum { FF_DEFAULT = 0, FF_AHEM } fontfamily_t;
enum { EDGE_TOP = 0, EDGE_RIGHT, EDGE_BOTTOM, EDGE_LEFT };
typedef struct computed {
  /* css-flexbox-1.  `order` is an <integer>; flex_grow/flex_shrink are
     <number>s; flex_basis is a <length-percentage> or auto (§7). */
  flexdir_t flex_direction; flexwrap_t flex_wrap;
  align_t justify_content, align_items, align_self, align_content;
  int order; double flex_grow, flex_shrink; length flex_basis;
  /* Set during the cascade: the parent is a flex container.  The
     values module cannot see the box tree (values_box carries one
     box's used geometry and no parent link), so flex-item-ness is
     resolved where the parent's computed style IS in scope. */
  int is_flex_item;
  /* css-grid-1.  Track lists are arena-allocated and shared by pointer:
     `computed` is copied per element during inheritance, so inlining them
     would bloat every style in the tree.  NULL means `none`. */
  const track *grid_cols; int n_grid_cols;
  const track *grid_rows; int n_grid_rows;
  gridpos grid_col_start, grid_col_end, grid_row_start, grid_row_end;
  autoflow_t grid_auto_flow;
  length row_gap, column_gap;
  align_t justify_items, justify_self;
  int is_grid_item;
  display_t display; position_t position; float_t cfloat; clear_t clear;
  overflow_t overflow_x, overflow_y; boxsizing_t box_sizing; direction_t direction;
  textalign_t text_align; valign_t vertical_align; double vertical_align_px;
  whitespace_t white_space;
  length width, height, min_width, min_height, max_width, max_height;
  length margin[4], padding[4], inset[4];
  double border[4];
  double font_size; double line_height;
  length text_indent;
  fontfamily_t font_family; int font_weight;
  int is_replaced; double intrinsic_w, intrinsic_h;
  int z_index_auto; int z_index;
} computed;
#endif
