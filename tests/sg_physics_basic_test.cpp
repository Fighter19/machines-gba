#include "sg_bodies_2d_internal.h"
#include "sg_world_2d_internal.h"
#include "sg_fixed_transform_2d_internal.h"

#include <stdio.h>
#include <unistd.h>

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

static void print_fixed(fixed value)
{
#ifndef GBA
  printf("%f", value.to_float());
#endif
}

static void print_vec2d(SGFixedVector2Internal vec2d)
{
  printf("Vec2D: {x: ");
  print_fixed(vec2d.x);
  printf(" y: ");
  print_fixed(vec2d.y);
  printf("}");
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
  while(!bQuit)
  {
#ifdef USE_SDL
    SDL_SetRenderDrawColor(pRenderer, 255, 255, 255, 255);
    SDL_RenderClear(pRenderer);
#endif

    bool bCollided = world.move_and_collide(&ball_body, SGFixedVector2Internal(fixed::from_int(0), fixed::from_int(1)), &body_collision_info);
    if (bCollided)
    {
      bQuit = bCollided;
    }
    printf("Ball: ");
    print_vec2d(ball_body.get_transform().get_origin());
    printf("\nFloor: ");
    print_vec2d(floor_body.get_transform().get_origin());
    printf("\n");

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
  }

#ifdef USE_SDL
  SDL_DestroyRenderer(pRenderer);
  SDL_DestroyWindow(pMainWindow);
  SDL_Quit();
#endif
  
  return 0;
}