#ifndef BITMAP_H_INCLUDED
#define BITMAP_H_INCLUDED


typedef struct BitmapFileHeaderStruct{
    uint8_t  signature[2];       // "BM"
    uint32_t fileSize;           // size in bytes
    uint16_t reserved1;          // zero
    uint16_t reserved2;          // zero
    uint32_t dataOffset;         // (sizeof(File Header) + sizeof(Info Header)). offset from begining of file to the begining of the bitmap data
}__attribute__((__packed__)) BmpFileHdr;


typedef struct BitmapInfoV4HeaderStruct{
    uint32_t sizeHeader;        // size of InfoHeader (40 bytes)
    uint32_t width;             // pixels per row
    uint32_t height;            // number of rows (0 is the bottom of image)
    uint16_t planes;            // 1. number of planes
    uint16_t bitsPerPixel;      // 32 RGBA. monochrome(1), 4bit Pallete(4), 8bit Pallete(8), 16bit RGB(16), 24bit RGB(24)
    uint32_t compression;       // 3 BI_BITFIELDS. BI_RGB no compression (0), BI_RLE8 8bit (1), BI_RLE4 4bit (2),
    uint32_t imageSize;         // 4 * width * height. Compressed image size (ok to set to 0 if image is not compressed)
    uint32_t XpixelsPerM;       // 2835. pixels per meter (dpi*39.3701)
    uint32_t YpixelsPerM;       // 2835. pixels per meter (dpi*39.3701)
    uint32_t colorsUsed;        // zero. number of actually used colors. For 8-bit/pixel bitmap will be 100h or 256
    uint32_t importantColors;   // zero. number of important colors (0 = ALL)
    uint32_t redMask;           // 0x0000FF00
    uint32_t greenMask;         // 0x00FF0000
    uint32_t blueMask;          // 0xFF000000
    uint32_t alphaMask;         // 0x000000FF
    uint8_t  csType[4];         // 'Win '
    uint8_t  ciexyzTriple[0x24];// zero
    uint32_t gammaRed;          // zero
    uint32_t gammaGreen;        // zero
    uint32_t gammaBlue;         // zero
}__attribute__((__packed__)) BmpInfoV4Hdr;


typedef struct BitmapInfoHeaderStruct{
    uint32_t sizeHeader;        // size of InfoHeader (40 bytes)
    uint32_t width;             // pixels per row
    uint32_t height;            // number of rows (0 is the bottom of image)
    uint16_t planes;            // 1. number of planes
    uint16_t bitsPerPixel;      // 24. monochrome(1), 4bit Pallete(4), 8bit Pallete(8), 16bit RGB(16), 24bit RGB(24)
    uint32_t compression;       // 0. BI_RGB no compression (0), BI_RLE8 8bit (1), BI_RLE4 4bit (2),
    uint32_t imageSize;         // 4 * width * height. Compressed image size (ok to set to 0 if image is not compressed)
    uint32_t XpixelsPerM;       // 2835. Pixels per meter
    uint32_t YpixelsPerM;       // 2835. Pixels per meter
    uint32_t colorsUsed;        // zero. Number of actually used colors. For 8-bit/pixel bitmap will be 100h or 256
    uint32_t importantColors;   // zero. Number of important colors (0 = ALL)
}__attribute__((__packed__)) BmpInfoHdr;


typedef struct BitmapPixelStruct{
    uint8_t a;  // alpha 
    uint8_t r;  // red
    uint8_t g;  // green
    uint8_t b;  // blue
}__attribute__((__packed__)) BmpPixelData;


#endif // BITMAP_H_INCLUDED
