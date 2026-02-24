//========== Copyright (c) 2012, PG & 2025, WH, All rights reserved. ============//
//
// Purpose:		image wrapper
//
// $NoKeywords: $img
//===============================================================================//

#include "Image.h"

#include "Engine.h"
#include "Environment.h"
#include "File.h"
#include "Logging.h"
#include "ConVar.h"
#include "Graphics.h"
#include "AsyncPool.h"

#include <png.h>
#include <turbojpeg.h>
#include <zlib.h>

#include <csetjmp>
#include <cstddef>
#include <cstring>
#include <utility>

/* ====== stb_image config ====== */
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_PNM
#define STBI_NO_PIC
#define STBI_MINGW_ENABLE_SSE2                     // bring on the pain
#define STB_IMAGE_STATIC                           // we only use stb_image in this translation unit
#define STBI_MAX_DIMENSIONS (16384ULL * 16384ULL)  // there's no way we need anything more than this

#ifndef _DEBUG
#define STBI_ASSERT
#endif

#define STB_IMAGE_IMPLEMENTATION

#include "stb_image.h"
/* ==== end stb_image config ==== */

#if defined(ZLIBNG_VERNUM)
// this is complete bullshit and a bug in zlib-ng (probably, less likely libpng)
// need to prevent zlib from lazy-initializing the crc tables, otherwise data race galore
// literally causes insane lags/issues in completely unrelated places for async loading
#include "SyncOnce.h"

namespace {
Sync::once_flag zlib_init_once;
void zlibinit() {
    uLong dummy_crc = crc32(0L, Z_NULL, 0);
    dummy_crc = crc32(dummy_crc, reinterpret_cast<const Bytef *>("shit"), 4);
    z_stream strm{};
    if(inflateInit(&strm) == Z_OK) inflateEnd(&strm);
    (void)dummy_crc;
}
}  // namespace
#define garbage_zlib() Sync::call_once(zlib_init_once, zlibinit)
#else
#define garbage_zlib()
#endif

namespace {  // static
struct pngErrorManager {
    jmp_buf setjmp_buffer{};
};

void pngErrorExit(png_structp png_ptr, png_const_charp error_msg) {
    debugLog("PNG Error: {:s}", error_msg);
    auto *err = static_cast<pngErrorManager *>(png_get_error_ptr(png_ptr));
    longjmp(&err->setjmp_buffer[0], 1);
}

void pngWarning(png_structp /*unused*/, png_const_charp warning_msg) {
    if constexpr(Env::cfg(BUILD::DEBUG)) {
        debugLog("PNG Warning: {:s}", warning_msg);
    }
}

struct pngMemoryReader {
    const u8 *pdata{nullptr};
    u64 size{0};
    u64 offset{0};
};

void pngReadFromMemory(png_structp png_ptr, png_bytep outBytes, png_size_t byteCountToRead) {
    auto *reader = static_cast<pngMemoryReader *>(png_get_io_ptr(png_ptr));

    if(reader->offset + byteCountToRead > reader->size) {
        png_error(png_ptr, "Read past end of data");
        return;
    }

    memcpy(outBytes, reader->pdata + reader->offset, byteCountToRead);
    reader->offset += byteCountToRead;
}
}  // namespace

Image::ImageDecodeResult Image::decodePNGFromMemory(const u8 *inData, u64 size) {
    garbage_zlib();
    using enum ImageDecodeResult;

    pngErrorManager err;
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, &err, pngErrorExit, pngWarning);
    if(!png_ptr) {
        debugLog("Image Error: png_create_read_struct failed");
        return FAIL;
    }

    if(setjmp(&err.setjmp_buffer[0])) {
        png_destroy_read_struct(&png_ptr, nullptr, nullptr);
        return FAIL;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if(!info_ptr) {
        png_destroy_read_struct(&png_ptr, nullptr, nullptr);
        debugLog("Image Error: png_create_info_struct failed");
        return FAIL;
    }

    // Set up memory reading
    pngMemoryReader reader{.pdata = inData, .size = size, .offset = 0};

    png_set_read_fn(png_ptr, &reader, pngReadFromMemory);

    png_read_info(png_ptr, info_ptr);

    if(this->isInterrupted()) {  // cancellation point
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        return INTERRUPTED;
    }

    u32 tempOutWidth = png_get_image_width(png_ptr, info_ptr);
    u32 tempOutHeight = png_get_image_height(png_ptr, info_ptr);

    if(tempOutWidth > 8192 || tempOutHeight > 8192) {
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        debugLog("Image Error: PNG image size is too big ({} x {})", tempOutWidth, tempOutHeight);
        return FAIL;
    }

    if(this->isInterrupted()) {  // cancellation point
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        return INTERRUPTED;
    }

    i32 outWidth = static_cast<i32>(tempOutWidth);
    i32 outHeight = static_cast<i32>(tempOutHeight);

    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    png_byte interlace_type = png_get_interlace_type(png_ptr, info_ptr);

    // convert to RGBA if needed
    if(bit_depth == 16) png_set_strip_16(png_ptr);

    if(color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);

    if(color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png_ptr);

    if(png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);

    // these color types don't have alpha channel, so fill it with 0xff
    if(color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);

    if(color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png_ptr);

    // "Interlace handling should be turned on when using png_read_image"
    if(interlace_type != PNG_INTERLACE_NONE) png_set_interlace_handling(png_ptr);

    png_read_update_info(png_ptr, info_ptr);

    if(this->isInterrupted()) {  // cancellation point
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        return INTERRUPTED;
    }

    // allocate memory for the image
    this->rawImage = SizedRGBABytes{outWidth, outHeight};

    auto row_pointers = std::make_unique_for_overwrite<png_bytep[]>(outHeight);
    for(sSz y = 0; y < outHeight; y++) {
        row_pointers[y] = &this->rawImage.get()[y * outWidth * Image::NUM_CHANNELS];
    }

    png_read_image(png_ptr, row_pointers.get());

    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return SUCCESS;
}

Image::ImageDecodeResult Image::decodeJPEGFromMemory(const u8 *inData, u64 size) {
    using enum ImageDecodeResult;
    // decode jpeg
    tjhandle tjInstance = tj3Init(TJINIT_DECOMPRESS);
    if(!tjInstance) {
        debugLog("Image Error: tj3Init failed");
        return FAIL;
    }

    if(tj3DecompressHeader(tjInstance, inData, size) < 0) {
        debugLog("Image Error: tj3DecompressHeader failed: {:s}", tj3GetErrorStr(tjInstance));
        tj3Destroy(tjInstance);
        return FAIL;
    }

    if(this->isInterrupted())  // cancellation point
    {
        tj3Destroy(tjInstance);
        return INTERRUPTED;
    }

    i32 outWidth = tj3Get(tjInstance, TJPARAM_JPEGWIDTH);
    i32 outHeight = tj3Get(tjInstance, TJPARAM_JPEGHEIGHT);

    if(outWidth > 8192 || outHeight > 8192) {
        debugLog("Image Error: JPEG image size is too big ({} x {})", outWidth, outHeight);
        tj3Destroy(tjInstance);
        return FAIL;
    }

    if(this->isInterrupted())  // cancellation point
    {
        tj3Destroy(tjInstance);
        return INTERRUPTED;
    }

    // preallocate
    this->rawImage = SizedRGBABytes{outWidth, outHeight};

    // always convert to RGBA for consistency with PNG
    // decompress directly to RGBA
    if(tj3Decompress8(tjInstance, inData, size, this->rawImage.get(), 0, TJPF_RGBA) < 0) {
        debugLog("Image Error: tj3Decompress8 failed: {:s}", tj3GetErrorStr(tjInstance));
        tj3Destroy(tjInstance);
        return FAIL;
    }

    tj3Destroy(tjInstance);
    return SUCCESS;
}

Image::ImageDecodeResult Image::decodeSTBFromMemory(const u8 *inData, u64 size) {
    using enum ImageDecodeResult;

    // use stbi_info to validate dimensions before decoding
    i32 outWidth, outHeight, channels;
    if(!stbi_info_from_memory(inData, static_cast<i32>(size), &outWidth, &outHeight, &channels)) {
        debugLog("Image Error: stb_image info query failed: {:s}", stbi_failure_reason());
        return FAIL;
    }

    if(outWidth > 8192 || outHeight > 8192) {
        debugLog("Image Error: Image size is too big ({} x {})", outWidth, outHeight);
        return FAIL;
    }

    if(this->isInterrupted())  // cancellation point
        return INTERRUPTED;

    u8 *decoded =
        stbi_load_from_memory(inData, static_cast<i32>(size), &outWidth, &outHeight, &channels, Image::NUM_CHANNELS);

    if(!decoded) {
        debugLog("Image Error: stb_image failed: {:s}", stbi_failure_reason());
        return FAIL;
    }

    if(this->isInterrupted()) {  // cancellation point
        stbi_image_free(decoded);
        return INTERRUPTED;
    }

    // don't stbi_image_free, we own the data now
    this->rawImage = SizedRGBABytes{decoded, outWidth, outHeight};

    return SUCCESS;
}

void Image::saveToImage(const u8 *data, i32 width, i32 height, u8 channels, std::string filepath) {
    if(channels != 3 && channels != 4) {
        debugLog("PNG Error: Can only save 3 or 4 channel image data.");
        return;
    }

    garbage_zlib();
    debugLog("Saving image to {:s} ...", filepath);

    FILE *fp = File::fopen_c(filepath.c_str(), "wb");
    if(!fp) {
        debugLog("PNG error: Could not open file {:s} for writing", filepath);
        engine->showMessageError("PNG Error", "Could not open file for writing");
        return;
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if(!png_ptr) {
        fclose(fp);
        debugLog("PNG error: png_create_write_struct failed");
        engine->showMessageError("PNG Error", "png_create_write_struct failed");
        return;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if(!info_ptr) {
        png_destroy_write_struct(&png_ptr, nullptr);
        fclose(fp);
        debugLog("PNG error: png_create_info_struct failed");
        engine->showMessageError("PNG Error", "png_create_info_struct failed");
        return;
    }

    if(setjmp(&png_jmpbuf(png_ptr)[0])) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        debugLog("PNG error during write");
        engine->showMessageError("PNG Error", "Error during PNG write");
        return;
    }

    png_init_io(png_ptr, fp);

    // write header (8 bit colour depth, RGB(A))
    const int pngChannelType = (channels == 4 ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB);

    png_set_IHDR(png_ptr, info_ptr, width, height, 8, pngChannelType, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png_ptr, info_ptr);

    // write row by row directly from the RGB(A) data
    for(sSz y = 0; y < height; y++) {
        png_write_row(png_ptr, &data[y * width * channels]);
    }

    png_write_end(png_ptr, nullptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
}

Image::Image(std::string filepath, bool mipmapped, bool keepInSystemMemory)
    : Resource(IMAGE, std::move(filepath),
               /*doFilesystemExistenceCheck=*/false) {  // we check filesystem status during async load
    this->bMipmapped = mipmapped;
    this->bKeepInSystemMemory = keepInSystemMemory;

    this->type = Image::TYPE::TYPE_PNG;
    this->filterMode = mipmapped ? TextureFilterMode::MIPMAP : TextureFilterMode::LINEAR;
    this->wrapMode = TextureWrapMode::CLAMP;
    this->iWidth = 1;
    this->iHeight = 1;

    this->bCreatedImage = false;
}

Image::Image(i32 width, i32 height, bool mipmapped, bool keepInSystemMemory) : Resource(IMAGE) {
    this->bMipmapped = mipmapped;
    this->bKeepInSystemMemory = keepInSystemMemory;

    this->type = Image::TYPE::TYPE_RGBA;
    this->filterMode = mipmapped ? TextureFilterMode::MIPMAP : TextureFilterMode::LINEAR;
    this->wrapMode = TextureWrapMode::CLAMP;
    this->iWidth = std::min(16384, width);  // sanity
    this->iHeight = std::min(16384, height);

    this->bCreatedImage = true;

    // allocate dirty grid for texture atlas reuploads
    if(keepInSystemMemory) {
        this->dirtyGridW = (this->iWidth + DIRTY_TILE_SIZE - 1) >> DIRTY_TILE_SHIFT;
        this->dirtyGridH = (this->iHeight + DIRTY_TILE_SIZE - 1) >> DIRTY_TILE_SHIFT;
        this->dirtyGrid.resize(static_cast<size_t>(this->dirtyGridW) * this->dirtyGridH, 0);
    }

    // reserve rawImage
    if(cv::debug_image.getBool()) {
        // don't calloc() if we're filling with pink anyways
        this->rawImage = SizedRGBABytes{this->iWidth, this->iHeight};
        // fill with pink pixels
        for(u64 i = 0; i < static_cast<u64>(this->iWidth) * this->iHeight; i++) {
            this->rawImage[i * Image::NUM_CHANNELS + 0] = 255;  // R
            this->rawImage[i * Image::NUM_CHANNELS + 1] = 0;    // G
            this->rawImage[i * Image::NUM_CHANNELS + 2] = 255;  // B
            this->rawImage[i * Image::NUM_CHANNELS + 3] = 255;  // A
        }
    } else {
        // otherwise fill with zeroes (transparent black)
        this->rawImage = SizedRGBABytes{this->iWidth, this->iHeight, true};
    }

    // special case: filled rawimage is always already async ready
    this->setAsyncReady(true);
}

std::vector<McIRect> Image::getDirtyRects() {
    if(this->bDirtyFull || this->dirtyGrid.empty()) return {{0, 0, this->iWidth, this->iHeight}};

    std::vector<McIRect> rects;
    const i32 gw = this->dirtyGridW;
    const i32 gh = this->dirtyGridH;
    u8 *grid = this->dirtyGrid.data();

    for(i32 gy = 0; gy < gh; gy++) {
        for(i32 gx = 0; gx < gw; gx++) {
            if(!grid[gy * gw + gx]) continue;

            // extend right
            i32 gx2 = gx + 1;
            while(gx2 < gw && grid[gy * gw + gx2]) gx2++;

            // extend down (all columns in [gx, gx2) must be dirty)
            i32 gy2 = gy + 1;
            while(gy2 < gh) {
                bool allDirty = true;
                for(i32 cx = gx; cx < gx2; cx++) {
                    if(!grid[gy2 * gw + cx]) {
                        allDirty = false;
                        break;
                    }
                }
                if(!allDirty) break;
                gy2++;
            }

            // zero the tiles
            for(i32 ry = gy; ry < gy2; ry++)
                for(i32 rx = gx; rx < gx2; rx++) grid[ry * gw + rx] = 0;

            // convert tile coords to pixel coords, clamping to image bounds
            const i32 px = gx << DIRTY_TILE_SHIFT;
            const i32 py = gy << DIRTY_TILE_SHIFT;
            const i32 pw = std::min(gx2 << DIRTY_TILE_SHIFT, this->iWidth) - px;
            const i32 ph = std::min(gy2 << DIRTY_TILE_SHIFT, this->iHeight) - py;

            rects.emplace_back(px, py, pw, ph);
        }
    }

    if(rects.empty()) return {{0, 0, this->iWidth, this->iHeight}};
    return rects;
}

bool Image::loadRawImage() {
    bool alreadyLoaded = !!this->rawImage.get() && this->totalBytes() >= 4;

    auto exit = [this]() -> bool {
        // if we were interrupted, it's not a load error
        this->bLoadError.store(!this->isInterrupted(), std::memory_order_release);
        this->rawImage.clear();
        this->iWidth = 0;
        this->iHeight = 0;
        this->bLoadedImageEntirelyTransparent = false;
        return false;
    };

    // if it isn't a created image (created within the engine), load it from the corresponding file
    if(!this->bCreatedImage) {
        if(alreadyLoaded) {  // has already been loaded (or loading it again after setPixel(s))
            // don't render if we're still transparent
            return !this->bLoadedImageEntirelyTransparent;
        }

        if(this->isInterrupted())  // cancellation point
            return exit();

        // load entire file
        std::unique_ptr<u8[]> fileBuffer;
        size_t fileSize{0};
        {
            File file(this->sFilePath);
            if(!file.canRead()) {
                debugLog("Image Error: Couldn't canRead() file {:s}", this->sFilePath);
                return exit();
            }
            if(((fileSize = file.getFileSize()) < 32) || fileSize > INT_MAX) {
                debugLog("Image Error: FileSize is {} in file {:s}", this->sFilePath,
                         fileSize < 32 ? "< 32" : "> INT_MAX");
                return exit();
            }

            if(this->isInterrupted())  // cancellation point
                return exit();

            fileBuffer = file.takeFileBuffer();
            if(!fileBuffer) {
                debugLog("Image Error: Couldn't readFile() file {:s}", this->sFilePath);
                return exit();
            }
            // don't keep the file open
        }

        if(this->isInterrupted())  // cancellation point
            return exit();

        // determine file type by magic number
        this->type = Image::TYPE::TYPE_RGBA;  // default for unknown formats
        bool isJPEG = false;
        bool isPNG = false;
        {
            if(fileBuffer[0] == 0xff && fileBuffer[1] == 0xD8 && fileBuffer[2] == 0xff) {  // 0xFFD8FF
                isJPEG = true;
                this->type = Image::TYPE::TYPE_JPG;
            } else if(fileBuffer[0] == 0x89 && fileBuffer[1] == 0x50 && fileBuffer[2] == 0x4E &&
                      fileBuffer[3] == 0x47) {  // 0x89504E47 (%PNG)
                isPNG = true;
                this->type = Image::TYPE::TYPE_PNG;
            }
        }

        ImageDecodeResult res = ImageDecodeResult::FAIL;

        // try format-specific decoder first if format is recognized
        if(isPNG) {
            res = decodePNGFromMemory(fileBuffer.get(), fileSize);
        } else if(isJPEG) {
            res = decodeJPEGFromMemory(fileBuffer.get(), fileSize);
        }

        // early exit on interruption
        if(res == ImageDecodeResult::INTERRUPTED) {
            return exit();
        }

        // fallback to stb_image if primary decoder failed or format was unrecognized
        if(res == ImageDecodeResult::FAIL) {
            if(isPNG || isJPEG) {
                debugLog("Image Warning: Primary decoder failed for {:s}, trying fallback...", this->sFilePath);
            }
            res = decodeSTBFromMemory(fileBuffer.get(), fileSize);
        }

        // final result check
        if(res != ImageDecodeResult::SUCCESS) {
            if(res == ImageDecodeResult::FAIL) {
                debugLog("Image Error: Could not decode image file {:s}", this->sFilePath);
            }
            return exit();
        }

        if((this->type == Image::TYPE::TYPE_PNG) && canHaveTransparency(fileBuffer.get(), fileSize) &&
           isRawImageCompletelyTransparent()) {
            if(!this->isInterrupted()) {
                debugLog("Image: Ignoring empty transparent image {:s}", this->sFilePath);
            }
            // optimization: ignore completely transparent images (don't render)
            this->bLoadedImageEntirelyTransparent = true;
        }
    } else {
        // don't avoid rendering createdImages with the completelyTransparent check
    }

    // sanity check and one more cancellation point
    if(this->isInterrupted() || !this->rawImage.get() || this->rawImage.getNumBytes() < 4) {
        return exit();
    }

    // update standard width/height to raw image's size (just in case)
    this->iWidth = this->rawImage.getX();
    this->iHeight = this->rawImage.getY();

    return !this->bLoadedImageEntirelyTransparent;
}

Color Image::getPixel(i32 x, i32 y) const {
    if(unlikely(x < 0 || y < 0 || this->totalBytes() < 1)) return 0xffffff00;

    const u64 indexEnd = static_cast<u64>(Image::NUM_CHANNELS) * y * this->rawImage.getX() +
                         static_cast<u64>(Image::NUM_CHANNELS) * x + Image::NUM_CHANNELS;
    if(unlikely(indexEnd > this->totalBytes())) return 0xffffff00;
    const u64 indexBegin =
        static_cast<u64>(Image::NUM_CHANNELS) * y * this->rawImage.getX() + static_cast<u64>(Image::NUM_CHANNELS) * x;

    const Channel &r{this->rawImage[indexBegin + 0]};
    const Channel &g{this->rawImage[indexBegin + 1]};
    const Channel &b{this->rawImage[indexBegin + 2]};
    const Channel &a{this->rawImage[indexBegin + 3]};

    return argb(a, r, g, b);
}

void Image::setPixel(i32 x, i32 y, Color color) {
    assert(!(x < 0 || y < 0 || this->totalBytes() < 1) && "setPixel: out of bounds");

#ifdef _DEBUG
    const u64 indexEnd = static_cast<u64>(Image::NUM_CHANNELS) * y * this->rawImage.getX() +
                         static_cast<u64>(Image::NUM_CHANNELS) * x + Image::NUM_CHANNELS;
    assert(!(indexEnd > this->totalBytes()) && "setPixel: out of bounds");
#endif

    const u64 indexBegin =
        static_cast<u64>(Image::NUM_CHANNELS) * y * this->rawImage.getX() + static_cast<u64>(Image::NUM_CHANNELS) * x;

    this->rawImage[indexBegin + 0] = color.R();
    this->rawImage[indexBegin + 1] = color.G();
    this->rawImage[indexBegin + 2] = color.B();
    this->rawImage[indexBegin + 3] = color.A();
    if(!this->bCreatedImage) {
        if(color.A() != 0) {
            // play it safe, don't recompute the entire alpha channel visibility here
            this->bLoadedImageEntirelyTransparent = false;
        }
    } else if(this->bKeepInSystemMemory && !this->bDirtyFull &&
              this->isReady() /* if we have not already been loaded once, the entire rect is dirty */) {
        this->dirtyGrid[(y >> DIRTY_TILE_SHIFT) * this->dirtyGridW + (x >> DIRTY_TILE_SHIFT)] = 1;
    }
}

void Image::setRegion(i32 x, i32 y, i32 w, i32 h, const u8 *rgbaPixels) {
    assert(x >= 0 && y >= 0 && w > 0 && h > 0);
    assert(x + w <= this->iWidth && y + h <= this->iHeight);

    const sSz stride = static_cast<sSz>(this->iWidth) * NUM_CHANNELS;
    u8 *dst = this->rawImage.get() + static_cast<sSz>(y) * stride + static_cast<sSz>(x) * NUM_CHANNELS;
    const sSz srcStride = static_cast<sSz>(w) * NUM_CHANNELS;

    for(i32 row = 0; row < h; row++) {
        std::memcpy(dst, rgbaPixels + row * srcStride, srcStride);
        dst += stride;
    }

    if(this->bKeepInSystemMemory && !this->bDirtyFull && this->isReady()) {
        const i32 tx0 = x >> DIRTY_TILE_SHIFT;
        const i32 ty0 = y >> DIRTY_TILE_SHIFT;
        const i32 tx1 = (x + w - 1) >> DIRTY_TILE_SHIFT;
        const i32 ty1 = (y + h - 1) >> DIRTY_TILE_SHIFT;
        for(i32 ty = ty0; ty <= ty1; ty++) {
            std::memset(&this->dirtyGrid[ty * this->dirtyGridW + tx0], 1, tx1 - tx0 + 1);
        }
    }
}

void Image::clearRegion(i32 x, i32 y, i32 w, i32 h) {
    assert(x >= 0 && y >= 0 && w > 0 && h > 0);
    assert(x + w <= this->iWidth && y + h <= this->iHeight);

    const sSz stride = static_cast<sSz>(this->iWidth) * NUM_CHANNELS;
    u8 *dst = this->rawImage.get() + static_cast<sSz>(y) * stride + static_cast<sSz>(x) * NUM_CHANNELS;
    const sSz clearBytes = static_cast<sSz>(w) * NUM_CHANNELS;

    for(i32 row = 0; row < h; row++) {
        std::memset(dst, 0, clearBytes);
        dst += stride;
    }

    if(this->bKeepInSystemMemory && !this->bDirtyFull && this->isReady()) {
        const i32 tx0 = x >> DIRTY_TILE_SHIFT;
        const i32 ty0 = y >> DIRTY_TILE_SHIFT;
        const i32 tx1 = (x + w - 1) >> DIRTY_TILE_SHIFT;
        const i32 ty1 = (y + h - 1) >> DIRTY_TILE_SHIFT;
        for(i32 ty = ty0; ty <= ty1; ty++) {
            std::memset(&this->dirtyGrid[ty * this->dirtyGridW + tx0], 1, tx1 - tx0 + 1);
        }
    }
}

void Image::setPixels(const std::vector<u8> &pixels) {
    if(pixels.size() < this->totalBytes()) {
        debugLog("Image Error: setPixels() supplied array is too small!");
        return;
    }

    assert(this->totalBytes() == static_cast<u64>(this->iWidth) * this->iHeight * NUM_CHANNELS &&
           "width and height are somehow out of sync with raw image");

    std::memcpy(this->rawImage.get(), pixels.data(), this->totalBytes());
    if(!this->bCreatedImage) {
        // recompute alpha channel visibility here (TODO: remove if slow)
        this->bLoadedImageEntirelyTransparent = isRawImageCompletelyTransparent();
    }

    this->bDirtyFull = true;
    if(!this->dirtyGrid.empty()) std::memset(this->dirtyGrid.data(), 0, this->dirtyGrid.size());
}

// internal
bool Image::canHaveTransparency(const u8 *data, u64 size) {
    if(size < 33)  // not enough data for IHDR, so just assume true
        return true;

    // PNG IHDR chunk starts at offset 16 (8 bytes signature + 8 bytes chunk header)
    // color type is at offset 25 (16 + 4 width + 4 height + 1 bit depth)
    if(size > 25) {
        u8 colorType = data[25];
        return colorType != 2;  // RGB without alpha
    }

    return true;  // unknown format? just assume true
}

bool Image::isRawImageCompletelyTransparent() const {
    if(!this->rawImage.get() || this->totalBytes() == 0) return false;

    const i64 alphaOffset = 3;
    const i64 totalPixels = static_cast<i64>(this->rawImage.getNumPixels());

    for(i64 i = 0; i < totalPixels; ++i) {
        if(this->isInterrupted())  // cancellation point
            return false;

        // check alpha channel directly
        if(this->rawImage[i * Image::NUM_CHANNELS + alphaOffset] > 0) return false;  // non-transparent pixel
    }

    return true;  // all pixels are transparent
}

void Image::CFree::operator()(void *p) const noexcept {
    // this can take a long time on the main thread so just let some background thread do it
    Async::dispatch(
        [p]() {
            // stb_image_free is just a macro to free, anyways
            free(p);
        },
        Lane::Background);
}
