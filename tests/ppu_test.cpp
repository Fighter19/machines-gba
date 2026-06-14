#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

// Use for debugging the state of the physics world, visually.
// Set in CMake variables
#ifdef USE_SDL
#include <SDL2/SDL.h>
#define SDL_SUCCESS(x) (x == 0)
#endif

static void print_msg(char *msg)
{
  puts(msg);
}

struct __attribute__((packed)) ColorBGR 
{
  uint16_t r: 5;
  uint16_t g: 5;
  uint16_t b: 5;
  uint16_t x: 1;
};

static_assert(sizeof(ColorBGR) == sizeof(Uint16));

//static uint16_t paletteRAM[16][16];

struct PaletteRow
{
  ColorBGR colors[16];
};

struct Palette
{
  PaletteRow rows_bg[16];
  PaletteRow rows_obj[16];
};

typedef enum
{
  OBJ_MODE_NORMAL,
  OBJ_MODE_TRANSPARENT,
  OBJ_MODE_WINDOW,
  OBJ_MODE_PROHIBTED
} ObjMode;

typedef enum
{
  COLOR_AND_PAL_16_16,
  COLOR_AND_PAL_256_1
} ColorsAndPalConfig;

typedef enum
{
  OBJ_SHAPE_SQUARE,
  OBJ_SHAPE_HORIZONTAL,
  OBJ_SHAPE_VERTICAL,
  OBJ_SHAPE_PROHIBITED
} ObjShape;

struct __attribute__((packed)) ObjAttr0
{
  uint8_t y;
  bool rot_scale_on : 1;

  // This is both, double size (if rot/scale on)
  // and obj disable (if not).
  // Couldn't figure out how to make the union not
  // increase the size by 2 bytes
  bool double_size_on_obj_disable : 1;

  ObjMode obj_mode : 2;
  bool mosaic_on : 1;
  ColorsAndPalConfig : 1;
  ObjShape obj_shape : 2;
};

static_assert(sizeof(ObjAttr0) == 2);

struct __attribute__((packed)) ObjAttr1
{
  uint8_t x;
  uint8_t rot_scale_param : 4;

  // This is both, double size (if rot/scale on)
  // and obj disable (if not).
  // Couldn't figure out how to make the union not
  // increase the size by 2 bytes
  bool horizontal_flip : 1;
  bool vertical_flip : 1;

  uint8_t obj_size : 2;
};

static_assert(sizeof(ObjAttr1) == 2);

struct __attribute__((packed)) ObjAttr2
{
  uint16_t number : 10;
  uint8_t priority : 2;
  uint8_t palette_num : 4;
};

static_assert(sizeof(ObjAttr2) == 2);

struct OAMEntry
{
  ObjAttr0 attr0;
  ObjAttr1 attr1;
  ObjAttr2 attr2;
  uint16_t scale;
};

struct OAMEntries
{
  OAMEntry entries[128];
};

static_assert(sizeof(OAMEntry) == 0x8, "Size of OAM entry doesn't match");

static Palette *pPaletteRam = NULL;
static OAMEntries *pOAMEntries = NULL;
static SDL_PixelFormat *s_pPixelFormat = NULL;

typedef Uint16 FBPixel;

// Function to convert color to current preferred SDL format
static inline FBPixel palIndexToRGBA(bool bIsObj, uint8_t palNumber, uint8_t idx)
{
  if (idx == 0)
  {
    // Return value include alpha channel for SDL
    return 0;
  }

  const PaletteRow *pRow = pPaletteRam->rows_obj;
  if (!bIsObj)
  {
    pRow = pPaletteRam->rows_bg;
  }

  const ColorBGR &color = pRow[palNumber].colors[idx];

  // This would be necessary if the pixel format was different
  //return SDL_MapRGB(s_pPixelFormat, color.r, color.g, color.b);

  // But it isn't!
  // Plus however I used it, it didn't yield the expected result
  return *(Uint16*)&color;
}

struct TileInfo
{
  size_t idx;
  size_t palNumber;
};

struct TileData8x8
{
  // Only placeholder to have correct size
  uint8_t data[0x20];
};

/**
 * Render part of a tile to a destination buffer
 * 
 * Runs during line buffer emulation. y is the line of the tile to be presented,
 * needs to be calculated prior to calling.
 * This function doesn't do range checking on X, so the buffer needs to be a tiny bit larger
 * (stride). This is done to avoid having to include a branch, which could impact performance.
 * And to keep the algorithm simple
 * @param pVram The base address to sample the palette indices for the tile from
 * @param pSourcePalette (Complete) Palette to reference palette index from
 * @param pTileInfo Which tile to render with which palette
 * @param bIsObj If VRAM is supposed to be read from object page
 * @param y Which part of the tile to render
 * @param pDst The color output for the tile
 */
static void renderRowOfTileWithPalette(void *pVram, Palette *pSourcePalette, TileInfo *pTileInfo, bool bIsObj, size_t y, FBPixel *pDst)
{
  TileData8x8 *pVram8x8 = (TileData8x8 *)pVram;
  if (bIsObj)
  {
    // Jump forward to Obj page (Which is 0x10000 from base)
    pVram8x8 = (TileData8x8 *)(((char *)pVram) + 0x10000);
  }

  TileData8x8 &vramTile = pVram8x8[pTileInfo->idx];
  // The byte offset where the current line of the tile starts
  size_t rowOffset = y * 4;
  // Draw all 8 pixels into pDst
  // (2 pixels drawn per step because of nibble extraction)
  for (size_t i = 0; i < 4; i++)
  {
    size_t pixel1 = vramTile.data[i + rowOffset] & 0xf;
    size_t pixel2 = (vramTile.data[i + rowOffset] >> 4) & 0xf;
    FBPixel pixel1RGBA = palIndexToRGBA(bIsObj, pTileInfo->palNumber, pixel1);
    FBPixel pixel2RGBA = palIndexToRGBA(bIsObj, pTileInfo->palNumber, pixel2);

    pDst[0] = pixel1RGBA;
    pDst[1] = pixel2RGBA;
    // Progress by two pixels
    pDst += 2;
  }
}

// 256 is 240 aligned to 32,
// in order to allow rendering of a tile horizontally (up to 16 width),
// without having to use range checks
static FBPixel linebuffer[256];

int main(int argc, char *argv[])
{
  int fd = open("vram.bin", O_RDONLY);
  if (fd == -1)
  {
    fprintf(stderr, "Failed to open vram.bin\n");
    return -1;
  }
  void *pVram = mmap((void*)0x06000000, 0x18000, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0x1000);
  if (pVram == MAP_FAILED)
  {
    fprintf(stderr, "Failed to mmap vram\n");
    return -1;
  }

  const size_t nPaletteSize = 0x400;
  const size_t nPaletteOffset = 0x800;

  // pPaletteRam = (PaletteRow*)mmap(NULL, nPaletteSize, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0x800);
  pPaletteRam = (Palette*)mmap((void*)0x05000000, nPaletteSize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  //pPaletteRam = (PaletteRow*)malloc(nPaletteSize);
  if (pPaletteRam == MAP_FAILED || pPaletteRam == NULL)
  {
    fprintf(stderr, "Failed mapping of Palette RAM\n");
    return -1;
  }

  lseek(fd, nPaletteOffset, SEEK_SET);
  read(fd, pPaletteRam, nPaletteSize);

  const size_t nOAMSize = sizeof(OAMEntries);

  pOAMEntries = (OAMEntries*)mmap((void*)0x07000000, nOAMSize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  if (pOAMEntries == MAP_FAILED)
  {
    fprintf(stderr, "Failed mapping of OAM RAM\n");
    return -1;
  }

  lseek(fd, 0xc00, SEEK_SET);
  read(fd, pOAMEntries, nOAMSize);

#ifdef USE_SDL
  int retval = SDL_Init(SDL_INIT_VIDEO);
  if (!SDL_SUCCESS(retval))
  {
    fprintf(stderr, "Error occured while initializing SDL: %s\n", SDL_GetError());
    return -1;
  }

  SDL_Window *pMainWindow = SDL_CreateWindow("Physics test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 240, 140, 0);
  if (!pMainWindow)
  {
    fprintf(stderr, "Error occured while opening Window: %s\n", SDL_GetError());
    return -1;
  }

  // Not relevant
  // Uint32 pixelFormat = SDL_GetWindowPixelFormat(pMainWindow);

  SDL_Renderer *pRenderer = SDL_CreateRenderer(pMainWindow, -1, 0);
  if (!pRenderer)
  {
    fprintf(stderr, "Error occured while setting up renderer: %s\n", SDL_GetError());
    return -1;
  }

  // 16 = bit count. 256 = amount of pixels. Required is by count -> 2byte/pixel * 256 pixels
  Uint32 pixelFormatGBA = SDL_MasksToPixelFormatEnum(16, 0x1F, 0x1F << 5, 0x1F << 10, 0x0);
  s_pPixelFormat = SDL_AllocFormat(pixelFormatGBA);
  if (!s_pPixelFormat)
  {
    fprintf(stderr, "Failed to obtain pixel format\n");
    return -1;
  }

  SDL_Texture *pTexture = SDL_CreateTexture(pRenderer, pixelFormatGBA, SDL_TEXTUREACCESS_STREAMING, 240, 160);
  if (!pTexture)
  {
    fprintf(stderr, "Error occured while setting up SDL texture: %s\n", SDL_GetError());
    return -1;
  }
#endif

  bool bQuit = false;
  while(!bQuit)
  {
#ifdef USE_SDL
    SDL_SetRenderDrawColor(pRenderer, 255, 255, 255, 255);
    SDL_RenderClear(pRenderer);

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      if (event.type == SDL_KEYDOWN)
      {
        bQuit = true;
      }
    }
#endif

    // Currently lock everything
    SDL_Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = 240;
    rect.h = 160;

    // The locked row of pixels that can be rendered
    void *pPixels = NULL;
    int nPitch = 0;
    SDL_LockTexture(pTexture, &rect, &pPixels, &nPitch);

    // TODO: Do rendering
    for (int y = 0; y < 160; y++)
    {
      // Start of linebuffer preparation

      // Mock a color
      for (int x = 0; x < rect.w; x++)
      {
        // Map 160 to 31, roughly by divding by 4
        FBPixel brightness = y >> 2;
        if (brightness >= 31)
        {
          brightness = 31;
        }
        
        linebuffer[x] = brightness * 0x421;
      }

      // Mock render of one tile repeatedly across the very left
      TileInfo tileInfo;
      tileInfo.idx = 0;
      tileInfo.palNumber = 15;
      renderRowOfTileWithPalette(pVram, pPaletteRam, &tileInfo, true, y % 8, &linebuffer[0]);

      // End of linebuffer

      // Copy the linebuffer to the currently locked row.
      // nPitch is in bytes, calculate start of row, then cast to FBPixel
      FBPixel *pRow = (FBPixel *)&((char *)pPixels)[y * nPitch];

      // Only copy as much as fits into the framebuffer row.
      // (Our linebuffer is a bit larger. 256 pixels vs 240 pixels)
      memcpy(pRow, linebuffer, nPitch);
    }


    // Upload the texture
    SDL_UnlockTexture(pTexture);

    // Render the uploaded texture
    SDL_RenderCopy(pRenderer, pTexture, NULL, NULL);

#ifdef USE_SDL
    SDL_RenderPresent(pRenderer);
    // Prevent rendering and logic to cap it to 60 FPS
    usleep(16666);
#endif
  }

#ifdef USE_SDL
  // SDL_FreeSurface(pSurface);
  SDL_DestroyRenderer(pRenderer);
  SDL_DestroyWindow(pMainWindow);
  SDL_Quit();
#endif
  
  return 0;
}