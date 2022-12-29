/*
 * sdlapp.c -- App struct.
 *
 * Copyright (C) 2022 Trevor Last
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "sdlapp.h"
#include "font.h"
#include "util.h"
#include "gif/gif.h"

#include <math.h>


/** Size (in pixels) of background grid squares. */
static int const BACKGROUND_GRID_SIZE = 8;

/** Color for even-numbered background grid squares. */
static uint8_t const BACKGROUND_GRID_COLOR_A[3] = {0x64, 0x64, 0x64};
/** Color for odd-numbered background grid squares. */
static uint8_t const BACKGROUND_GRID_COLOR_B[3] = {0x90, 0x90, 0x90};


SDL_Rect _get_current_frame_rect(struct App const *app)
{
    struct Graphic const *const img = app->current_frame->data;
    int const img_scaled_h = img->height * app->view.transform.zoom;
    int const img_scaled_w = img->width * app->view.transform.zoom;
    SDL_Rect rect;
    rect.h = img_scaled_h;
    rect.w = img_scaled_w;
    rect.x = app->width / 2 - img_scaled_w / 2 + app->view.transform.offset_x;
    rect.y = app->height / 2 - img_scaled_h / 2 + app->view.transform.offset_y;
    return rect;
}

/** Generate the background grid texture. */
void _generate_bg_grid(struct App *app)
{
    SDL_Surface *grid_surf = SDL_CreateRGBSurfaceWithFormat(
        0, app->width, app->height, 32, SDL_PIXELFORMAT_RGBA32);

    Uint32 const grid_color_a = SDL_MapRGB(
        grid_surf->format,
        BACKGROUND_GRID_COLOR_A[0],
        BACKGROUND_GRID_COLOR_A[1],
        BACKGROUND_GRID_COLOR_A[2]);
    Uint32 const grid_color_b = SDL_MapRGB(
        grid_surf->format,
        BACKGROUND_GRID_COLOR_B[0],
        BACKGROUND_GRID_COLOR_B[1],
        BACKGROUND_GRID_COLOR_B[2]);

    SDL_FillRect(grid_surf, NULL, grid_color_a);
    for (int y = 0; y < (app->height/BACKGROUND_GRID_SIZE+1); ++y)
    {
        int const initial_x = (y % 2 == 1)? 0 : BACKGROUND_GRID_SIZE;
        for (int x = initial_x; x < app->width; x += BACKGROUND_GRID_SIZE*2)
        {
            SDL_Rect const rect = {
                .h = BACKGROUND_GRID_SIZE,
                .w = BACKGROUND_GRID_SIZE,
                .x = x,
                .y = y * BACKGROUND_GRID_SIZE
            };
            SDL_FillRect(grid_surf, &rect, grid_color_b);
        }
    }
    SDL_DestroyTexture(app->bg_texture);
    app->bg_texture = SDL_CreateTextureFromSurface(app->renderer, grid_surf);
    SDL_FreeSurface(grid_surf);
}

/** Returns true if the app is on the final frame, false otherwise. */
bool _is_app_on_final_frame(struct App const *app)
{
    return app->current_frame->next == app->images;
}


struct App app_new(GIF const *gif)
{
    struct App app;

    app.window = SDL_CreateWindow(
        "GIF View",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        gif->width, gif->height,
        SDL_WINDOW_RESIZABLE);
    if (app.window == NULL)
        fatal("Failed to create window: %s\n", SDL_GetError());

    app.renderer = SDL_CreateRenderer(
        app.window, -1, SDL_RENDERER_ACCELERATED);
    if (app.renderer == NULL)
        fatal("Failed to create renderer -- %s\n", SDL_GetError());

    app.bg_texture = NULL;

    app.paused_text = textrenderer_new(DEFAULT_FONT_PATH, DEFAULT_FONT_SIZE);
    if (app.paused_text->font == NULL)
        SDL_Log("Failed to load font: %s\n", TTF_GetError());
    app.looping_text = textrenderer_new(DEFAULT_FONT_PATH, DEFAULT_FONT_SIZE);
    if (app.looping_text->font == NULL)
        SDL_Log("Failed to load font: %s\n", TTF_GetError());
    app.playback_speed_text = textrenderer_new(DEFAULT_FONT_PATH, DEFAULT_FONT_SIZE);
    if (app.playback_speed_text->font == NULL)
        SDL_Log("Failed to load font: %s\n", TTF_GetError());
    textrenderer_set_text(app.paused_text, app.renderer, "Paused ?");
    textrenderer_set_text(app.looping_text, app.renderer, "Looping ?");
    textrenderer_set_text(
        app.playback_speed_text, app.renderer, "Playback Speed ?");

    SDL_GetWindowSize(app.window, &app.width, &app.height);

    app.view.running = true,
    app.view.shift_amount = 2.5 * BACKGROUND_GRID_SIZE,
    /* In feh, zooming in 3 times doubles the image's size.  Zooming is
     * equivalent to exponentiation (eg. 3 zoom ins gives
     * `n*2*2*2 = n * 2^3`).  Therefore our equation is `2 = m^3`.  Solving for
     * m gives us our multiplier */
    app.view.zoom_change_multiplier = 1.259921049894873,
    app.view.transform.offset_x = 0;
    app.view.transform.offset_y = 0;
    app.view.transform.zoom = 1.0;

    app.images = graphiclist_new(app.renderer, *gif);
    app.current_frame = app.images;
    app.timer = 0;
    app.full_time = 0;
    for (GraphicList curr = app.images; curr->next != app.images; curr = curr->next)
    {
        struct Graphic const *const img = curr->data;
        app.full_time += img->delay;
    }
    app_set_paused(&app, false);
    app_set_looping(&app, true);
    app_set_playback_speed(&app, 1.0);
    app.state_text_visible = false;

    _generate_bg_grid(&app);
    return app;
}

void app_free(struct App const *app)
{
    graphiclist_free(app->images);
    textrenderer_free(app->paused_text);
    SDL_DestroyTexture(app->bg_texture);
    SDL_DestroyRenderer(app->renderer);
    SDL_DestroyWindow(app->window);
}

void app_clear_screen(struct App *app)
{
    SDL_RenderCopy(app->renderer, app->bg_texture, NULL, NULL);
}

bool app_timer_increment(struct App *app)
{
    if (!viewer_should_timer_increment(&app->view))
        return false;
    bool advanced = false;
    app->timer = fmod(app->timer + app->view.playback_speed, app->full_time);
    for (
        struct Graphic const *image = app->current_frame->data;
        app->timer >= image->delay;
        image = app->current_frame->data)
    {
        if (_is_app_on_final_frame(app) && !app->view.looping)
            break;
        else
        {
            app_next_frame(app);
            advanced = true;
        }
    }
    return advanced;
}

void app_next_frame(struct App *app)
{
    struct Graphic const *const image = app->current_frame->data;
    app->timer -= image->delay;
    app->current_frame = app->current_frame->next;
}

void app_previous_frame(struct App *app)
{
    GraphicList current = app->current_frame;
    // TODO: Switch to doubly-linked lists to simplify.
    while (app->current_frame->next != current)
        app_next_frame(app);
    app->timer = 0;
}

void app_draw(struct App *app)
{
    struct Graphic const *const img = app->current_frame->data;
    SDL_Rect const position = _get_current_frame_rect(app);
    SDL_RenderCopy(app->renderer, img->texture, NULL, &position);
    if (app->state_text_visible)
    {
        SDL_Rect moved_looping_rect = app->looping_text->rect;
        moved_looping_rect.y += app->paused_text->rect.h;
        SDL_Rect moved_playback_speed_rect = app->playback_speed_text->rect;
        moved_playback_speed_rect.y += moved_looping_rect.y + moved_looping_rect.h;
        SDL_RenderCopy(
            app->renderer, app->paused_text->texture,
            NULL, &app->paused_text->rect);
        SDL_RenderCopy(
            app->renderer, app->looping_text->texture,
            NULL, &moved_looping_rect);
        SDL_RenderCopy(
            app->renderer, app->playback_speed_text->texture,
            NULL, &moved_playback_speed_rect);
    }
    SDL_RenderPresent(app->renderer);
}

void app_resize(struct App *app, int width, int height)
{
    app->width  = width;
    app->height = height;
    viewer_transform_reset(&app->view);
    _generate_bg_grid(app);
}

void app_set_paused(struct App *app, bool paused)
{
    app->view.paused = paused;
    textrenderer_set_text(
        app->paused_text,
        app->renderer,
        app->view.paused? "paused TRUE" : "paused FALSE");
}

void app_set_looping(struct App *app, bool looping)
{
    app->view.looping = looping;
    textrenderer_set_text(
        app->looping_text,
        app->renderer,
        app->view.looping? "looping TRUE" : "looping FALSE");
}

void app_set_playback_speed(struct App *app, double playback_speed)
{
    static char const *const FMT = "Playback Speed %#g";
    app->view.playback_speed = playback_speed;
    int size = snprintf(NULL, 0, FMT, app->view.playback_speed);
    char *str = calloc(size + 1, 1);
    snprintf(str, size + 1, FMT, app->view.playback_speed);
    textrenderer_set_text(app->playback_speed_text, app->renderer, str);
    free(str);
}
