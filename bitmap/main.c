#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "BitmapV4.h"

#define _ 0xFFFFFFFF,
#define X 0x000000FF,

static uint32_t picture[]= /* 16x16 pixels */
{
    X X X X X X X X X X X X X X X X X X X X
    X _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ X X X
    X _ X X X X _ X X X X _ X X X X _ X X X
    X _ X _ _ _ _ X _ _ _ _ X _ _ X _ X X X
    X _ X X X _ _ X X X _ _ X X X X _ X X X
    X _ X _ _ _ _ X _ _ _ _ X _ X _ _ X X X
    X _ X _ _ _ _ X X X X _ X _ _ X _ X X X
    X _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ X X X
    X _ X _ _ X _ X X X X _ X _ _ X _ X X X
    X _ X X _ X _ X _ _ X _ X X _ X _ X X X
    X _ X X X X _ X X X X _ X X X X _ X X X
    X _ X _ X X _ X _ _ X _ X _ X X _ X X X
    X _ X _ _ X _ X _ _ X _ X _ _ X _ X X X
    X _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ X X X
    X _ X X X _ _ X X X X _ _ X X _ _ X X X
    X _ X _ _ X _ X _ _ X _ _ X X _ _ X X X
    X _ X _ _ X _ X _ _ X _ _ X X _ _ X X X
    X _ X _ _ X _ X _ _ X _ _ _ _ _ _ X X X
    X _ X X X _ _ X X X X _ _ X X _ _ X X X
    X _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ X X X
    X X X X X X X X X X X X X X X X X X X X
};


void printBitmap(void *buff){
    BmpFileHdr *hdr = buff;
    BmpInfoV4Hdr *info = buff + sizeof(BmpFileHdr);

    printf("\n");
    printf("magic...........%c%c\n", hdr->signature[0], hdr->signature[1]);
    printf("filesize........%d\n", hdr->fileSize);
    printf("  hdr...........%ld\n", sizeof(BmpFileHdr));
    printf("  info..........%ld\n", sizeof(BmpInfoV4Hdr));
    printf("  imgsize.......%ld\n", (hdr->fileSize - sizeof(BmpFileHdr) - sizeof(BmpInfoV4Hdr)));
    printf("reserved1.......%d\n", hdr->reserved1);
    printf("reserved2.......%d\n", hdr->reserved2);
    printf("dataOffset......%d\n", hdr->dataOffset);
    printf("size header.....%d\n", info->sizeHeader);
    printf("width...........%d\n", info->width);
    printf("height..........%d\n", info->height);
    printf("planes..........%d\n", info->planes);
    printf("bitsPerPixel....%d\n", info->bitsPerPixel);
    printf("compression.....%d\n", info->compression);
    printf("imageSize.......%d\n", info->imageSize);
    printf("XpixelsPerM.....%d\n", info->XpixelsPerM);
    printf("YpixelsPerM.....%d\n", info->YpixelsPerM);
    printf("colorsUsed......%d\n", info->colorsUsed);
    printf("importantColors.%d\n", info->importantColors);
    printf("bV4RedMask......0x%08X\n", info->redMask);
    printf("bV4GreenMask....0x%08X\n", info->greenMask);
    printf("bV4BlueMask.....0x%08X\n", info->blueMask);
    printf("bV4AlphaMask....0x%08X\n", info->alphaMask);
    printf("bV4CSType.......0x%02x%02x%02x%02x\n", info->csType[0], info->csType[1], info->csType[2], info->csType[3]);
    for(int i=0; i < sizeof(info->ciexyzTriple); i++) { printf("%02x ", info->ciexyzTriple[i]); } printf("\n");
    printf("bV4GammaRed.....%x\n", info->gammaRed);
    printf("bV4GammaGreen...%x\n", info->gammaGreen);
    printf("bV4GammaBlue....%x\n", info->gammaBlue);

    uint32_t *pix = buff + hdr->dataOffset;
    for(int i=1; i <= (info->width * info->height); i++)
    {
        printf("%08X", *pix);
        printf( (i%16 == 0) ? "\n" : " ");        
        pix++;
    }
    printf("\n");
}


uint32_t getBitmap(uint32_t width, uint32_t height, void **buffer, BmpPixelData **pixel)
{
    int dpi = 72;
    int ppm = dpi*39.3701;
    uint32_t imageSize = 4 * width * height;
    uint32_t fileSize = sizeof(BmpFileHdr) + sizeof(BmpInfoV4Hdr) + imageSize;

    void *buff = calloc(fileSize, sizeof(uint8_t));
    if (NULL == buff){
        return 0;
    }

    BmpFileHdr *hdr = buff;
    BmpInfoV4Hdr *info = buff + sizeof(BmpFileHdr);

    hdr->signature[0] = 'B';
    hdr->signature[1] = 'M';
    hdr->fileSize = fileSize;
    hdr->reserved1 = 0;
    hdr->reserved2 = 0;
    hdr->dataOffset = sizeof(BmpFileHdr) + sizeof(BmpInfoV4Hdr);

    info->sizeHeader = sizeof(BmpInfoV4Hdr); // number of header bytes (from this point)
    info->width  = width;
    info->height = height;
    info->planes = 1;
    info->bitsPerPixel = 32;
    info->compression = 3;
    info->imageSize = imageSize;
    info->XpixelsPerM = ppm;
    info->YpixelsPerM = ppm;
    info->colorsUsed = 0;
    info->importantColors = 0;
    info->redMask    = 0x0000FF00;
    info->greenMask  = 0x00FF0000;
    info->blueMask   = 0xFF000000;
    info->alphaMask  = 0x000000FF;
    info->csType[0]  = 'W';
    info->csType[1]  = 'i';
    info->csType[2]  = 'n';
    info->csType[3]  = ' ';
    info->gammaRed   = 0x0;
    info->gammaGreen = 0x0;
    info->gammaBlue  = 0x0;

    *pixel =  buff + hdr->dataOffset;
    *buffer = buff;

    return fileSize;
}


int main()
{
    int w = 20;
    int h = 21;
    uint32_t size = 0;
    void *buff = NULL;
    BmpPixelData *pixels = NULL;
    
    size = getBitmap(w, h, &buff, &pixels);

    for (int y = h-1; y >= 0; y--) // run backwards
    for(int x = 0; x < w; x++)
    {
        uint32_t *p = (uint32_t*)pixels;
        *p = picture[(w*y)+x];
        
        pixels++; 
    }

    printBitmap(buff);
    FILE *image = fopen("fernando.bmp", "wb");
    fwrite(buff, 1, size, image);
    fclose(image);
    free(buff);

    return 0;
}
