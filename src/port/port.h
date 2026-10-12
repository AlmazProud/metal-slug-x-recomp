/* Port nativo: piezas compartidas entre main.c, el mode con window y el mode sin window. */
#ifndef PORT_H
#define PORT_H

#define NS_PER_FRAME 16896000LL         /* 202 752 cycles a 12 MHz: 59,1856 Hz */

typedef struct {
    const char *roms;
    int scale, headless, empty_sram;
    int turbo;                           /* -1 = el de la configuración */
    long frames;
    const char *screenshots, *output, *script, *wav, *inputs, *fingerprints, *dump;
    int no_audio;
    int test_state;                   /* frames a comparar tras cargar un estado (0 = no) */
    const char *catalog;                /* --catalog-sonido: tabla de los comandos de sonido */
    int test_sounds;                  /* --probar-sonidos: prueba del menú de sonido */
    int blood;                          /* --blood: opción BLOOD del juego encendida */
    int date[7], has_date;
} Options;

/* headless.c */
int save_bmp(const char *ruta);
int headless_prepare(const Options *o);
int headless_run(const Options *o);

/* window.c */
int window_run(const Options *o);

/* gog.c: carpeta resources\game de una instalación de Metal Slug X de GOG (registro de Windows y
 * carpetas habituales). Devuelve 1 si encontró una cuyos archivos validan. */
int gog_find(char *carpeta, int tam);

#endif
