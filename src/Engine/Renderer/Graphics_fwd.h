#pragma once
#include "Color.h"

struct Matrix4;

class Image;
class McFont;
class Shader;
class RenderTarget;
class VertexArrayObject;
class Graphics;

// text effects: shadow, outline. offset pixels to the bottom right
struct TextFX {
    Color col_text{rgb(255, 255, 255)};
    Color col_shadow{rgb(0, 0, 0)};
    float offs_px{1.f};                   // not scaled to display DPI
    Color col_outline{argb(0, 0, 0, 0)};  // disabled by default (alpha=0)
    float outline_px{1.f};                // only used when col_outline.a > 0
    float shadow_softness_px{0.f};        // 0 = hard, >0 = blur spread in pixels
};

enum class AnchorPoint : unsigned char {
    CENTER,        // Default - image centered on x,y
    TOP_LEFT,      // x,y at top left corner
    TOP_RIGHT,     // x,y at top right corner
    BOTTOM_LEFT,   // x,y at bottom left corner
    BOTTOM_RIGHT,  // x,y at bottom right corner
    TOP,           // x,y at top center
    BOTTOM,        // x,y at bottom center
    LEFT,          // x,y at middle left
    RIGHT          // x,y at middle right
};

enum class DrawPrimitive : unsigned char {
    LINES,
    LINE_STRIP,
    TRIANGLES,
    TRIANGLE_FAN,
    TRIANGLE_STRIP,
    QUADS,
    LINE_LOOP
};

enum class DrawUsageType : unsigned char { STATIC, DYNAMIC, STREAM };

enum class DrawPixelsType : unsigned char { UBYTE, FLOAT };

enum class MultisampleType : unsigned char { X0, X2, X4, X8, X16 };

enum class TextureWrapMode : unsigned char { CLAMP, REPEAT };

enum class TextureFilterMode : unsigned char { NONE, LINEAR, MIPMAP };

enum class DrawBlendMode : unsigned char {
    ALPHA,         // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) (default)
    ADDITIVE,      // glBlendFunc(GL_SRC_ALPHA, GL_ONE)
    PREMUL_ALPHA,  // glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                   // GL_ONE_MINUS_SRC_ALPHA)
    PREMUL_COLOR,  // glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)
    MAX            // glBlendEquation(GL_MAX): dst = max(src, dst) per channel (blend factors ignored)
};

enum class DrawCompareFunc : unsigned char { NEVER, LESS, EQUAL, LESSEQUAL, GREATER, NOTEQUAL, GREATEREQUAL, ALWAYS };
