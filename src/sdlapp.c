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
    textrenderer_set_text(app.paused_text, app.renderer, "paused ?");
    textrenderer_set_text(app.looping_text, app.renderer, "looping ?");

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
    app_set_paused(&app, false);
    app_set_looping(&app, true);
    app.playback_speed = 1.0;
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
    if (app->paused)
        return false;
    struct Graphic const *image = app->current_frame->data;
    app->timer++;
    if (app->timer * app->playback_speed >= image->delay)
    {
        if (!app->looping && app->current_frame->next == app->images)
            return false;
        app_next_frame(app);
        return true;
    }
    return false;
}

void app_next_frame(struct App *app)
{
    app->current_frame = app->current_frame->next;
    app->timer = 0;
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
        SDL_RenderCopy(
            app->renderer, app->paused_text->texture,
            NULL, &app->paused_text->rect);
        SDL_RenderCopy(
            app->renderer, app->looping_text->texture,
            NULL, &moved_looping_rect);
    }
    SDL_RenderPresent(app->renderer);
}

void app_resize(struct App *app, int width, int height)
{
    app->width  = width;
    app->height = height;
    /* Image is recentered on size changes. */
    app->view.transform.offset_x = 0;
    app->view.transform.offset_y = 0;
    _generate_bg_grid(app);
}

void app_set_paused(struct App *app, bool paused)
{
    app->paused = paused;
    textrenderer_set_text(
        app->paused_text,
        app->renderer,
        app->paused? "paused TRUE" : "paused FALSE");
}

void app_set_looping(struct App *app, bool looping)
{
    app->looping = looping;
    textrenderer_set_text(
        app->looping_text,
        app->renderer,
        app->looping? "looping TRUE" : "looping FALSE");
}
