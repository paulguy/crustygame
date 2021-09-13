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
    SDL_Point center;
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

static void init_tileset(Tileset *t,
                         SDL_Texture *tex,
                         unsigned int tw, unsigned int th,
                         unsigned int maxx, unsigned int maxy) {
    t->tex = tex;
    t->tw = tw;
    t->th = th;
    t->maxx = maxx;
    t->max = maxx * maxy;
    t->refs = 0;
}

static void add_tileset_ref(Tileset *ts) {
    ts->refs++;
}

static void free_tileset_ref(LayerList *ll, Tileset *ts) {
    if(ts->refs == 0) {
        LOG_PRINTF(ll, "WARNING: Attenpt to free reference to tileset with no references.\n");
        return;
    }

    ts->refs--;
}

static int do_tilemap_add_tileset(LayerList *ll,
                                  SDL_Surface *surface,
                                  unsigned int tw,
                                  unsigned int th) {
    Tileset *temp;
    SDL_Surface *surface2 = NULL;
    SDL_Texture *tex;
    unsigned int i, j;
    unsigned int maxx, maxy;
    unsigned int texw, texh;
    SDL_Rect src, dest;
    unsigned int w = surface->w;
    unsigned int h = surface->h;

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

    /* make sure the texture ends up being a power of two */
    texw = find_power_of_two(w);
    texh = find_power_of_two(h);
    if(w != texw || h != texh) {
        surface2 = SDL_CreateRGBSurface(0,
                                        texw,
                                        texh,
                                        32,
                                        surface->format->Rmask,
                                        surface->format->Gmask,
                                        surface->format->Bmask,
                                        surface->format->Amask);
        if(surface2 == NULL) {
            LOG_PRINTF(ll, "Failed to create power of two surface.\n");
            return(-1);
        }
        src.x = 0; src.y = 0; src.w = surface->w; src.h = surface->h;
        dest.x = 0; dest.y = 0; dest.w = surface2->w; dest.h = surface2->h;
        if(SDL_BlitSurface(surface, &src, surface2, &dest) < 0) {
            LOG_PRINTF(ll, "Failed to copy to power of two surface: %s.\n", SDL_GetError());
            SDL_FreeSurface(surface2);
            return(-1);
        }
    } else {
        surface2 = surface;
    }

    /* create the texture */
    tex = SDL_CreateTextureFromSurface(ll->renderer, surface2);
    /* if it's not this function's surface, don't free it */
    if(surface != surface2) {
        SDL_FreeSurface(surface2);
    }
    if(tex == NULL) {
        LOG_PRINTF(ll, "Failed to create texture from surface: %s.\n", SDL_GetError());
        return(-1);
    }

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
        init_tileset(&(ll->tileset[0]), tex, tw, th, maxx, maxy);
        return(0);
    }

    /* find first NULL surface and assign it */
    for(i = 0; i < ll->tilesetsmem; i++) {
        if(ll->tileset[i].tex == NULL) {
            init_tileset(&(ll->tileset[i]), tex, tw, th, maxx, maxy);
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
    init_tileset(&(ll->tileset[i]), tex, tw, th, maxx, maxy);
    /* initialize empty excess surfaces as NULL */
    for(j = i + 1; j < ll->tilesetsmem; j++) {
        ll->tileset[j].tex = NULL;
    }
 
    return(i);
}

int tilemap_add_tileset(LayerList *ll,
                        void *pixels,
                        unsigned int w,
                        unsigned int h,
                        unsigned int pitch,
                        unsigned int tw,
                        unsigned int th) {
    SDL_Surface *surface;
    int retval;

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

    retval = do_tilemap_add_tileset(ll, surface, tw, th);
    SDL_FreeSurface(surface);

    return(retval);
}

static Tileset *get_tileset(LayerList *ll, unsigned int index) {
    if(index >= ll->tilesetsmem ||
       ll->tileset[index].tex == NULL) {
        LOG_PRINTF(ll, "Invalid tileset index: %u\n", index);
        return(NULL);
    }

    return(&(ll->tileset[index]));
}

int tilemap_free_tileset(LayerList *ll, unsigned int index) {
    Tileset *ts = get_tileset(ll, index);
    if(ts == NULL) {
        return(-1);
    }
    if(ts->refs > 0) {
        LOG_PRINTF(ll, "Tileset index referenced.\n");
        return(-1);
    }

    SDL_DestroyTexture(ts->tex);
    ts->tex = NULL;

    return(0);
}

static int init_tilemap(LayerList *ll, Tilemap *t,
                        unsigned int tileset,
                        unsigned int w, unsigned int h) {
    t->map = malloc(sizeof(unsigned int) * w * h);
    if(t->map == NULL) {
        LOG_PRINTF(ll, "Failed to allocate first tilemap map.\n");
        return(-1);
    }
    memset(t->map, 0, sizeof(unsigned int) * w * h);
    t->w = w;
    t->h = h;
    t->tileset = tileset;
    t->tex = NULL;
    t->attr_flags = NULL;
    t->attr_colormod = NULL;
    t->refs = 0;

    return(0);
}

static void add_tilemap_ref(Tilemap *tm) {
    tm->refs++;
}

static void free_tilemap_ref(LayerList *ll, Tilemap *tm) {
    if(tm->refs == 0) {
        LOG_PRINTF(ll, "WARNING: Attenpt to free reference to tilemap with no references.\n");
        return;
    }

    tm->refs--;
}

int tilemap_add_tilemap(LayerList *ll,
                        unsigned int tileset,
                        unsigned int w,
                        unsigned int h) {
    Tilemap *temp;
    unsigned int i, j;
    Tileset *ts = get_tileset(ll, tileset);
    if(ts == NULL) {
        return(-1);
    }

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
        if(init_tilemap(ll, &(ll->tilemap[0]), tileset, w, h) < 0) {
            return(-1);
        }
        add_tileset_ref(ts);
        return(0);
    }

    /* find first NULL surface and assign it */
    for(i = 0; i < ll->tilemapsmem; i++) {
        if(ll->tilemap[i].map == NULL) {
            if(init_tilemap(ll, &(ll->tilemap[i]), tileset, w, h) < 0) {
                return(-1);
            }
            add_tileset_ref(ts);
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
    if(init_tilemap(ll, &(ll->tilemap[i]), tileset, w, h) < 0) {
        return(-1);
    }
    add_tileset_ref(ts);

    /* initialize empty excess surfaces as NULL */
    for(j = i + 1; j < ll->tilemapsmem; j++) {
        ll->tilemap[j].map = NULL;
    }
 
    return(i);
}

static Tilemap *get_tilemap(LayerList *ll, unsigned int index) {
    if(index >= ll->tilemapsmem ||
       ll->tilemap[index].map == NULL) {
        LOG_PRINTF(ll, "Invalid tilemap index: %u\n", index);
        return(NULL);
    }

    return(&(ll->tilemap[index]));
}

int tilemap_free_tilemap(LayerList *ll, unsigned int index) {
    Tilemap *tm = get_tilemap(ll, index);
    if(tm == NULL) {
        return(-1);
    }
    if(tm->refs > 0) {
        LOG_PRINTF(ll, "Tilemap index referenced.\n");
        return(-1);
    }

    Tileset *ts = get_tileset(ll, tm->tileset);
    if(ts == NULL) {
        return(-1);
    }
    free_tileset_ref(ll, ts);

    free(tm->map);
    tm->map = NULL;
    /* free any attribute layers */
    if(tm->attr_flags != NULL) {
        free(tm->attr_flags);
        tm->attr_flags = NULL;
    }
    if(tm->attr_colormod != NULL) {
       free(tm->attr_colormod);
        tm->attr_colormod = NULL;
    }
    /* clear cached surface */
    if(tm->tex != NULL) {
        SDL_DestroyTexture(tm->tex);
        tm->tex = NULL;
    }

    return(0);
}

int tilemap_set_tilemap_tileset(LayerList *ll,
                                unsigned int index,
                                unsigned int tileset) {
    Tilemap *tm = get_tilemap(ll, index);
    if(tm == NULL) {
        return(-1);
    }
    Tileset *oldts = get_tileset(ll, tm->tileset);
    if(oldts == NULL) {
        return(-1);
    }
    Tileset *newts = get_tileset(ll, tileset);
    if(newts == NULL) {
        return(-1);
    }

    /* free the old, invalid texture, because the tile size may have changed */
    if(tm->tex != NULL) {
        SDL_DestroyTexture(tm->tex);
        tm->tex = NULL;
    }

    free_tileset_ref(ll, oldts);
    add_tileset_ref(newts);
    tm->tileset = tileset;

    return(0);
}

int tilemap_set_tilemap_map(LayerList *ll,
                            unsigned int index,
                            unsigned int x,
                            unsigned int y,
                            int pitch,
                            int w,
                            int h,
                            const unsigned int *value,
                            unsigned int size) {
    unsigned int i;
    Tilemap *tm = get_tilemap(ll, index);
    if(tm == NULL) {
        return(-1);
    }

    /* Allow passing in 0s to be filled in for the whole map size, allow a
     * 0 pitch to be specified to copy the same row over each line */
    if(pitch < 0) {
        pitch = tm->w;
    }
    if(w <= 0) {
        w = tm->w;
    }
    if(h <= 0) {
        h = tm->h;
    }

    if(((((unsigned int)h - 1) * (unsigned int)pitch) +
        (unsigned int)w) > size) {
        LOG_PRINTF(ll, "Buffer too small to hold tilemap.\n");
        return(-1);
    }

    /* make sure start coordinate and end position don't go out of
     * range */
    if(x > tm->w || y > tm->h ||
       x + w > tm->w || y + h > tm->h) {
        LOG_PRINTF(ll, "Position/size would expand outside of "
                       "tilemap. x:%d->%d, y:%d->%d\n",
                       x, w, y, h);
        return(-1);
    }

    for(i = 0; i < (unsigned int)h; i++) {
        memcpy(&(tm->map[tm->w * (y + i) + x]),
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
                                   const unsigned int *value,
                                   unsigned int size) {
    unsigned int i;
    Tilemap *tm = get_tilemap(ll, index);
    if(tm == NULL) {
        return(-1);
    }

    /* Allow passing in 0s to be filled in for the whole map size, allow a
     * 0 pitch to be specified to copy the same row over each line */
    if(pitch < 0) {
        pitch = tm->w;
    }
    if(w <= 0) {
        w = tm->w;
    }
    if(h <= 0) {
        h = tm->h;
    }

    if(((((unsigned int)h - 1) * (unsigned int)pitch) +
        (unsigned int)w) > size) {
        LOG_PRINTF(ll, "Buffer too small to hold tilemap.\n");
        return(-1);
    }

    /* make sure start coordinate and end position don't go out of
     * range */
    if(x > tm->w || y > tm->h ||
       x + w > tm->w || y + h > tm->h) {
        LOG_PRINTF(ll, "Position/size would expand outside of "
                       "tilemap.\n");
        return(-1);
    }
    
    /* allocate space for an attribute map if one doesn't exist */
    if(tm->attr_flags == NULL) {
        tm->attr_flags = malloc(sizeof(unsigned int) * tm->w * tm->h);
        if(tm->attr_flags == NULL) {
            LOG_PRINTF(ll, "Failed to allocate tilemap attribute map.\n");
            return(-1);
        }
        memset(tm->attr_flags, 0, sizeof(unsigned int) * w * h);
    }
 
    for(i = 0; i < (unsigned int)h; i++) {
        memcpy(&(tm->attr_flags[tm->w * (y + i) + x]),
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
                                      const Uint32 *value,
                                      unsigned int size) {
    unsigned int i;
    Tilemap *tm = get_tilemap(ll, index);
    if(tm == NULL) {
        return(-1);
    }

    /* Allow passing in 0s to be filled in for the whole map size, allow a
     * 0 pitch to be specified to copy the same row over each line */
    if(pitch < 0) {
        pitch = tm->w;
    }
    if(w <= 0) {
        w = tm->w;
    }
    if(h <= 0) {
        h = tm->h;
    }

    if(((((unsigned int)h - 1) * (unsigned int)pitch) +
        (unsigned int)w) > size) {
        LOG_PRINTF(ll, "Buffer too small to hold tilemap.\n");
        return(-1);
    }

    /* make sure start coordinate and end position don't go out of
     * range */
    if(x > tm->w || y > tm->h ||
       x + w > tm->w || y + h > tm->h) {
        LOG_PRINTF(ll, "Position/size would expand outside of "
                       "tilemap.\n");
        return(-1);
    }
    
    /* allocate space for an attribute map if one doesn't exist */
    if(tm->attr_colormod == NULL) {
        tm->attr_colormod =
            malloc(sizeof(unsigned int) * tm->w * tm->h);
        if(tm->attr_colormod == NULL) {
            LOG_PRINTF(ll, "Failed to allocate tilemap attribute map.\n");
            return(-1);
        }
        memset(tm->attr_colormod, 0,
               sizeof(unsigned int) * w * h);
    }
 
    for(i = 0; i < (unsigned int)h; i++) {
        memcpy(&(tm->attr_colormod[tm->w * (y + i) + x]),
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
    unsigned int attr;
    Uint32 colormod;
    double angle;
    SDL_RendererFlip flip;
    unsigned int texw, texh;
    Tilemap *tm = get_tilemap(ll, index);
    if(tm == NULL) {
        return(-1);
    }
    Tileset *ts = get_tileset(ll, tm->tileset);
    if(ts == NULL) {
        return(-1);
    }

    /* Allow passing in 0s to be filled in for the whole map size */
    if(w == 0) {
        w = tm->w;
    }
    if(h == 0) {
        h = tm->h;
    }

    /* make sure the range specified is within the map */
    if(x > tm->w || x + w > tm->w ||
       y > tm->h || y + h > tm->h) {
        LOG_PRINTF(ll, "Dimensions extens outside of tilemap.\n");
        return(-1);
    }

    /* create the surface if it doesn't exist */
    if(tm->tex == NULL) {
        texw = find_power_of_two(tm->w * ts->tw);
        texh = find_power_of_two(tm->h * ts->th);

        tm->tex = SDL_CreateTexture(ll->renderer,
                                    ll->format,
                                    SDL_TEXTUREACCESS_STATIC |
                                    SDL_TEXTUREACCESS_TARGET,
                                    texw,
                                    texh);
        if(tm->tex == NULL) {
            LOG_PRINTF(ll, "Failed to create texture.\n");
            return(-1);
        }
        if(SDL_RenderClear(ll->renderer) < 0) {
            LOG_PRINTF(ll, "Failed to clear texture.\n");
            return(-1);
        }
    }
    /* set it to be rendered to */
    if(SDL_SetRenderTarget(ll->renderer, tm->tex) < 0) {
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
    dest.x = x * ts->tw; dest.y = y * ts->th;
    dest.w = w * ts->tw; dest.h = h * ts->th;
    if(SDL_RenderFillRect(ll->renderer, &dest) < 0) {
        LOG_PRINTF(ll, "Failed to clear region.\n");
        return(-1);
    }
    
    /* blit each tile to the tilemap */
    src.w = ts->tw; src.h = ts->th; src.y = 0;
    dest.w = src.w; dest.h = src.h;
    dest.x = dest.w * x; dest.y = dest.h * y;
    for(j = y; j < y + h; j++) {
        dest.x = dest.w * x;
        for(i = x; i < x + w; i++) {
            src.x = tm->map[tm->w * j + i];
            /* check to see if index is within tileset */
            /* src.x can't be negative, because tm->map is unsigned,
             * silences a warning */
            if((unsigned int)(src.x) > ts->max) {
                LOG_PRINTF(ll, "Tilemap index beyond tileset: %u\n", src.x);
                return(-1);
            }
            /* calculate the source texture coords and render */
            src.y = src.x / ts->maxx;
            src.x %= ts->maxx;
            src.x *= ts->tw; src.y *= ts->th;
            if(tm->attr_colormod) {
                colormod = tm->attr_colormod[tm->w * j + i];
                if(SDL_SetTextureColorMod(ts->tex,
                        (colormod & TILEMAP_RMASK) >> TILEMAP_RSHIFT,
                        (colormod & TILEMAP_GMASK) >> TILEMAP_GSHIFT,
                        (colormod & TILEMAP_BMASK) >> TILEMAP_BSHIFT) < 0) {
                    fprintf(stderr, "Failed to set tile colormod.\n");
                    return(-1);
                }
                if(SDL_SetTextureAlphaMod(ts->tex,
                        (colormod & TILEMAP_AMASK) >> TILEMAP_ASHIFT) < 0) {
                    fprintf(stderr, "Failed to set tile alphamod.\n");
                    return(-1);
                }
            }
            if(tm->attr_flags &&
               tm->attr_flags[tm->w * j + i] != 0) {
                attr = tm->attr_flags[tm->w * j + i];
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
                    if(ts->tw != ts->th) {
                        LOG_PRINTF(ll, "Invalid rotation for rectangular "
                                       "tilemap.\n");
                        return(-1);
                    }
                    angle = 90.0;
                    finaldest.x += ts->tw;
                } else if((attr & TILEMAP_ROTATE_MASK) == TILEMAP_ROTATE_180) {
                    angle = 180.0;
                    finaldest.x += ts->tw;
                    finaldest.y += ts->th;
                } else { /* TILEMAP_ROTATE_270 */
                     if(ts->tw != ts->th) {
                        LOG_PRINTF(ll, "Invalid rotation for rectangular "
                                       "tilemap.\n");
                        return(-1);
                    }
                    angle = 270.0;
                    finaldest.y += ts->th;
                }
                if(SDL_RenderCopyEx(ll->renderer,
                                    ts->tex,
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
                                  ts->tex,
                                  &src,
                                  &dest) < 0) {
                    LOG_PRINTF(ll, "Failed to render tile.\n");
                    return(-1);
                }
            }
            if(tm->attr_colormod) {
                if(SDL_SetTextureColorMod(ts->tex, 255, 255, 255) < 0) {
                    fprintf(stderr, "Failed to set tile colormod.\n");
                    return(-1);
                }
                if(SDL_SetTextureAlphaMod(ts->tex, 255) < 0) {
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

static void init_layer(Layer *l,
                       Tilemap *tm,
                       Tileset *ts,
                       unsigned int tilemap) {
    l->x = 0;
    l->y = 0;
    l->w = tm->w * ts->tw;
    l->h = tm->h * ts->th;
    l->scroll_x = 0;
    l->scroll_y = 0;
    l->scale_x = 1.0;
    l->scale_y = 1.0;
    l->center.x = 0;
    l->center.y = 0;
    l->angle = 0.0;
    l->colormod = TILEMAP_COLOR(255, 255, 255, 255);
    l->blendMode = SDL_BLENDMODE_BLEND;
    l->tilemap = tilemap;
}

int tilemap_add_layer(LayerList *ll, unsigned int tilemap) {
    Layer *temp;
    unsigned int i, j;
    Tilemap *tm = get_tilemap(ll, tilemap);
    if(tm == NULL) {
        return(-1);
    }
    Tileset *ts = get_tileset(ll, tm->tileset);
    if(ts == NULL) {
        return(-1);
    }

    /* first created layer, so do some initial setup */
    if(ll->layersmem == 0) {
        ll->layer = malloc(sizeof(Layer));
        if(ll->layer == NULL) {
            LOG_PRINTF(ll, "Failed to allocate first layer.\n");
            return(-1);
        }
        ll->layersmem = 1;
        init_layer(&(ll->layer[0]), tm, ts, tilemap);
        add_tilemap_ref(tm);
        return(0);
    }

    /* find first NULL surface and assign it */
    for(i = 0; i < ll->layersmem; i++) {
        if(ll->layer[i].tilemap == -1) {
            init_layer(&(ll->layer[i]), tm, ts, tilemap);
            add_tilemap_ref(tm);
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
    init_layer(&(ll->layer[i]), tm, ts, tilemap);
    add_tilemap_ref(tm);

    /* initialize empty excess surfaces as NULL */
    for(j = i + 1; j < ll->layersmem; j++) {
        ll->layer[j].tilemap = -1;
    }
 
    return(i);
}

static Layer *get_layer(LayerList *ll, unsigned int index) {
    if(index >= ll->layersmem ||
       ll->layer[index].tilemap == -1) {
        LOG_PRINTF(ll, "Invalid layer index.\n");
        return(NULL);
    }

    return(&(ll->layer[index]));
}

int tilemap_free_layer(LayerList *ll, unsigned int index) {
    Layer *l = get_layer(ll, index);
    if(l == NULL) {
        return(-1);
    }
    Tilemap *tm = get_tilemap(ll, l->tilemap);
    if(tm == NULL) {
        return(-1);
    }

    free_tilemap_ref(ll, tm);
    l->tilemap = -1;

    return(0);
}

int tilemap_set_layer_pos(LayerList *ll, unsigned int index, int x, int y) {
    Layer *l = get_layer(ll, index);
    if(l == NULL) {
        return(-1);
    }

    l->x = x;
    l->y = y;

    return(0);
}

int tilemap_set_layer_window(LayerList *ll,
                             unsigned int index,
                             unsigned int w,
                             unsigned int h) {
    unsigned int tmw, tmh;
    Layer *l = get_layer(ll, index);
    if(l == NULL) {
        return(-1);
    }
    Tilemap *tm = get_tilemap(ll, l->tilemap);
    if(tm == NULL) {
        return(-1);
    }
    Tileset *ts = get_tileset(ll, tm->tileset);
    if(ts == NULL) {
        return(-1);
    }

    tmw = tm->w * ts->tw;
    tmh = tm->h * ts->th;
    /* Allow passing in 0s to be reset to full size */
    if(w == 0) {
        w = tmw;
    }
    if(h == 0) {
        h = tmh;
    }

    if(w > tmw || h > tmh) {
        LOG_PRINTF(ll, "Layer window out of range.\n");
        return(-1);
    }
 
    l->w = w;
    l->h = h;

    return(0);
}

int tilemap_set_layer_scroll_pos(LayerList *ll,
                                 unsigned int index,
                                 unsigned int scroll_x,
                                 unsigned int scroll_y) {
    unsigned int tmw, tmh;
    Layer *l = get_layer(ll, index);
    if(l == NULL) {
        return(-1);
    }
    Tilemap *tm = get_tilemap(ll, l->tilemap);
    if(tm == NULL) {
        return(-1);
    }
    Tileset *ts = get_tileset(ll, tm->tileset);
    if(ts == NULL) {
        return(-1);
    }

    tmw = tm->w * ts->tw;
    tmh = tm->h * ts->th;

    if(scroll_x > tmw - 1 || scroll_y > tmh - 1) {
        LOG_PRINTF(ll, "Layer scroll pos out of range.\n");
        return(-1);
    }
 
    l->scroll_x = scroll_x;
    l->scroll_y = scroll_y;

    return(0);
}

int tilemap_set_layer_scale(LayerList *ll,
                            unsigned int index,
                            double scale_x,
                            double scale_y) {
    Layer *l = get_layer(ll, index);
    if(l == NULL) {
        return(-1);
    }

    /* SDL doesn't seem to allow negative rect coords and just clamps to 0 so
     * to avoid unexpected behavior, just throw a warning to the user. */
    if(scale_x < 0.0 || scale_y < 0.0) {
        LOG_PRINTF(ll, "WARNING: Negative scale.\n");
    }

    l->scale_x = scale_x;
    l->scale_y = scale_y;

    return(0);
}

int tilemap_set_layer_rotation(LayerList *ll, unsigned int index, double angle) {
    Layer *l = get_layer(ll, index);
    if(l == NULL) {
        return(-1);
    }

    l->angle = angle;

    return(0);
}

int tilemap_set_layer_colormod(LayerList *ll, unsigned int index, Uint32 colormod) {
    Layer *l = get_layer(ll, index);
    if(l == NULL) {
        return(-1);
    }

    l->colormod = colormod;

    return(0);
}

int tilemap_set_layer_blendmode(LayerList *ll, unsigned int index, int blendMode) {
    Layer *l = get_layer(ll, index);
    if(l == NULL) {
        return(-1);
    }

    switch(blendMode) {
        case TILEMAP_BLENDMODE_BLEND:
            l->blendMode = SDL_BLENDMODE_BLEND;
            break;
        case TILEMAP_BLENDMODE_ADD:
            l->blendMode = SDL_BLENDMODE_ADD;
            break;
        case TILEMAP_BLENDMODE_MOD:
            l->blendMode = SDL_BLENDMODE_MOD;
            break;
        case TILEMAP_BLENDMODE_MUL:
            l->blendMode = SDL_BLENDMODE_MUL;
            break;
        case TILEMAP_BLENDMODE_SUB:
            l->blendMode =
                SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_SRC_ALPHA,
                                           SDL_BLENDFACTOR_ONE,
                                           SDL_BLENDOPERATION_REV_SUBTRACT,
                                           SDL_BLENDFACTOR_ZERO,
                                           SDL_BLENDFACTOR_ONE,
                                           SDL_BLENDOPERATION_ADD);
            break;
        default:
            LOG_PRINTF(ll, "Invalid blend mode: %d\n", blendMode);
            return(-1);
    }

    return(0);
}
 
int tilemap_draw_layer(LayerList *ll, unsigned int index) {
    SDL_Rect dest, src;
    unsigned int tmw, tmh;
    unsigned int right, bottom;
    int overRight, overBottom;
    int remainRight, remainBottom;

    Layer *l = get_layer(ll, index);
    if(l == NULL) {
        return(-1);
    }
    Tilemap *tm = get_tilemap(ll, l->tilemap);
    if(tm == NULL) {
        return(-1);
    }
    Tileset *ts = get_tileset(ll, tm->tileset);
    if(ts == NULL) {
        return(-1);
    }

    /* Make sure it's a layer with graphics */
    if(tm->tex == NULL) {
        LOG_PRINTF(ll, "Layer without graphics: %d\n", index);
        return(-1);
    }

    if(SDL_SetTextureColorMod(tm->tex,
            (l->colormod & TILEMAP_RMASK) >> TILEMAP_RSHIFT,
            (l->colormod & TILEMAP_GMASK) >> TILEMAP_GSHIFT,
            (l->colormod & TILEMAP_BMASK) >> TILEMAP_BSHIFT) < 0) {
        fprintf(stderr, "Failed to set layer colormod.\n");
        return(-1);
    }
    if(SDL_SetTextureAlphaMod(tm->tex,
            (l->colormod & TILEMAP_AMASK) >> TILEMAP_ASHIFT) < 0) {
        fprintf(stderr, "Failed to set tile alphamod.\n");
        return(-1);
    }

    if(SDL_SetTextureBlendMode(tm->tex, l->blendMode) < 0) {
        if(ll->blendWarned == 0) {
            fprintf(stderr, "Failed to set layer blend mode, falling back to "
                            "SDL_BLENDMODE_BLEND, some things may appear "
                            "wrong. This warning will appear only once.\n");
            ll->blendWarned = 1;
        }
        SDL_SetTextureBlendMode(tm->tex, SDL_BLENDMODE_BLEND);
    }
    tmw = tm->w * ts->tw;
    tmh = tm->h * ts->th;
    right = l->scroll_x + l->w;
    bottom = l->scroll_y + l->h;
    overRight = right - tmw;
    overBottom = bottom - tmh;
    remainRight = l->w - overRight;
    remainBottom = l->h - overBottom;

    src.x = l->scroll_x;
    src.y = l->scroll_y;
    src.w = overRight > 0 ? remainRight : l->w;
    src.h = overBottom > 0 ? remainBottom : l->h;
    dest.x = l->x;
    dest.y = l->y;
    dest.w = src.w * l->scale_x;
    dest.h = src.h * l->scale_y;
    if(FLOAT_COMPARE(l->angle, 0.0)) {
        if(SDL_RenderCopy(ll->renderer,
                          tm->tex,
                          &src,
                          &dest) < 0) {
            LOG_PRINTF(ll, "Failed to render layer.\n");
            return(-1);
        }
        if(overRight > 0) {
            src.x = 0;
            src.y = l->scroll_y;
            src.w = overRight;
            src.h = overBottom > 0 ? remainBottom : l->h;
            dest.x = l->x + (remainRight * l->scale_x);
            dest.y = l->y;
            dest.w = src.w * l->scale_x;
            dest.h = src.h * l->scale_y;
            if(SDL_RenderCopy(ll->renderer,
                              tm->tex,
                              &src,
                              &dest) < 0) {
                LOG_PRINTF(ll, "Failed to render layer.\n");
                return(-1);
            }
        }
        if(overBottom > 0) {
            src.x = l->scroll_x;
            src.y = 0;
            src.w = overRight > 0 ? remainRight : l->w;
            src.h = overBottom;
            dest.x = l->x;
            dest.y = l->y + (remainBottom * l->scale_y);
            dest.w = src.w * l->scale_x;
            dest.h = src.h * l->scale_y;
            if(SDL_RenderCopy(ll->renderer,
                              tm->tex,
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
            dest.x = l->x + (remainRight * l->scale_x);
            dest.y = l->y + (remainBottom * l->scale_y);
            dest.w = src.w * l->scale_x;
            dest.h = src.h * l->scale_y;
            if(SDL_RenderCopy(ll->renderer,
                              tm->tex,
                              &src,
                              &dest) < 0) {
                LOG_PRINTF(ll, "Failed to render layer.\n");
                return(-1);
            }
        }
    } else {
        if(SDL_RenderCopyEx(ll->renderer,
                            tm->tex,
                            &src,
                            &dest,
                            l->angle,
                            &(l->center),
                            SDL_FLIP_NONE) < 0) {
            LOG_PRINTF(ll, "Failed to render layer.\n");
            return(-1);
        }
    }

    return(0);
}
