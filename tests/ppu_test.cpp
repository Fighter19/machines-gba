#include <stdio.h>
#include <fcntl.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/mman.h>
#else
#include <Windows.h>
#endif

// Use for debugging the state of the physics world, visually.
// Set in CMake variables
#ifdef USE_SDL
#include "SDL3/SDL.h"
#endif

#include <fstream>

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

typedef enum : uint8_t
{
  OBJ_MODE_NORMAL,
  OBJ_MODE_TRANSPARENT,
  OBJ_MODE_WINDOW,
  OBJ_MODE_PROHIBTED
} ObjMode;

typedef enum : uint8_t
{
  COLOR_AND_PAL_16_16,
  COLOR_AND_PAL_256_1
} ColorsAndPalConfig;

typedef enum : uint8_t
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
  uint16_t priority : 2;
  uint16_t palette_num : 4;
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
static const SDL_PixelFormatDetails *s_pPixelFormat = NULL;

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

    if (pixel1)
      pDst[0] = pixel1RGBA;
    if (pixel2)
      pDst[1] = pixel2RGBA;
    // Progress by two pixels
    pDst += 2;
  }
}

struct ObjDimensions
{
  uint8_t nWidth;
  uint8_t nHeight;
};

struct ObjSizeLUTEntry
{
  ObjDimensions square;
  ObjDimensions horizontal;
  ObjDimensions vertical;
};

static const ObjSizeLUTEntry k_ObjSizeLUT[4] =
{
  // 0 - 8
  {
    .square = {.nWidth = 8, .nHeight = 8},
    .horizontal = {.nWidth = 16, .nHeight = 8},
    .vertical = {.nWidth = 8, .nHeight = 16}
  },
  // 1 - 16
  {
    .square = {.nWidth = 16, .nHeight = 16},
    .horizontal = {.nWidth = 32, .nHeight = 8},
    .vertical = {.nWidth = 8, .nHeight = 32}
  },
  // 2 - 32
  {
    .square = {.nWidth = 32, .nHeight = 32},
    .horizontal = {.nWidth = 32, .nHeight = 16},
    .vertical = {.nWidth = 16, .nHeight = 32}
  },
  // 3 - 64
  {
    .square = {.nWidth = 64, .nHeight = 64},
    .horizontal = {.nWidth = 64, .nHeight = 32},
    .vertical = {.nWidth = 32, .nHeight = 64}
  }
};

#ifndef WIN32
static void DebugBreak()
{
  abort();
}
#endif

static ObjDimensions GetTileDimensions(ObjShape shape, uint8_t nObjSize)
{
  if (shape == OBJ_SHAPE_SQUARE)
  {
    return k_ObjSizeLUT[nObjSize].square;
  }
  else if (shape == OBJ_SHAPE_HORIZONTAL)
  {
    return k_ObjSizeLUT[nObjSize].horizontal;
  }
  else if (shape == OBJ_SHAPE_VERTICAL)
  {
    return k_ObjSizeLUT[nObjSize].vertical;
  }

  // Assertion hit: shape is unknown/impossible
  DebugBreak();
  return k_ObjSizeLUT[nObjSize].square;
}

// 256 is 240 aligned to 32,
// in order to allow rendering of a tile horizontally (up to 16 width),
// without having to use range checks
static FBPixel linebuffer[256];

typedef enum
{
  GAME_ACTION_MOVE_LEFT,
  GAME_ACTION_MOVE_RIGHT,
  GAME_ACTION_MAX
} GameActionEnum;

static bool s_bGameActionActive[GAME_ACTION_MAX] = {false};

static void SetGameActionActive(GameActionEnum action, bool bActive)
{
  s_bGameActionActive[action] = bActive;
}

static bool IsGameActionActive(GameActionEnum action)
{
  return s_bGameActionActive[action];
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
  std::ifstream file_vram("vram.bin", std::ios::binary);
  if (!file_vram.is_open())
  {
    fprintf(stderr, "Failed to open vram.bin");
    return -1;
  }

  void *pVram = VirtualAlloc((void *)0x06000000, 0x18000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (pVram == NULL)
  {
    fprintf(stderr, "VritualAlloc failed:\n");
    fprintf(stderr, "%lu", GetLastError());
    return -1;
  }

  file_vram.seekg(0x1000, std::ios_base::beg);
  file_vram.read((char*)pVram, 0x18000);

  // PALETTE

  const size_t nPaletteSize = 0x400;
  const size_t nPaletteOffset = 0x800;

  pPaletteRam = (Palette*)VirtualAlloc((void *)0x05000000, nPaletteSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (pPaletteRam == NULL)
  {
    fprintf(stderr, "VritualAlloc failed:\n");
    fprintf(stderr, "%lu", GetLastError());
    return -1;
  }

  file_vram.seekg(nPaletteOffset, file_vram.beg);
  file_vram.read((char*)pPaletteRam, nPaletteSize);

  // OAM

  const size_t nOAMSize = sizeof(OAMEntries);

  pOAMEntries = (OAMEntries*)VirtualAlloc((void *)0x07000000, nPaletteSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (pOAMEntries == NULL)
  {
    fprintf(stderr, "VritualAlloc failed:\n");
    fprintf(stderr, "%lu", GetLastError());
    return -1;
  }

  file_vram.seekg(0xc00, std::ios_base::beg);
  file_vram.read((char*)pOAMEntries, nOAMSize);
#else
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
#endif

#ifdef USE_SDL
  bool bSuccess = SDL_Init(SDL_INIT_VIDEO);
  if (!bSuccess)
  {
    fprintf(stderr, "Error occured while initializing SDL: %s\n", SDL_GetError());
    return -1;
  }

  SDL_Window *pMainWindow = SDL_CreateWindow("Physics test", 240, 160, 0);
  if (!pMainWindow)
  {
    fprintf(stderr, "Error occured while opening Window: %s\n", SDL_GetError());
    return -1;
  }

  // Not relevant
  // Uint32 pixelFormat = SDL_GetWindowPixelFormat(pMainWindow);

  SDL_Renderer *pRenderer = SDL_CreateRenderer(pMainWindow, NULL);
  if (!pRenderer)
  {
    fprintf(stderr, "Error occured while setting up renderer: %s\n", SDL_GetError());
    return -1;
  }

  // 16 = bit count. 256 = amount of pixels. Required is by count -> 2byte/pixel * 256 pixels
  SDL_PixelFormat pixelFormatGBA = SDL_GetPixelFormatForMasks(16, 0x1F, 0x1F << 5, 0x1F << 10, 0x0);
  s_pPixelFormat = SDL_GetPixelFormatDetails(pixelFormatGBA);

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
      if (event.type == SDL_EVENT_KEY_DOWN)
      {
        if (event.key.key == SDLK_RIGHT)
        {
          SetGameActionActive(GAME_ACTION_MOVE_RIGHT, true);
        }
        else if (event.key.key == SDLK_LEFT)
        {
          SetGameActionActive(GAME_ACTION_MOVE_LEFT, true);
        }
        else if (event.key.key == SDLK_Q)
        {
          bQuit = true;
        }
      }
      else if (event.type == SDL_EVENT_KEY_UP)
      {
        if (event.key.key == SDLK_RIGHT)
        {
          SetGameActionActive(GAME_ACTION_MOVE_RIGHT, false);
        }
        else if (event.key.key == SDLK_LEFT)
        {
          SetGameActionActive(GAME_ACTION_MOVE_LEFT, false);
        }
      }
    }
#endif
    if (IsGameActionActive(GAME_ACTION_MOVE_RIGHT))
      pOAMEntries->entries[4].attr1.x += 2;
    else if (IsGameActionActive(GAME_ACTION_MOVE_LEFT))
      pOAMEntries->entries[4].attr1.x -= 2;

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

      // Pre-sort the tiles by y, to only consider the ones that will be on this line
      OAMEntry *entries[128];
      ObjDimensions dimensions[128];
      size_t nEntryCount = 0;
      for (int8_t nEntry = 127; nEntry >= 0; nEntry--)
      {
        dimensions[nEntryCount] = GetTileDimensions(pOAMEntries->entries[nEntry].attr0.obj_shape, pOAMEntries->entries[nEntry].attr1.obj_size);
        // TODO: Get tile height based on attributes and mode
        bool bWillDraw = true;
        if (pOAMEntries->entries[nEntry].attr0.rot_scale_on == false && pOAMEntries->entries[nEntry].attr0.double_size_on_obj_disable)
        {
          bWillDraw = false;
        }

        if (bWillDraw && y >= pOAMEntries->entries[nEntry].attr0.y && y < pOAMEntries->entries[nEntry].attr0.y + dimensions[nEntryCount].nHeight)
        {
          entries[nEntryCount] = &pOAMEntries->entries[nEntry];
          nEntryCount++;
        }
      }

      // countof OAMEntries
      for (int nEntry = 0; nEntry < nEntryCount; nEntry++)
      {
        size_t x_tiles = dimensions[nEntry].nWidth / 8;
        size_t y_tiles = dimensions[nEntry].nHeight / 8;

        // Y position within the tile
        int y_rel_tile = y - entries[nEntry]->attr0.y;

        size_t cur_y_tile = 0;
        if (y_rel_tile > 0)
        {
          cur_y_tile = y_rel_tile / 8;
        }
        y_rel_tile = y_rel_tile % 8;

        // Offset in number of tiles from the first,
        // caused due to y increase
        size_t tile_offset = cur_y_tile * x_tiles;

        for (uint8_t x_tile = 0; x_tile < x_tiles; x_tile++)
        {
          TileInfo tileInfo;
          tileInfo.idx = entries[nEntry]->attr2.number + tile_offset + x_tile;
          tileInfo.palNumber = entries[nEntry]->attr2.palette_num;
          renderRowOfTileWithPalette(pVram, pPaletteRam, &tileInfo, true, y_rel_tile, &linebuffer[entries[nEntry]->attr1.x + x_tile * 8]);
        }
      }

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
    SDL_RenderTexture(pRenderer, pTexture, NULL, NULL);

#ifdef USE_SDL
    SDL_RenderPresent(pRenderer);
    // Prevent rendering and logic to cap it to 60 FPS
    // usleep(16666);
    SDL_DelayNS(16666666);
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