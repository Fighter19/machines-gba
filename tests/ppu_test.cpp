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
  uint16_t x: 1;
  uint16_t b: 5;
  uint16_t g: 5;
  uint16_t r: 5;
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

static Palette *pPaletteRam = NULL;
static SDL_PixelFormat *s_pPixelFormat = NULL;

// Function to convert color to current preferred SDL format
static inline Uint32 palIndexToRGBA(bool is_obj, uint8_t bank, uint8_t idx)
{
  if (idx == 0)
  {
    // Return value include alpha channel for SDL
    return 0;
  }

  const PaletteRow *pRow = pPaletteRam->rows_obj;
  if (!is_obj)
  {
    pRow = pPaletteRam->rows_bg;
  }

  const ColorBGR &color = pRow[bank].colors[idx];
  return SDL_MapRGB(s_pPixelFormat, color.r, color.g, color.b);
}

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

  Uint32 pixelFormat = SDL_GetWindowPixelFormat(pMainWindow);
  s_pPixelFormat = SDL_AllocFormat(pixelFormat);
  if (!s_pPixelFormat)
  {
    fprintf(stderr, "Failed to obtain pixel format\n");
    return -1;
  }

  SDL_Renderer *pRenderer = SDL_CreateRenderer(pMainWindow, -1, 0);
  if (!pRenderer)
  {
    fprintf(stderr, "Error occured while setting up renderer: %s\n", SDL_GetError());
    return -1;
  }
#endif

  bool bQuit = false;
  while(!bQuit)
  {
#ifdef USE_SDL
    SDL_SetRenderDrawColor(pRenderer, 255, 255, 255, 255);
    SDL_RenderClear(pRenderer);
#endif

#ifdef USE_SDL
    SDL_RenderPresent(pRenderer);
    // Prevent rendering and logic to cap it to 60 FPS
    usleep(16666);
#endif
  }

#ifdef USE_SDL
  SDL_DestroyRenderer(pRenderer);
  SDL_DestroyWindow(pMainWindow);
  SDL_Quit();
#endif
  
  return 0;
}