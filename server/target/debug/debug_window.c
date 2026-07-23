#include <SDL.h>
#include "debug_window.h"
#include "server.h"
#include "uthash.h"
#include "server_bullets.h"

static SDL_Window* sdl_window = NULL;
static SDL_Renderer* sdl_renderer = NULL;

static int32_t camera_x = 0;
static int32_t camera_y = 0;
static int render_cnt = 0;
static int tile_size = 8;
static int slow_simulation = 0;
static long slow_time_origin = 0;
static long slow_time_base = 0;

#define RENDER_EVERY_NTH (8)
#define TILE_MOVE (128)
#define SCREEN_WIDTH (1024)
#define SCREEN_HEIGHT (768)
#define TILE_SIZE_MIN (2)
#define TILE_SIZE_MAX (32)
#define TILE_SIZE_BASE (8)
#define SLOW_SIMULATION_DIVIDER (10)

#define min(x, y) ((x < y) ? x : y)
#define max(x, y) ((x > y) ? x : y)

extern struct server_state_t server_state;

static void debug_window_set_zoom_keep_center(int new_tile_size)
{
    if (new_tile_size == tile_size)
    {
        return;
    }

    const double old_scale = (double)tile_size / (double)TILE_SIZE_BASE;
    const double new_scale = (double)new_tile_size / (double)TILE_SIZE_BASE;
    const double screen_center_x = (double)SCREEN_WIDTH / 2.0;
    const double screen_center_y = (double)SCREEN_HEIGHT / 2.0;

    const double world_center_x = (screen_center_x - (double)camera_x) / old_scale;
    const double world_center_y = (screen_center_y - (double)camera_y) / old_scale;

    tile_size = new_tile_size;

    camera_x = (int32_t)(screen_center_x - (world_center_x * new_scale));
    camera_y = (int32_t)(screen_center_y - (world_center_y * new_scale));
}

void debug_window_init()
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);

    sdl_window = SDL_CreateWindow(
        "Debug Window",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        0
    );

    sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(sdl_renderer);
    SDL_RenderPresent(sdl_renderer);
}

static void poll_events()
{
    SDL_Event e;

    while (SDL_PollEvent(&e))
    {
        if (e.type == SDL_QUIT)
        {
            server_state.running = 0;
        }
        if (e.type == SDL_KEYDOWN)
        {
            switch (e.key.keysym.sym)
            {
                case SDLK_d:
                {
                    camera_x -= TILE_MOVE;
                    break;
                }
                case SDLK_a:
                {
                    camera_x += TILE_MOVE;
                    break;
                }
                case SDLK_s:
                {
                    camera_y -= TILE_MOVE;
                    break;
                }
                case SDLK_w:
                {
                    camera_y += TILE_MOVE;
                    break;
                }
                case SDLK_LEFTBRACKET:
                {
                    debug_window_set_zoom_keep_center(max(TILE_SIZE_MIN, tile_size / 2));
                    break;
                }
                case SDLK_RIGHTBRACKET:
                {
                    debug_window_set_zoom_keep_center(min(TILE_SIZE_MAX, tile_size * 2));
                    break;
                }
                case SDLK_0:
                {
                    slow_simulation = !slow_simulation;
                    break;
                }
            }
        }
    }
}

void debug_window_frame()
{
    poll_events();

    render_cnt++;

    if (render_cnt < RENDER_EVERY_NTH)
        return;

    render_cnt = 0;

    SDL_RenderClear(sdl_renderer);

    int pix_per_chunk = MAP_CHUNK_SIZE * tile_size;

    int chunks_per_w = (SCREEN_WIDTH / pix_per_chunk) + 1;
    int chunks_per_h = (SCREEN_HEIGHT / pix_per_chunk) + 1;

    int min_chunk_x = max((-camera_x) / pix_per_chunk, 0);
    int max_chunk_x = min(min_chunk_x + chunks_per_w, server_state.map.map.width);
    int min_chunk_y = max((-camera_y) / pix_per_chunk, 0);
    int max_chunk_y = min(min_chunk_y + chunks_per_h, server_state.map.map.height);

    for (int y_c = min_chunk_y; y_c < max_chunk_y; y_c++)
    {
        for (int x_c = min_chunk_x; x_c < max_chunk_x; x_c++)
        {
            struct map_chunk_t* chunk = map_get_chunk(&server_state.map.map, x_c, y_c);
            block_t* b = chunk->data;

            for (int y = 0; y < MAP_CHUNK_SIZE; y++)
            {
                for (int x = 0; x < MAP_CHUNK_SIZE; x++)
                {
                    int x_a = x_c * MAP_CHUNK_SIZE + x;
                    int y_a = y_c * MAP_CHUNK_SIZE + y;

                    SDL_Rect rect;
                    rect.x = camera_x + x_a * tile_size;
                    rect.y = camera_y + y_a * tile_size;
                    rect.w = tile_size;
                    rect.h = tile_size;

                    if ((*b) & 0xFF)
                    {
                        block_t block = strip_block_flags(*b);
                        srand(block);

                        SDL_SetRenderDrawColor(sdl_renderer, rand() % 256, rand() % 256, rand() % 256, 255);
                        SDL_RenderFillRect(sdl_renderer, &rect);
                    }

                    b++;
                }
            }
        }
    }

    {
        struct server_object_reference_t* elem = NULL;
        struct server_object_reference_t* tmp;

        HASH_ITER(hh, server_state.map.objects, elem, tmp)
        {
            int x = camera_x + ((int)elem->object.location.x * tile_size) / TILE_SIZE_BASE;
            int y = camera_y + ((int)elem->object.location.y * tile_size) / TILE_SIZE_BASE;
            int target_x = camera_x + ((int)elem->object.target.x * tile_size) / TILE_SIZE_BASE;
            int target_y = camera_y + ((int)elem->object.target.y * tile_size) / TILE_SIZE_BASE;

            if (elem->object.type & MAP_OBJECT_PLAYER)
            {
                SDL_SetRenderDrawColor(sdl_renderer, 0, 255, 0, 128);
            }
            else
            {
                SDL_SetRenderDrawColor(sdl_renderer, 255, 0, 0, 128);
            }

            SDL_Rect rect;
            rect.x = x;
            rect.y = y;
            rect.w = tile_size;
            rect.h = tile_size;

            SDL_RenderFillRect(sdl_renderer, &rect);

            SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 255, 255);
            SDL_RenderDrawRect(sdl_renderer, &rect);

            SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 255, 128);
            for (uint8_t i = 0; i < OBJECT_PREDICTION_FRAMES; i++)
            {
                SDL_Rect prediction_rect;
                prediction_rect.x = camera_x + ((int)elem->predictions[i].x * tile_size) / TILE_SIZE_BASE;
                prediction_rect.y = camera_y + ((int)elem->predictions[i].y * tile_size) / TILE_SIZE_BASE;
                prediction_rect.w = tile_size;
                prediction_rect.h = tile_size;
                SDL_RenderDrawRect(sdl_renderer, &prediction_rect);
            }

            SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 255, 255);

            {
                SDL_Rect target_rect;
                target_rect.x = target_x;
                target_rect.y = target_y;
                target_rect.w = tile_size;
                target_rect.h = tile_size;
                SDL_RenderDrawRect(sdl_renderer, &target_rect);
            }

            {
                int center_x = x + (tile_size / 2);
                int center_y = y + (tile_size / 2);
                int speed_scale = tile_size;

                SDL_RenderDrawLine(
                    sdl_renderer,
                    center_x,
                    center_y,
                    center_x + (elem->object.speed.x * speed_scale),
                    center_y + (elem->object.speed.y * speed_scale));
            }
        }
    }

    SDL_SetRenderDrawColor(sdl_renderer, 255, 0, 0, 255);

    {
        struct server_bullet_t* elem = NULL;

        DL_FOREACH(server_state.bullets, elem)
        {
            uint16_t x = camera_x + ((int)server_bullet_get_x(elem) * tile_size) / TILE_SIZE_BASE;
            uint16_t y = camera_y + ((int)server_bullet_get_y(elem) * tile_size) / TILE_SIZE_BASE;

            SDL_RenderDrawLine(
                sdl_renderer, x, y,
                x + ((((int)elem->dx >> BULLETS_PRECISION) * tile_size) / TILE_SIZE_BASE),
                y + ((((int)elem->dy >> BULLETS_PRECISION) * tile_size) / TILE_SIZE_BASE));
        }
    }


    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);

    SDL_RenderPresent(sdl_renderer);
}

long debug_window_process_time(long time)
{
    if (!slow_simulation)
    {
        slow_time_origin = time;
        slow_time_base = time;
        return time;
    }

    if (slow_time_origin == 0)
    {
        slow_time_origin = time;
        slow_time_base = time;
        return time;
    }

    return slow_time_base + ((time - slow_time_origin) / SLOW_SIMULATION_DIVIDER);
}

void debug_window_free()
{
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();
}
