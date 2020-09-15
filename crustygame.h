#ifndef _CRUSTYGAME_H
#define _CRUSTYGAME_H

#include <SDL.h>
#include "tilemap.h"

#define CRUSTYGAME_KEYDOWN              (1)
#define CRUSTYGAME_KEYUP                (2)
#define CRUSTYGAME_MOUSEMOTION          (3)
#define CRUSTYGAME_MOUSEBUTTONDOWN      (4)
#define CRUSTYGAME_MOUSEBUTTONUP        (5)
#define CRUSTYGAME_MOUSEWHEEL           (6)
#define CRUSTYGAME_JOYAXISMOTION        (7)
#define CRUSTYGAME_JOYBALLMOTION        (8)
#define CRUSTYGAME_JOYHATMOTION         (9)
#define CRUSTYGAME_JOYBUTTONDOWN        (10)
#define CRUSTYGAME_JOYBUTTONUP          (11)
#define CRUSTYGAME_CONTROLLERAXISMOTION (12)
#define CRUSTYGAME_CONTROLLERBUTTONDOWN (13)
#define CRUSTYGAME_CONTROLLERBUTTONUP   (14)
 
typedef struct {
    SDL_Window *win;
    SDL_Renderer *renderer;
    SDL_Event lastEvent;
    LayerList *ll;
    int running;

    void *buffer;
    unsigned int size;
    int ret;

    int mouseCaptured;
    int mouseReleaseCombo;
} CrustyGame;

extern CrustyGame state;
#endif
