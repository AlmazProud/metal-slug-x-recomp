/* Port nativo: piezas compartidas entre main.c, el modo con ventana y el modo sin ventana. */
#ifndef PORT_H
#define PORT_H

#define NS_POR_CUADRO 16896000LL         /* 202 752 ciclos a 12 MHz: 59,1856 Hz */

typedef struct {
    const char *roms;
    int escala, sin_ventana, sram_vacia;
    int turbo;                           /* -1 = el de la configuración */
    long cuadros;
    const char *capturas, *salida, *guion, *wav, *entradas, *huellas, *volcar;
    int sin_audio;
    int probar_estado;                   /* cuadros a comparar tras cargar un estado (0 = no) */
    int fecha[7], con_fecha;
} Opciones;

/* sin_ventana.c */
int guardar_bmp(const char *ruta);
int sin_ventana_preparar(const Opciones *o);
int sin_ventana_correr(const Opciones *o);

/* ventana.c */
int ventana_correr(const Opciones *o);

/* gog.c: carpeta resources\game de una instalación de Metal Slug X de GOG (registro de Windows y
 * carpetas habituales). Devuelve 1 si encontró una cuyos archivos validan. */
int gog_buscar(char *carpeta, int tam);

#endif
