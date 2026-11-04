#ifndef STYLE_H
#define STYLE_H
typedef enum { LEN_AUTO = 0, LEN_PX, LEN_PCT, LEN_NONE,
               LEN_STRETCH } len_kind;   /* css-sizing-3 `stretch` */
typedef struct { len_kind kind; double v; } length;
typedef enum { D_NONE = 0, D_BLOCK, D_INLINE, D_INLINE_BLOCK, D_LIST_ITEM,
               D_TABLE, D_TABLE_ROW, D_TABLE_CELL, D_TABLE_ROW_GROUP,
               D_TABLE_HEADER_GROUP, D_TABLE_FOOTER_GROUP, D_TABLE_COLUMN,
               D_TABLE_COLUMN_GROUP, D_TABLE_CAPTION, D_INLINE_TABLE } display_t;
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
