# only sfnt-based fonts (TrueType/OpenType, also inside WOFF/WOFF2) are ever loaded, so drop the other font drivers
# and the unused renderers (same as build-aux/misc/freetype-sfnt-only.patch for the autotools build)
file(READ "${SOURCE_DIR}/include/freetype/config/ftmodule.h" FILE_CONTENT)
string(REGEX REPLACE
    "FT_USE_MODULE\\( [A-Za-z_]+, ((t1|t1cid|pfr|t42|winfnt|pcf|bdf)_driver_class|ft_(bitmap_)?sdf_renderer_class|ft_svg_renderer_class) \\)\n"
    "" FILE_CONTENT "${FILE_CONTENT}")
file(WRITE "${SOURCE_DIR}/include/freetype/config/ftmodule.h" "${FILE_CONTENT}")

file(READ "${SOURCE_DIR}/include/freetype/config/ftoption.h" FILE_CONTENT)
string(REGEX REPLACE "\n#define (FT_CONFIG_OPTION_ADOBE_GLYPH_LIST|FT_CONFIG_OPTION_SVG)\n" "\n/* #undef \\1 */\n"
    FILE_CONTENT "${FILE_CONTENT}")
file(WRITE "${SOURCE_DIR}/include/freetype/config/ftoption.h" "${FILE_CONTENT}")
