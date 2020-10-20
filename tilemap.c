/*
 * Copyright 2020 paulguy <paulguy119@gmail.com>
 *
 * This file is part of crustygame.
 *
 * crustygame is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * crustygame is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with crustygame.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdlib.h>

#include <SDL.h>

#include "tilemap.h"

#define FUDGE (0.0001)

#define LOG_PRINTF(LL, FMT, ...) \
    (LL)->log_cb((LL)->log_priv, \
    FMT, \
    ##__VA_ARGS__)

#define FLOAT_COMPARE(X, Y) ((X - FUDGE < Y) && (X + FUDGE > Y))

const SDL_Point ZEROZERO = {.x = 0, .y = 0};

typedef struct {
    SDL_Texture *tex;
    unsigned int tw;
    unsigned int th;
    unsigned int maxx;
    unsigned int max;

    unsigned int refs; /* tilemaps referencing this tileset */
} Tileset;

typedef struct {
    int tileset;
    unsigned int w;
    unsigned int h;
    unsigned int *map;
    unsigned int *attr_flags;
    Uint32 *attr_colormod;
    SDL_Texture *tex; /* cached surface */

    unsigned int refs; /* layers referencing this tileset */
} Tilemap;

typedef struct {
    int tilemap;
    int x;
    int y;
    int scroll_x;
    int scroll_y;
    int w;
    int h;
    double scale_x;
    double scale_y;
    double angle;
    Uint32 colormod;
    SDL_BlendMode blendMode;
} Layer;

typedef struct LayerList_t {
    SDL_Renderer *renderer;
    Uint32 format;
    layerlist_log_cb_t log_cb;
    void *log_priv;
    
    Tileset *tileset;
    unsigned int tilesetsmem;

    Tilemap *tilemap;
    unsigned int tilemapsmem;

    Layer *layer;
    unsigned int layersmem;

    int blendWarned;
} LayerList;

static unsigned int find_power_of_two(unsigned int val) {
    unsigned int i;

    for(i = 1; i < val; i *= 2);

    return(i);
}

static int debug_show_texture(LayerList *ll,
                              SDL_Texture *texture) {
    SDL_Rect src, dest;
    Uint32 format;
    int access;

    if(SDL_QueryTexture(texture,
                        &format,
                        &access,
                        &src.w,
                        &src.h) < 0) {
        LOG_PRINTF(ll, "Couldn't query texture.\n");
        return(-1);
    }
    src.x = 0; src.y = 0; dest.x = 0; dest.y = 0;
    dest.w = src.w * 2; dest.h = src.h * 2;

    if(SDL_SetRenderDrawColor(ll->renderer, 0, 0, 0, SDL_ALPHA_OPAQUE) < 0) {
        LOG_PRINTF(ll, "Couldn't set render color.\n");
        return(-1);
    }

    if(SDL_RenderClear(ll->renderer) < 0) {
        LOG_PRINTF(ll, "Couldn't clear screen.\n");
        return(-1);
    }

    if(SDL_SetRenderDrawColor(ll->renderer,
                              0, 0, 0,
                              SDL_ALPHA_TRANSPARENT) < 0) {
        LOG_PRINTF(ll, "Couldn't restore render color.\n");
        return(-1);
    }

    if(SDL_RenderCopy(ll->renderer, texture, &src, &dest) < 0) {
        LOG_PRINTF(ll, "Couldn't render texture.\n");
        return(-1);
    }
    SDL_RenderPresent(ll->renderer);

    return(0);
}

#define DEBUG_SHOW_TEXTURE(LL, TEXTURE) \
    if(debug_show_texture(LL, TEXTURE) < 0) { \
        LOG_PRINTF(LL, "Couldn't show texture.\n"); \
    }

LayerList *layerlist_new(SDL_Renderer *renderer,
                         Uint32 format,
                         layerlist_log_cb_t log_cb,
                         void *log_priv) {
    LayerList *ll;

    ll = malloc(sizeof(LayerList));
    if(ll == NULL) {
        log_cb(log_priv, "Couldn't allocate memory for LayerList.\n");
        return(NULL);
    }

    ll->renderer = renderer;
    ll->format = format;
    ll->log_cb = log_cb;
    ll->log_priv = log_priv;
    ll->tilesetsmem = 0;
    ll->tilemapsmem = 0;
    ll->layersmem = 0;
    ll->blendWarned = 0;

    return(ll);
}

void layerlist_free(LayerList *ll) {
    unsigned int i;

    if(ll->tilesetsmem > 0) {
        for(i = 0; i < ll->tilesetsmem; i++) {
            if(ll->tileset[i].tex != NULL) {
                SDL_DestroyTexture(ll->tileset[i].tex);
            }
        }
        free(ll->tileset);
    }

    if(ll->tilemapsmem > 0) {
        for(i = 0; i < ll->tilemapsmem; i++) {
            if(ll->tilemap[i].map != NULL) {
                free(ll->tilemap[i].map);
                if(ll->tilemap[i].attr_flags != NULL) {
                    free(ll->tilemap[i].attr_flags);
                }
                if(ll->tilemap[i].attr_colormod != NULL) {
                    free(ll->tilemap[i].attr_colormod);
                }
                if(ll->tilemap[i].tex != NULL) {
                    SDL_DestroyTexture(ll->tilemap[i].tex);
                }
            }
        }
        free(ll->tilemap);
    }

    if(ll->layersmem > 0) {
        free(ll->layer);
    }

    free(ll);
}

int tilemap_add_tileset(LayerList *ll,
                        void *pixels,
                        unsigned int w,
                        unsigned int h,
                        unsigned int pitch,
                        unsigned int tw,
                        unsigned int th) {
    Tileset *temp;
    SDL_Surface *surface, *surface2;
    SDL_Texture *tex;
    unsigned int i, j;
    unsigned int maxx, maxy;
    unsigned int texw, texh;
    SDL_Rect src, dest;

    /* tiles should at least be 1x1 */
    if(tw == 0 || th == 0) {
        LOG_PRINTF(ll, "Tile dimensions are 0.\n");
        return(-1);
    }

    /* check if there would be 0 tiles */
    if(tw > w || th > h) {
        LOG_PRINTF(ll, "Tile dimensions greater than set.\n");
        return(-1);
    }

    maxx = w / tw;
    maxy = h / th;

    /* create the surface */
    surface = SDL_CreateRGBSurfaceFrom(pixels,
                                        w,
                                        h,
                                        32,
                                        pitch,
                                        TILEMAP_RMASK,
                                        TILEMAP_GMASK,
                                        TILEMAP_BMASK,
                                        TILEMAP_AMASK);
    if(surface == NULL) {
        LOG_PRINTF(ll, "Failed to create surface.\n");
        return(-1);
    }

    /* make sure the texture ends up being a power of two */
    texw = find_power_of_two(w);
    texh = find_power_of_two(h);
    if(w != texw || h != texh) {
        surface2 = SDL_CreateRGBSurface(0,
                                        texw,
                                        texh,
                                        32,
                                        TILEMAP_RMASK,
                                        TILEMAP_GMASK,
                                        TILEMAP_BMASK,
                                        TILEMAP_AMASK);
        if(surface2 == NULL) {
            LOG_PRINTF(ll, "Failed to create power of two surface.\n");
            SDL_FreeSurface(surface);
            return(-1);
        }
        src.x = 0; src.y = 0; src.w = surface->w; src.h = surface->h;
        dest.x = 0; dest.y = 0; dest.w = surface2->w; dest.h = surface2->h;
        if(SDL_BlitSurface(surface, &src, surface2, &dest) < 0) {
            LOG_PRINTF(ll, "Failed to copy to power of two surface.\n");
            SDL_FreeSurface(surface);
            SDL_FreeSurface(surface2);
        }
        SDL_FreeSurface(surface);
        surface = surface2;
    }

    /* create the texture */
    tex = SDL_CreateTextureFromSurface(ll->renderer, surface);
    if(tex == NULL) {
        LOG_PRINTF(ll, "Failed to create texture from surface.\n");
        SDL_FreeSurface(surface);
        return(-1);
    }
    SDL_FreeSurface(surface);

    /* make values overwrite existing values */
    if(SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE) < 0) {
        LOG_PRINTF(ll, "Failed to set blend mode.\n");
        SDL_DestroyTexture(tex);
        return(-1);
    }
 
    /* first loaded surface, so do some initial setup */
    if(ll->tilesetsmem == 0) {
        ll->tileset = malloc(sizeof(Tileset));
        if(ll->tileset == NULL) {
            LOG_PRINTF(ll, "Failed to allocate tileset.\n");
            SDL_DestroyTexture(tex);
            return(-1);
        }
        ll->tilesetsmem = 1;
        ll->tileset[0].tex = tex;
        ll->tileset[0].tw = tw;
        ll->tileset[0].th = th;
        ll->tileset[0].maxx = maxx;
        ll->tileset[0].max = maxx * maxy;
        ll->tileset[0].refs = 0;
        return(0);
    }

    /* find first NULL surface and assign it */
    for(i = 0; i < ll->tilesetsmem; i++) {
        if(ll->tileset[i].tex == NULL) {
            ll->tileset[i].tex = tex;
            ll->tileset[i].tw = tw;
            ll->tileset[i].th = th;
            ll->tileset[i].maxx = maxx;
            ll->tileset[i].max = maxx * maxy;
            ll->tileset[i].refs = 0;
            return(i);
        }
    }

    /* expand buffer if there's no free slots */
    temp = realloc(ll->tileset,
            sizeof(Tileset) * ll->tilesetsmem * 2);
    if(temp == NULL) {
        LOG_PRINTF(ll, "Failed to allocate tileset.\n");
        SDL_DestroyTexture(tex);
        return(-1);
    }
    ll->tileset = temp;
    ll->tilesetsmem *= 2;
    ll->tileset[i].tex = tex;
    ll->tileset[i].tw = tw;
    ll->tileset[i].th = th;
    ll->tileset[i].maxx = maxx;
    ll->tileset[i].max = maxx * maxy;
    ll->tileset[i].refs = 0;
    /* initialize empty excess surfaces as NULL */
    for(j = i + 1; j < ll->tilesetsmem; j++) {
        ll->tileset[j].tex = NULL;
    }
 
    return(i);
}

int tilemap_free_tileset(LayerList *ll, unsigned int index) {
    /* can't free a vacant slot, nor one with open references */
    if(index >= ll->tilesetsmem ||
       ll->tileset[index].tex == NULL ||
       ll->tileset[index].refs > 0) {
        LOG_PRINTF(ll, "Invalid tileset index or index referenced.\n");
        return(-1);
    }

    SDL_DestroyTexture(ll->tileset[index].tex);
    ll->tileset[index].tex = NULL;

    return(0);
}

int tilemap_add_tilemap(LayerList *ll,
                        unsigned int w,
                        unsigned int h) {
    Tilemap *temp;
    unsigned int i, j;

    if(w == 0 || h == 0) {
        LOG_PRINTF(ll, "Tilemap must have area.\n");
        return(-1);
    }

    /* first created tilemap, so do some initial setup */
    if(ll->tilemapsmem == 0) {
        ll->tilemap = malloc(sizeof(Tilemap));
        if(ll->tilemap == NULL) {
            LOG_PRINTF(ll, "Failed to allocate first tilemap.\n");
            return(-1);
        }
        ll->tilemapsmem = 1;
        ll->tilemap[0].map = malloc(sizeof(unsigned int) * w * h);
        if(ll->tilemap[0].map == NULL) {
            LOG_PRINTF(ll, "Failed to allocate first tilemap map.\n");
            return(-1);
        }
        memset(ll->tilemap[0].map, 0,
               sizeof(unsigned int) * w * h);
        ll->tilemap[0].w = w;
        ll->tilemap[0].h = h;
        ll->tilemap[0].tileset = -1;
        ll->tilemap[0].tex = NULL;
        ll->tilemap[0].attr_flags = NULL;
        ll->tilemap[0].attr_colormod = NULL;
        ll->tilemap[0].refs = 0;
        return(0);
    }

    /* find first NULL surface and assign it */
    for(i = 0; i < ll->tilemapsmem; i++) {
        if(ll->tilemap[i].map == NULL) {
            ll->tilemap[i].map = malloc(sizeof(unsigned int) * w * h);
            if(ll->tilemap[i].map == NULL) {
                LOG_PRINTF(ll, "Failed to allocate tilemap map.\n");
                return(-1);
            }
            memset(ll->tilemap[i].map, 0,
                   sizeof(unsigned int) * w * h);
            ll->tilemap[i].w = w;
            ll->tilemap[i].h = h;
            ll->tilemap[i].tileset = -1;
            ll->tilemap[i].tex = NULL;
            ll->tilemap[i].attr_flags = NULL;
            ll->tilemap[i].attr_colormod = NULL;
            ll->tilemap[i].refs = 0;
            return(i);
        }
    }

    /* expand buffer if there's no free slots */
    temp = realloc(ll->tilemap,
            sizeof(Tilemap) * ll->tilemapsmem * 2);
    if(temp == NULL) {
        LOG_PRINTF(ll, "Failed to expand tilemap space.\n");
        return(-1);
    }
    ll->tilemap = temp;
    ll->tilemapsmem *= 2;
    ll->tilemap[i].map = malloc(sizeof(unsigned int) * w * h);
    if(ll->tilemap[i].map == NULL) {
        LOG_PRINTF(ll, "Failed to allocate expanded tilemap map.\n");
        return(-1);
    }
    memset(ll->tilemap[i].map, 0,
           sizeof(unsigned int) * w * h);
    ll->tilemap[i].w = w;
    ll->tilemap[i].h = h;
    ll->tilemap[i].tileset = -1;
    ll->tilemap[i].tex = NULL;
    ll->tilemap[i].attr_flags = NULL;
    ll->tilemap[i].attr_colormod = NULL;
    ll->tilemap[i].refs = 0;
    /* initialize empty excess surfaces as NULL */
    for(j = i + 1; j < ll->tilemapsmem; j++) {
        ll->tilemap[j].map = NULL;
    }
 
    return(i);
}

int tilemap_free_tilemap(LayerList *ll, unsigned int index) {
    /* can't free a vacant slot, nor one with open references */
    if(index >= ll->tilemapsmem ||
       ll->tilemap[index].map == NULL ||
       ll->tilemap[index].refs > 0) {
        LOG_PRINTF(ll, "Invalid tilemap index or tilemap is referenced.\n");
        return(-1);
    }

    /* decrement reference from tileset from this tilemap */
    if(ll->tilemap[index].tileset >= 0) {
        ll->tileset[ll->tilemap[index].tileset].refs--;
    }
    free(ll->tilemap[index].map);
    ll->tilemap[index].map = NULL;
    /* free any attribute layers */
    if(ll->tilemap[index].attr_flags != NULL) {
        free(ll->tilemap[index].attr_flags);
        ll->tilemap[index].attr_flags = NULL;
    }
    if(ll->tilemap[index].attr_colormod != NULL) {
       free(ll->tilemap[index].attr_colormod);
        ll->tilemap[index].attr_colormod = NULL;
    }
    /* clear cached surface */
    if(ll->tilemap[index].tex != NULL) {
        SDL_DestroyTexture(ll->tilemap[index].tex);
        ll->tilemap[index].tex = NULL;
    }

    return(0);
}

int tilemap_set_tilemap_tileset(LayerList *ll,
                                unsigned int index,
                                unsigned int tileset) {
    /* make sure index is a valid tilemap */
    if(index >= ll->tilemapsmem ||
       ll->tilemap[index].map == NULL) {
        LOG_PRINTF(ll, "Invalid tilemap index: %u\n", index);
        return(-1);
    }

    /* make sure tileset is a valid tileset */
    if(tileset >= ll->tilesetsmem ||
       ll->tileset[tileset].tex == NULL) {
        LOG_PRINTF(ll, "Invalid tileset index: %u\n", index);
        return(-1);
    }

    ll->tilemap[index].tileset = tileset;
    ll->tileset[tileset].refs++;

    return(0);
}

int tilemap_set_tilemap_map(LayerList *ll,
                            unsigned int index,
                            unsigned int x,
                            unsigned int y,
                            int pitch,
                            int w,
                            int h,
                            unsigned int *value,
                            unsigned int size) {
    unsigned int i;

    /* make sure index is a valid tilemap */
    if(index >= ll->tilemapsmem ||
       ll->tilemap[index].map == NULL) {
        LOG_PRINTF(ll, "Invalid tilemap index %u.\n", index);
        return(-1);
    }
    /* Allow passing in 0s to be filled in for the whole map size, allow a
     * 0 pitch to be specified to copy the same row over each line */
    if(pitch < 0) {
        pitch = ll->tilemap[index].w;
    }
    if(w <= 0) {
        w = ll->tilemap[index].w;
    }
    if(h <= 0) {
        h = ll->tilemap[index].h;
    }

    if((((h - 1) * pitch) + w) > size) {
        LOG_PRINTF(ll, "Buffer too small to hold tilemap.\n");
        return(-1);
    }

    /* make sure start coordinate and end position don't go out of
     * range */
    if(x > ll->tilemap[index].w ||
       y > ll->tilemap[index].h ||
       x + w > ll->tilemap[index].w ||
       y + h > ll->tilemap[index].h) {
        LOG_PRINTF(ll, "Position/size would expand outside of "
                       "tilemap. x:%d->%d, y:%d->%d\n",
                       x, w, y, h);
        return(-1);
    }

    for(i = 0; i < h; i++) {
        memcpy(&(ll->tilemap[index].map[ll->tilemap[index].w * (y + i) + x]),
               &(value[(pitch * i)]),
               sizeof(unsigned int) * w); 
    }

    return(0);
}

int tilemap_set_tilemap_attr_flags(LayerList *ll,
                                   unsigned int index,
                                   unsigned int x,
                                   unsigned int y,
                                   int pitch,
                                   int w,
                                   int h,
                                   unsigned int *value,
                                   unsigned int size) {
    unsigned int i;

    /* make sure index is a valid tilemap */
    if(index >= ll->tilemapsmem ||
       ll->tilemap[index].map == NULL) {
        LOG_PRINTF(ll, "Invalid tilemap index %u.\n", index);
        return(-1);
    }
    /* Allow passing in 0s to be filled in for the whole map size, allow a
     * 0 pitch to be specified to copy the same row over each line */
    if(pitch < 0) {
        pitch = ll->tilemap[index].w;
    }
    if(w <= 0) {
        w = ll->tilemap[index].w;
    }
    if(h <= 0) {
        h = ll->tilemap[index].h;
    }

    if(((h - 1) * pitch) + w > size) {
        LOG_PRINTF(ll, "Buffer too small to hold tilemap.\n");
        return(-1);
    }

    /* make sure start coordinate and end position don't go out of
     * range */
    if(x > ll->tilemap[index].w ||
       y > ll->tilemap[index].h ||
       x + w > ll->tilemap[index].w ||
       y + h > ll->tilemap[index].h) {
        LOG_PRINTF(ll, "Position/size would expand outside of "
                       "tilemap.\n");
        return(-1);
    }
    
    /* allocate space for an attribute map if one doesn't exist */
    if(ll->tilemap[index].attr_flags == NULL) {
        ll->tilemap[index].attr_flags =
            malloc(sizeof(unsigned int) *
                   ll->tilemap[index].w *
                   ll->tilemap[index].h);
        if(ll->tilemap[index].attr_flags == NULL) {
            LOG_PRINTF(ll, "Failed to allocate tilemap attribute map.\n");
            return(-1);
        }
        memset(ll->tilemap[index].attr_flags, 0,
               sizeof(unsigned int) * w * h);
    }
 
    for(i = 0; i < h; i++) {
        memcpy(&(ll->tilemap[index].attr_flags[ll->tilemap[index].w * (y + i) + x]),
               &(value[(pitch * i)]),
               sizeof(unsigned int) * w); 
    }

    return(0);
}

int tilemap_set_tilemap_attr_colormod(LayerList *ll,
                                      unsigned int index,
                                      unsigned int x,
                                      unsigned int y,
                                      int pitch,
                                      int w,
                                      int h,
                                      Uint32 *value,
                                      unsigned int size) {
    unsigned int i;

    /* make sure index is a valid tilemap */
    if(index >= ll->tilemapsmem ||
       ll->tilemap[index].map == NULL) {
        LOG_PRINTF(ll, "Invalid tilemap index %u.\n", index);
        return(-1);
    }
    /* Allow passing in 0s to be filled in for the whole map size, allow a
     * 0 pitch to be specified to copy the same row over each line */
    if(pitch < 0) {
        pitch = ll->tilemap[index].w;
    }
    if(w <= 0) {
        w = ll->tilemap[index].w;
    }
    if(h <= 0) {
        h = ll->tilemap[index].h;
    }

    if(((h - 1) * pitch) + w > size) {
        LOG_PRINTF(ll, "Buffer too small to hold tilemap.\n");
        return(-1);
    }

    /* make sure start coordinate and end position don't go out of
     * range */
    if(x > ll->tilemap[index].w ||
       y > ll->tilemap[index].h ||
       x + w > ll->tilemap[index].w ||
       y + h > ll->tilemap[index].h) {
        LOG_PRINTF(ll, "Position/size would expand outside of "
                       "tilemap.\n");
        return(-1);
    }
    
    /* allocate space for an attribute map if one doesn't exist */
    if(ll->tilemap[index].attr_colormod == NULL) {
        ll->tilemap[index].attr_colormod =
            malloc(sizeof(unsigned int) *
                   ll->tilemap[index].w *
                   ll->tilemap[index].h);
        if(ll->tilemap[index].attr_colormod == NULL) {
            LOG_PRINTF(ll, "Failed to allocate tilemap attribute map.\n");
            return(-1);
        }
        memset(ll->tilemap[index].attr_colormod, 0,
               sizeof(unsigned int) * w * h);
    }
 
    for(i = 0; i < h; i++) {
        memcpy(&(ll->tilemap[index].attr_colormod[ll->tilemap[index].w * (y + i) + x]),
               &(value[(pitch * i)]),
               sizeof(unsigned int) * w); 
    }

    return(0);
}

int tilemap_update_tilemap(LayerList *ll,
                           unsigned int index,
                           unsigned int x,
                           unsigned int y,
                           unsigned int w,
                           unsigned int h) {
    unsigned int i, j;
    SDL_Rect dest, src, finaldest;
    Tilemap *tilemap;
    Tileset *tileset;
    unsigned int attr;
    Uint32 colormod;
    double angle;
    SDL_RendererFlip flip;
    unsigned int texw, texh;

    /* make sure index is a valid tilemap */
    if(index >= ll->tilemapsmem ||
       ll->tilemap[index].map == NULL) {
        LOG_PRINTF(ll, "Invalid tilemap index: %s\n");
        return(-1);
    }
    tilemap = &(ll->tilemap[index]);
    /* Allow passing in 0s to be filled in for the whole map size */
    if(w == 0) {
        w = ll->tilemap[index].w;
    }
    if(h == 0) {
        h = ll->tilemap[index].h;
    }

    /* make sure there's a tileset referenced */
    if(tilemap->tileset < 0) {
        LOG_PRINTF(ll, "Tilemap has no tileset.\n");
        return(-1);
    }
    tileset = &(ll->tileset[tilemap->tileset]);

    /* make sure the range specified is within the map */
    if(x > tilemap->w || x + w > tilemap->w ||
       y > tilemap->h || y + h > tilemap->h) {
        LOG_PRINTF(ll, "Dimensions extens outside of tilemap.\n");
        return(-1);
    }

    /* create the surface if it doesn't exist */
    if(tilemap->tex == NULL) {
        texw = find_power_of_two(tilemap->w * tileset->tw);
        texh = find_power_of_two(tilemap->h * tileset->th);

        tilemap->tex = SDL_CreateTexture(ll->renderer,
                                         ll->format,
                                         SDL_TEXTUREACCESS_STATIC |
                                         SDL_TEXTUREACCESS_TARGET,
                                         texw,
                                         texh);
        if(tilemap->tex == NULL) {
            LOG_PRINTF(ll, "Failed to create texture.\n");
            return(-1);
        }
        if(SDL_RenderClear(ll->renderer) < 0) {
            LOG_PRINTF(ll, "Failed to clear texture.\n");
            return(-1);
        }
    }
    /* set it to be rendered to */
    if(SDL_SetRenderTarget(ll->renderer, tilemap->tex) < 0) {
        LOG_PRINTF(ll, "Failed to set render target: %s.\n",
                       SDL_GetError());
        return(-1);
    }

    if(SDL_SetRenderDrawColor(ll->renderer,
                              0, 0, 0,
                              SDL_ALPHA_TRANSPARENT) < 0) {
        LOG_PRINTF(ll, "Failed to set render draw color.\n");
        return(-1);
    }
    dest.x = x * tileset->tw; dest.y = y * tileset->th;
    dest.w = w * tileset->tw; dest.h = h * tileset->th;
    if(SDL_RenderFillRect(ll->renderer, &dest) < 0) {
        LOG_PRINTF(ll, "Failed to clear region.\n");
        return(-1);
    }
    
    /* blit each tile to the tilemap */
    src.w = tileset->tw; src.h = tileset->th; src.y = 0;
    dest.w = src.w; dest.h = src.h;
    dest.x = dest.w * x; dest.y = dest.h * y;
    for(j = y; j < y + h; j++) {
        dest.x = dest.w * x;
        for(i = x; i < x + w; i++) {
            src.x = tilemap->map[tilemap->w * j + i];
            /* check to see if index is within tileset */
            if(src.x > tileset->max) {
                LOG_PRINTF(ll, "Tilemap index beyond tileset: %u\n", src.x);
                return(-1);
            }
            /* calculate the source texture coords and render */
            src.y = src.x / tileset->maxx;
            src.x %= tileset->maxx;
            src.x *= tileset->tw; src.y *= tileset->th;
            if(tilemap->attr_colormod) {
                colormod = tilemap->attr_colormod[tilemap->w * j + i];
                if(SDL_SetTextureColorMod(tileset->tex,
                        (colormod & TILEMAP_RMASK) >> TILEMAP_RSHIFT,
                        (colormod & TILEMAP_GMASK) >> TILEMAP_GSHIFT,
                        (colormod & TILEMAP_BMASK) >> TILEMAP_BSHIFT) < 0) {
                    fprintf(stderr, "Failed to set tile colormod.\n");
                    return(-1);
                }
                if(SDL_SetTextureAlphaMod(tileset->tex,
                        (colormod & TILEMAP_AMASK) >> TILEMAP_ASHIFT) < 0) {
                    fprintf(stderr, "Failed to set tile alphamod.\n");
                    return(-1);
                }
            }
            if(tilemap->attr_flags &&
               tilemap->attr_flags[tilemap->w * j + i] != 0) {
                attr = tilemap->attr_flags[tilemap->w * j + i];
                memcpy(&finaldest, &dest, sizeof(SDL_Rect));
                flip = SDL_FLIP_NONE;
                if(attr & TILEMAP_HFLIP_MASK) {
                    flip |= SDL_FLIP_HORIZONTAL;
                }
                if(attr & TILEMAP_VFLIP_MASK) {
                    flip |= SDL_FLIP_VERTICAL;
                }
                if((attr & TILEMAP_ROTATE_MASK) == TILEMAP_ROTATE_NONE) {
                    angle = 0.0;
                } else if((attr & TILEMAP_ROTATE_MASK) == TILEMAP_ROTATE_90) {
                    if(tileset->tw != tileset->th) {
                        LOG_PRINTF(ll, "Invalid rotation for rectangular "
                                       "tilemap.\n");
                        return(-1);
                    }
                    angle = 90.0;
                    finaldest.x += tileset->tw;
                } else if((attr & TILEMAP_ROTATE_MASK) == TILEMAP_ROTATE_180) {
                    angle = 180.0;
                    finaldest.x += tileset->tw;
                    finaldest.y += tileset->th;
                } else { /* TILEMAP_ROTATE_270 */
                     if(tileset->tw != tileset->th) {
                        LOG_PRINTF(ll, "Invalid rotation for rectangular "
                                       "tilemap.\n");
                        return(-1);
                    }
                    angle = 270.0;
                    finaldest.y += tileset->th;
                }
                if(SDL_RenderCopyEx(ll->renderer,
                                    tileset->tex,
                                    &src,
                                    &finaldest,
                                    angle,
                                    &ZEROZERO,
                                    flip) < 0) {
                    LOG_PRINTF(ll, "Failed to render tile.\n");
                    return(-1);
                }
            } else {
                if(SDL_RenderCopy(ll->renderer,
                                  tileset->tex,
                                  &src,
                                  &dest) < 0) {
                    LOG_PRINTF(ll, "Failed to render tile.\n");
                    return(-1);
                }
            }
            if(tilemap->attr_colormod) {
                if(SDL_SetTextureColorMod(tileset->tex, 255, 255, 255) < 0) {
                    fprintf(stderr, "Failed to set tile colormod.\n");
                    return(-1);
                }
                if(SDL_SetTextureAlphaMod(tileset->tex, 255) < 0) {
                    fprintf(stderr, "Failed to set tile alphamod.\n");
                    return(-1);
                }
            }
            dest.x += dest.w;
        }
        dest.y += dest.h;
    }

    /* restore default render target */
    if(SDL_SetRenderTarget(ll->renderer, NULL) < 0) {
        LOG_PRINTF(ll, "Failed to restore default render target.\n");
        return(-1);
    }

    return(0);
}

int tilemap_add_layer(LayerList *ll,
                      unsigned int tilemap) {
    Layer *temp;
    unsigned int i, j;
    Tilemap *tm;
    Tileset *ts;

    if(tilemap >= ll->tilemapsmem ||
       ll->tilemap[tilemap].map == NULL) {
        LOG_PRINTF(ll, "Invalid tilemap index.\n");
        return(-1);
    }
    tm = &(ll->tilemap[tilemap]);
    ts = &(ll->tileset[tm->tileset]);

    /* first created layer, so do some initial setup */
    if(ll->layersmem == 0) {
        ll->layer = malloc(sizeof(Layer));
        if(ll->layer == NULL) {
            LOG_PRINTF(ll, "Failed to allocate first layer.\n");
            return(-1);
        }
        ll->layersmem = 1;
        ll->layer[0].x = 0;
        ll->layer[0].y = 0;
        ll->layer[0].w = tm->w * ts->tw;
        ll->layer[0].h = tm->h * ts->th;
        ll->layer[0].scroll_x = 0;
        ll->layer[0].scroll_y = 0;
        ll->layer[0].scale_x = 1.0;
        ll->layer[0].scale_y = 1.0;
        ll->layer[0].angle = 0.0;
        ll->layer[0].colormod = TILEMAP_COLOR(255, 255, 255, 255);
        ll->layer[0].blendMode = SDL_BLENDMODE_BLEND;
        ll->layer[0].tilemap = tilemap;
        return(0);
    }

    /* find first NULL surface and assign it */
    for(i = 0; i < ll->layersmem; i++) {
        if(ll->layer[i].tilemap == -1) {
            ll->layer[i].x = 0;
            ll->layer[i].y = 0;
            ll->layer[i].w = tm->w * ts->tw;
            ll->layer[i].h = tm->h * ts->th;
            ll->layer[i].scroll_x = 0;
            ll->layer[i].scroll_y = 0;
            ll->layer[i].scale_x = 1.0;
            ll->layer[i].scale_y = 1.0;
            ll->layer[i].angle = 0.0;
            ll->layer[i].colormod = TILEMAP_COLOR(255, 255, 255, 255);
            ll->layer[i].blendMode = SDL_BLENDMODE_BLEND;
            ll->layer[i].tilemap = tilemap;
            return(i);
        }
    }

    /* expand buffer if there's no free slots */
    temp = realloc(ll->layer,
            sizeof(Layer) * ll->layersmem * 2);
    if(temp == NULL) {
        LOG_PRINTF(ll, "Failed to expand layer memory.\n");
        return(-1);
    }
    ll->layer = temp;
    ll->layersmem *= 2;
    ll->layer[i].x = 0;
    ll->layer[i].y = 0;
    ll->layer[i].w = tm->w * ts->tw;
    ll->layer[i].h = tm->h * ts->th;
    ll->layer[i].scroll_x = 0;
    ll->layer[i].scroll_y = 0;
    ll->layer[i].scale_x = 1.0;
    ll->layer[i].scale_y = 1.0;
    ll->layer[i].angle = 0.0;
    ll->layer[i].colormod = TILEMAP_COLOR(255, 255, 255, 255);
    ll->layer[i].blendMode = SDL_BLENDMODE_BLEND;
    ll->layer[i].tilemap = tilemap;
    /* initialize empty excess surfaces as NULL */
    for(j = i + 1; j < ll->layersmem; j++) {
        ll->layer[j].tilemap = -1;
    }
 
    return(i);
}

int tilemap_free_layer(LayerList *ll, unsigned int index) {
    /* can't free a vacant slot */
    if(index >= ll->layersmem ||
       ll->layer[index].tilemap == -1) {
        LOG_PRINTF(ll, "Invalid layer index.\n");
        return(-1);
    }

    ll->layer[index].tilemap = -1;

    return(0);
}

int tilemap_set_layer_pos(LayerList *ll, unsigned int index, int x, int y) {
    if(index >= ll->layersmem ||
       ll->layer[index].tilemap == -1) {
        LOG_PRINTF(ll, "Invalid layer index.\n");
        return(-1);
    }

    ll->layer[index].x = x;
    ll->layer[index].y = y;

    return(0);
}

int tilemap_set_layer_window(LayerList *ll, unsigned int index, int w, int h) {
    Layer *layer;
    Tilemap *tilemap;
    Tileset *tileset;
    unsigned int tmw, tmh;

    if(index >= ll->layersmem ||
       ll->layer[index].tilemap == -1) {
        LOG_PRINTF(ll, "Invalid layer index.\n");
        return(-1);
    }

    layer = &(ll->layer[index]);
    tilemap = &(ll->tilemap[layer->tilemap]);
    tileset = &(ll->tileset[tilemap->tileset]);
    tmw = tilemap->w * tileset->tw;
    tmh = tilemap->h * tileset->th;
    /* Allow passing in 0s to be reset to full size */
    if(w == 0) {
        w = tmw;
    }
    if(h == 0) {
        h = tmh;
    }

    if(w < 0 || h < 0 ||
       w > tmw || h > tmh) {
        LOG_PRINTF(ll, "Layer window out of range.\n");
        return(-1);
    }
 
    layer->w = w;
    layer->h = h;

    return(0);
}

int tilemap_set_layer_scroll_pos(LayerList *ll,
                                 unsigned int index,
                                 int scroll_x,
                                 int scroll_y) {
    Layer *layer;
    Tilemap *tilemap;
    Tileset *tileset;
    unsigned int tmw, tmh;

    if(index >= ll->layersmem ||
       ll->layer[index].tilemap == -1) {
        LOG_PRINTF(ll, "Invalid layer index.\n");
        return(-1);
    }

    layer = &(ll->layer[index]);
    tilemap = &(ll->tilemap[layer->tilemap]);
    tileset = &(ll->tileset[tilemap->tileset]);
    tmw = tilemap->w * tileset->tw;
    tmh = tilemap->h * tileset->th;

    if(scroll_x < 0 || scroll_y < 0 ||
       scroll_x > tmw - 1 || scroll_y > tmh - 1) {
        LOG_PRINTF(ll, "Layer scroll pos out of range.\n");
        return(-1);
    }
 
    layer->scroll_x = scroll_x;
    layer->scroll_y = scroll_y;

    return(0);
}

int tilemap_set_layer_scale(LayerList *ll,
                            unsigned int index,
                            double scale_x,
                            double scale_y) {
    /* SDL doesn't seem to allow negative rect coords and just clamps to 0 so
     * to avoid unexpected behavior, just throw an error to the user. */
    if(scale_x < 0.0 || scale_y < 0.0) {
        LOG_PRINTF(ll, "Negative X scale.\n");
        return(-1);
    }

    if(index >= ll->layersmem ||
       ll->layer[index].tilemap == -1) {
        LOG_PRINTF(ll, "Invalid layer index.\n");
        return(-1);
    }

    ll->layer[index].scale_x = scale_x;
    ll->layer[index].scale_y = scale_y;

    return(0);
}

int tilemap_set_layer_rotation(LayerList *ll, unsigned int index, double angle) {
    if(index >= ll->layersmem ||
       ll->layer[index].tilemap == -1) {
        LOG_PRINTF(ll, "Invalid layer index.\n");
        return(-1);
    }

    ll->layer[index].angle = angle;

    return(0);
}

int tilemap_set_layer_colormod(LayerList *ll, unsigned int index, Uint32 colormod) {
    if(index >= ll->layersmem ||
       ll->layer[index].tilemap == -1) {
        LOG_PRINTF(ll, "Invalid layer index.\n");
        return(-1);
    }

    ll->layer[index].colormod = colormod;

    return(0);
}

int tilemap_set_layer_blendmode(LayerList *ll, unsigned int index, int blendMode) {
    if(index >= ll->layersmem ||
       ll->layer[index].tilemap == -1) {
        LOG_PRINTF(ll, "Invalid layer index.\n");
        return(-1);
    }

    if(blendMode == TILEMAP_BLENDMODE_BLEND) {
        ll->layer[index].blendMode = SDL_BLENDMODE_BLEND;
    } else if(blendMode == TILEMAP_BLENDMODE_ADD) {
        ll->layer[index].blendMode = SDL_BLENDMODE_ADD;
    } else if(blendMode == TILEMAP_BLENDMODE_MOD) {
        ll->layer[index].blendMode = SDL_BLENDMODE_MOD;
    } else if(blendMode == TILEMAP_BLENDMODE_MUL) {
        ll->layer[index].blendMode = SDL_BLENDMODE_MUL;
    } else if(blendMode == TILEMAP_BLENDMODE_SUB) {
        ll->layer[index].blendMode =
            SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_SRC_ALPHA,
                                       SDL_BLENDFACTOR_ONE,
                                       SDL_BLENDOPERATION_REV_SUBTRACT,
                                       SDL_BLENDFACTOR_ZERO,
                                       SDL_BLENDFACTOR_ONE,
                                       SDL_BLENDOPERATION_ADD);
    } else {
        LOG_PRINTF(ll, "Invalid blend mode: %d\n", blendMode);
        return(-1);
    }

    return(0);
}
 
int tilemap_draw_layer(LayerList *ll, unsigned int index) {
    Tileset *tileset;
    Tilemap *tilemap;
    Layer *layer;
    SDL_Rect dest, src;
    unsigned int tmw, tmh;
    unsigned int right, bottom;
    int overRight, overBottom;
    int remainRight, remainBottom;
    
    /* Make sure it's a valid layer with graphics */
    if(index >= ll->layersmem ||
       ll->tilemap[ll->layer[index].tilemap].tex == NULL) {
        LOG_PRINTF(ll, "Invalid layer index or layer without graphics.\n");
        return(-1);
    }

    layer = &(ll->layer[index]);
    tilemap = &(ll->tilemap[layer->tilemap]);
    if(SDL_SetTextureColorMod(tilemap->tex,
            (layer->colormod & TILEMAP_RMASK) >> TILEMAP_RSHIFT,
            (layer->colormod & TILEMAP_GMASK) >> TILEMAP_GSHIFT,
            (layer->colormod & TILEMAP_BMASK) >> TILEMAP_BSHIFT) < 0) {
        fprintf(stderr, "Failed to set layer colormod.\n");
        return(-1);
    }
    if(SDL_SetTextureAlphaMod(tilemap->tex,
            (layer->colormod & TILEMAP_AMASK) >> TILEMAP_ASHIFT) < 0) {
        fprintf(stderr, "Failed to set tile alphamod.\n");
        return(-1);
    }

    if(SDL_SetTextureBlendMode(tilemap->tex, layer->blendMode) < 0) {
        if(ll->blendWarned == 0) {
            fprintf(stderr, "Failed to set layer blend mode, falling back to "
                            "SDL_BLENDMODE_BLEND, some things may appear "
                            "wrong. This warning will appear only once.\n");
            ll->blendWarned = 1;
        }
        SDL_SetTextureBlendMode(tilemap->tex, SDL_BLENDMODE_BLEND);
    }
    tileset = &(ll->tileset[tilemap->tileset]);
    tmw = tilemap->w * tileset->tw;
    tmh = tilemap->h * tileset->th;
    right = layer->scroll_x + layer->w;
    bottom = layer->scroll_y + layer->h;
    overRight = right - tmw;
    overBottom = bottom - tmh;
    remainRight = layer->w - overRight;
    remainBottom = layer->h - overBottom;

    src.x = layer->scroll_x;
    src.y = layer->scroll_y;
    src.w = overRight > 0 ? remainRight : layer->w;
    src.h = overBottom > 0 ? remainBottom : layer->h;
    dest.x = layer->x;
    dest.y = layer->y;
    dest.w = src.w * layer->scale_x;
    dest.h = src.h * layer->scale_y;
    if(FLOAT_COMPARE(layer->angle, 0.0)) {
        if(SDL_RenderCopy(ll->renderer,
                          tilemap->tex,
                          &src,
                          &dest) < 0) {
            LOG_PRINTF(ll, "Failed to render layer.\n");
            return(-1);
        }
        if(overRight > 0) {
            src.x = 0;
            src.y = layer->scroll_y;
            src.w = overRight;
            src.h = overBottom > 0 ? remainBottom : layer->h;
            dest.x = layer->x + (remainRight * layer->scale_x);
            dest.y = layer->y;
            dest.w = src.w * layer->scale_x;
            dest.h = src.h * layer->scale_y;
            if(SDL_RenderCopy(ll->renderer,
                              tilemap->tex,
                              &src,
                              &dest) < 0) {
                LOG_PRINTF(ll, "Failed to render layer.\n");
                return(-1);
            }
        }
        if(overBottom > 0) {
            src.x = layer->scroll_x;
            src.y = 0;
            src.w = overRight > 0 ? remainRight : layer->w;
            src.h = overBottom;
            dest.x = layer->x;
            dest.y = layer->y + (remainBottom * layer->scale_y);
            dest.w = src.w * layer->scale_x;
            dest.h = src.h * layer->scale_y;
            if(SDL_RenderCopy(ll->renderer,
                              tilemap->tex,
                              &src,
                              &dest) < 0) {
                LOG_PRINTF(ll, "Failed to render layer.\n");
                return(-1);
            }
        }
        if(overRight > 0 && overBottom > 0) {
            src.x = 0;
            src.y = 0;
            src.w = overRight;
            src.h = overBottom;
            dest.x = layer->x + (remainRight * layer->scale_x);
            dest.y = layer->y + (remainBottom * layer->scale_y);
            dest.w = src.w * layer->scale_x;
            dest.h = src.h * layer->scale_y;
            if(SDL_RenderCopy(ll->renderer,
                              tilemap->tex,
                              &src,
                              &dest) < 0) {
                LOG_PRINTF(ll, "Failed to render layer.\n");
                return(-1);
            }
        }
    } else {
        if(SDL_RenderCopyEx(ll->renderer,
                            tilemap->tex,
                            &src,
                            &dest,
                            layer->angle,
                            &ZEROZERO,
                            SDL_FLIP_NONE) < 0) {
            LOG_PRINTF(ll, "Failed to render layer.\n");
            return(-1);
        }
    }

    return(0);
}
