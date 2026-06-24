#include "sg_bodies_2d_internal.h"
#include "sg_world_2d_internal.h"
#include "sg_fixed_transform_2d_internal.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>

// Use for debugging the state of the physics world, visually.
// Set in CMake variables
#ifdef USE_SDL
#include <SDL2/SDL.h>
#define SDL_SUCCESS(x) (x == 0)
#endif

#ifdef GBA
#include <gba_base.h>
#include <gba_video.h>
#include <gba_systemcalls.h>
#include <gba_interrupt.h>

#include "../source/r6502_portfont_bin.h"
#include "../source/Common/ppu.h"
#include "../source/splash.h"

#include <array>

static volatile OAMEntries *pOAMEntries = (OAMEntries*)0x07000000;
#else
static OAMEntries *pOAMEntries = NULL;
#endif

static void print_msg(char *msg)
{
#ifndef GBA
  puts(msg);
#endif
}

static void print_fixed(fixed value)
{
#ifndef GBA
  printf("%f", value.to_float());
#endif
}

static void print_vec2d(SGFixedVector2Internal vec2d)
{
#ifndef GBA
  printf("Vec2D: {x: ");
  print_fixed(vec2d.x);
  printf(" y: ");
  print_fixed(vec2d.y);
  printf("}");
#endif
}

#ifdef USE_SDL
static SDL_Rect SgToSdlRect(SGFixedRect2Internal sgRect)
{
  SDL_Rect rect;
  rect.x = sgRect.position.x.to_int();
  rect.y = sgRect.position.y.to_int();
  rect.w = sgRect.size.x.to_int();
  rect.h = sgRect.size.y.to_int();
  return rect;
}
#endif

const u16 palette[] = {
	RGB8(0x40,0x80,0xc0),
	RGB8(0xFF,0xFF,0xFF),
	RGB8(0xF5,0xFF,0xFF),
	RGB8(0xDF,0xFF,0xF2),
	RGB8(0xCA,0xFF,0xE2),
	RGB8(0xB7,0xFD,0xD8),
	RGB8(0x2C,0x4F,0x8B)
};

enum eTimerToUse
{
	TIMER0,
	TIMER1,
	TIMER2,
	TIMER3
};

static std::array<uint64_t, 4> gFunctionCycles = {0, 0, 0, 0};

template <size_t Timer>
class FunctionProfiler
{
public:
	FunctionProfiler()
	{
	}

	void Start()
	{
    gFunctionCycles[Timer] = 0;

		// Set TM0CNT_L to 0
		*(vu16 *)(REG_BASE + 0x100 + Timer * 4) = 0;

		// Activate timer by setting TM0CNT_H to 0x80
		*(vu16 *)(REG_BASE + 0x102 + Timer * 4) = 0x83;
	}

	void Stop()
	{
		// Read the cycle count
		vu16 endCycle = *(vu16 *)(REG_BASE + 0x100 + Timer * 4);

		// Deactivate timer by setting TM0CNT_H to 0
		*(vu16 *)(REG_BASE + 0x102 + Timer * 4) = 0;

		// // Check for overflow
		// if (endCycle < startCycle)
		// {
		// 	endCycle += 0x100000000;
		// }

		gFunctionCycles[Timer] += (uint64_t)(endCycle - startCycle) * 1024;
	}

private:
	vu16 startCycle = 0;
};

static char *g_pCurrentFB = (char*)VRAM;

static void RenderFillRect(SGFixedRect2Internal &rect, uint8_t color)
{
  int pos_x = rect.position.x.to_int();
  if (pos_x < 0)
  {
    pos_x = 0;
  }

  int dest_x = pos_x + rect.size.width.to_int();
  if (dest_x >= 240)
  {
    dest_x = 240;
  }

  if (pos_x >= 240)
  {
    pos_x = 239;
  }

  uint8_t line[240] = {0};
  for(uint8_t x = pos_x; x < dest_x; x+=1)
  {
    line[x] = color;
  }

  int pos_y = rect.position.y.to_int();
  if (pos_y < 0)
  {
    pos_y = 0;
  }

  int dest_y = pos_y + rect.size.height.to_int();
  if (dest_y >= 160)
  {
    dest_y = 160;
  }

  if (pos_y >= 160)
  {
    pos_y = 159;
  }

  uint16_t offset_total = 0;
  // No out-of-bounds check on y, should be enough space after
  offset_total = pos_y * 240 + pos_x;

  char *vramBytes = (char*)g_pCurrentFB;
  vramBytes += offset_total;

  for (int y = pos_y; y < dest_y; y++)
  {
    for (int x = pos_x; x < dest_x; x++)
    {
      vramBytes[x] = line[x];
    }
    // Offset per line
    vramBytes += 240;
  }
}

int main(int argc, char *argv[])
{
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

  SDL_Renderer *pRenderer = SDL_CreateRenderer(pMainWindow, -1, 0);
  if (!pRenderer)
  {
    fprintf(stderr, "Error occured while setting up renderer: %s\n", SDL_GetError());
    return -1;
  }
#endif

#ifdef GBA
  // Set up the interrupt handlers
	irqInit();
	// Enable Vblank Interrupt to allow VblankIntrWait
	irqEnable(IRQ_VBLANK);

	// Allow Interrupts
	REG_IME = 1;

  vu16 * temppointer = BG_COLORS;
	for(int i=0; i<7; i++) {
		*temppointer++ = palette[i];
	}

  const unsigned char *splash_bytes = bin2c_splash_cropped_bmp;
  const uint32_t nPosPicture = *(uint32_t*)(splash_bytes + 0x0a);
  const unsigned char *splash_actual_bytes = splash_bytes + nPosPicture;

  BG_COLORS[0] = RGB8(0xFF,0xDF,0xFF);
  BG_COLORS[1] = RGB8(0xFF,0x00,0x00);
  BG_COLORS[2] = RGB8(0X00,0xFF,0x00);

  // TODO: Toggle between 0x0000 and 0xa000 for framebuffer

  // CpuFastSet(r6502_portfont_bin, (u16*)VRAM,(r6502_portfont_bin_size/4) | COPY32);
  int count = 0;
  for (int color_idx = 0; color_idx < 256; color_idx++)
  {
    uint8_t b = splash_bytes[count+0x36];
    uint8_t g = splash_bytes[count+0x36+1];
    uint8_t r = splash_bytes[count+0x36+2];

    BG_COLORS[color_idx] = RGB8(r,g,b);

    // Padding to 4
    count += 4;
  }

  CpuFastSet(splash_actual_bytes, (u16*)VRAM,(240*160/4) | COPY32);
  sleep(1);
  CpuFastSet(splash_actual_bytes, (u16*)(VRAM+0xa000),(240*160/4) | COPY32);

  // pOAMEntries->entries[0].attr0.double_size_on_obj_disable = 0;
  // pOAMEntries->entries[0].attr0.obj_mode = OBJ_MODE_NORMAL;
  // pOAMEntries->entries[0].attr0.obj_shape = OBJ_SHAPE_SQUARE;
  // pOAMEntries->entries[0].attr0.y = 5;

  // pOAMEntries->entries[0].attr1.obj_size = 0;
  // pOAMEntries->entries[0].attr1.x = 5;

  // pOAMEntries->entries[0].attr2.number = 32;
  // pOAMEntries->entries[0].attr2.palette_num = 0;
  // pOAMEntries->entries[0].attr2.priority = 3;
  SetMode(MODE_4 | BG2_ON);
#endif

  // 128 is the default cell-size of the physics server, when used with Godot
  SGWorld2DInternal world(128);
  SGBody2DInternal floor_body(SGBody2DInternal::BODY_STATIC);
  SGRectangle2DInternal floor_rectangle(fixed::from_int(240/2), fixed::from_int(10/2));
  floor_body.add_shape(&floor_rectangle);
  world.add_body(&floor_body);

  floor_body.set_transform(SGFixedTransform2DInternal(fixed::ZERO, SGFixedVector2Internal(fixed::from_int(240/2), fixed::from_int(140) - fixed::from_int(10/2))));

  // GBA resolution 240x160, place the floor on the lower side of the display
  SGBody2DInternal ball_body(SGBody2DInternal::BODY_KINEMATIC);
  SGCircle2DInternal ball_shape(fixed::from_int(8));
  ball_body.add_shape(&ball_shape);
  world.add_body(&ball_body);

  SGWorld2DInternal::BodyCollisionInfo body_collision_info;
  bool bQuit = false;
#ifdef GBA
  // FunctionProfiler<0> function0;
#endif
  while(!bQuit)
  {
#ifdef USE_SDL
    SDL_SetRenderDrawColor(pRenderer, 255, 255, 255, 255);
    SDL_RenderClear(pRenderer);
#endif

#ifdef GBA
    // Wait until VBlank is entered to make sure we can safely draw
    VBlankIntrWait();
    // 280576 is 16.6ms / 60FPS (100% of available back-buffer counter)

    if (g_pCurrentFB == (char*)VRAM)
    {
      // VRAM contains stable image, draw to the second buffer
      SetMode(MODE_4 | BG2_ON);
      g_pCurrentFB = (char*)VRAM+0xa000;
    }
    else
    {
      // VRAM+0xa000 contains stable image, draw to first buffer
      g_pCurrentFB = (char*)VRAM;
      SetMode(MODE_4 | BG2_ON | LCDC_BITS::BACKBUFFER);
    }

    // Current buffer is the one that will be drawn to
    *(uint32_t*)g_pCurrentFB = 0;
    // CpuFastSet(g_pCurrentFB, g_pCurrentFB, FILL | COPY32 | (0xa000 / 4));
#endif

    // 17.5% on this function
    bool bCollided = world.move_and_collide(&ball_body, SGFixedVector2Internal(fixed::from_int(0), fixed::from_int(1)), &body_collision_info);
    if (bCollided)
    {
      bQuit = bCollided;
    }
#ifndef GBA
    printf("Ball: ");
    print_vec2d(ball_body.get_transform().get_origin());
    printf("\nFloor: ");
    print_vec2d(floor_body.get_transform().get_origin());
    printf("\n");
#endif

#ifdef USE_SDL
    SDL_Rect sdlFloorRect = SgToSdlRect(floor_body.get_bounds());
    SDL_SetRenderDrawColor(pRenderer, 255, 0, 0, 255);
    SDL_RenderFillRect(pRenderer, &sdlFloorRect);

    SDL_Rect ballRect = SgToSdlRect(ball_body.get_bounds());
    SDL_RenderDrawRect(pRenderer, &ballRect);

    SDL_RenderPresent(pRenderer);
    // Prevent rendering and logic to cap it to 60 FPS
    usleep(16666);
#endif

#ifdef GBA
    asm volatile("nop");

    // 6% on this
    SGFixedRect2Internal bounds1 = floor_body.get_bounds();
    SGFixedRect2Internal bounds2 = ball_body.get_bounds();

    // Critically slow: 16% of free time on rendering this
    RenderFillRect(bounds1, 0x1);
    RenderFillRect(bounds2, 0x2);
#endif
  }

#ifdef USE_SDL
  SDL_DestroyRenderer(pRenderer);
  SDL_DestroyWindow(pMainWindow);
  SDL_Quit();
#endif
  
  return 0;
}