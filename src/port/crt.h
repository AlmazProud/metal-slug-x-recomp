/* CRT effect (docs/QOL_FEATURES.md): drawn from the scaled game image with the plain SDL renderer
 * (no shaders), at the output's real pixels:
 *   - scanlines: each output row darkened by its distance to the centre of its game line (a
 *     gaussian beam profile);
 *   - the phosphor mask: an aperture grille (vertical RGB stripes, Arcade) or a dot mask (TV),
 *     multiplied over the image;
 *   - the brightness the mask and the lines take away, partly given back by adding the image
 *     again (bright areas bloom a little, as on a tube);
 *   - the glow: the image shrunk twice (a blur) and added over it, so bright areas bloom;
 *   - a vignette towards the corners;
 *   - the curvature (optional): the finished image, drawn in a texture, goes onto the window over
 *     a grid bent like a tube's face.
 */
#ifndef CRT_H
#define CRT_H

#include <SDL3/SDL.h>

enum { CRT_OFF, CRT_ARCADE, CRT_TV, CRT_SOFT, CRT_COUNT };

/* Draws the game image `base` (any size) at `d` (output pixels) with the effect. */
void crt_draw(SDL_Renderer *r, SDL_Texture *base, SDL_FRect d, int style, int curve);
void crt_destroy(void);                  /* frees the textures (before the renderer) */

#endif
