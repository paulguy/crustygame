#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <SDL.h>

#include "tilemap.h"

const char CRSG_HEADER[] = "CRSG";
const char CRSG_EXT[] = "crsg";

int main(int argc, char **argv) {
    char *outname;
    char filename[256];
    char *dot;
    unsigned int namelen, dotpos;
    SDL_Surface *bmp, *bmp2;
    Uint32 fmt;
    FILE *out;
    unsigned int temp;
    unsigned int i;

    if(argc < 2 || argc > 3) {
        fprintf(stderr, "USAGE: %s <infile> [outfile]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if(argc < 3) {
        namelen = strlen(argv[1]);
        dot = strrchr(argv[1], '.');
        if(dot == NULL) {
            dotpos = namelen;
        } else {
            dotpos = dot - argv[1];
        }

        if(dotpos + 1 + strlen(CRSG_EXT) + 1 >
           sizeof(filename)) {
            fprintf(stderr, "Filename would be too long.\n");
            exit(EXIT_FAILURE);
        }

        memcpy(filename, argv[1], dotpos);
        filename[dotpos] = '.';
        memcpy(&(filename[dotpos+1]), CRSG_EXT, strlen(CRSG_EXT));
        filename[dotpos + 1 + sizeof(CRSG_EXT)] = '\0';

        outname = filename;
    } else {
        outname = argv[2];
    }

    fmt = SDL_MasksToPixelFormatEnum(32,
                                     TILEMAP_RMASK,
                                     TILEMAP_GMASK,
                                     TILEMAP_BMASK,
                                     TILEMAP_AMASK);
    if(fmt == SDL_PIXELFORMAT_UNKNOWN) {
        fprintf(stderr, "Conversion isn't possible.\n");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "Converting %s to %s\n", argv[1], outname);

    bmp = SDL_LoadBMP(argv[1]);
    if(bmp == NULL) {
        fprintf(stderr, "Failed to load %s: %s\n", argv[1], SDL_GetError());
        exit(EXIT_FAILURE);
    }

    bmp2 = SDL_ConvertSurfaceFormat(bmp, fmt, 0);
    if(bmp2 == NULL) {
        fprintf(stderr, "Conversion failed.\n");
        goto error_surface;
    }
    SDL_FreeSurface(bmp);
    bmp = bmp2;

    if(SDL_LockSurface(bmp) < 0) {
        fprintf(stderr, "Failed to lock surface for access: %s\n",
                SDL_GetError());
        goto error_surface;
    }

    out = fopen(outname, "wb");
    if(out == NULL) {
        fprintf(stderr, "Failed to open %s:%s\n", outname, strerror(errno));
        goto error_surface;
    }

    if(fwrite(CRSG_HEADER, 1, strlen(CRSG_HEADER), out) <
       strlen(CRSG_HEADER)) {
        fprintf(stderr, "Failed to write header.\n");
        goto error_outfile;
    }

    temp = bmp->w;
    if(fwrite(&temp, 1, sizeof(temp), out) < sizeof(temp)) {
        fprintf(stderr, "Failed to write width.\n");
        goto error_outfile;
    }

    temp = bmp->h;
    if(fwrite(&temp, 1, sizeof(temp), out) < sizeof(temp)) {
        fprintf(stderr, "Failed to write height.\n");
        goto error_outfile;
    }

    for(i = 0; i < bmp->h; i++) {
        if(fwrite(&(((char *)(bmp->pixels))[i * bmp->pitch]),
                  1, bmp->w * 4,
                  out) < bmp->w * 4) {
            fprintf(stderr, "Failed to write row.\n");
            goto error_outfile;
        }
    }

    fclose(out);
    SDL_FreeSurface(bmp);
    exit(EXIT_SUCCESS);
error_outfile:
    fclose(out);
error_surface:
    SDL_FreeSurface(bmp);

    exit(EXIT_FAILURE);
}
