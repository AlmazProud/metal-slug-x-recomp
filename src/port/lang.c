/* Frontend translations (see lang.h).
 *
 * The Spanish texts are the port's original ones. The menu font only has ASCII (font.c), so
 * they are written without accents or n-tilde; the only exception is the ROM folder dialog,
 * which is a native Windows message box. Lookup is a linear strcmp over the table: a few dozen
 * strings per frame against ~150 entries, negligible.
 */
#include "lang.h"

#include <string.h>

int lang = LANG_EN;

void lang_set(int l) { lang = l >= 0 && l < LANG_COUNT ? l : LANG_EN; }

static const struct { const char *en, *es; } TABLE[] = {
    /* menu: page titles */
    {"PAUSE", "PAUSA"},
    {"SETTINGS", "CONFIGURACION"},
    {"GAME", "JUEGO"},
    {"CONTROLS", "CONTROLES"},
    /* menu: options */
    {"Play", "Jugar"},
    {"Continue", "Continuar"},
    {"Save game", "Guardar partida"},
    {"Load game", "Cargar partida"},
    {"Settings", "Configuracion"},
    {"Music and sounds", "Musica y sonidos"},
    {"Back to start screen", "Volver al inicio"},
    {"Quit", "Salir"},
    {"Game", "Juego"},
    {"Controls", "Controles"},
    {"Language", "Idioma"},
    {"Fullscreen", "Pantalla completa"},
    {"Aspect", "Aspecto"},
    {"Filter", "Filtro"},
    {"Volume", "Volumen"},
    {"Rumble", "Vibracion"},
    {"Slowdown", "Ralentizacion"},
    {"Blood", "Sangre"},
    {"Autofire P1", "Autodisparo J1"},
    {"Autofire P2", "Autodisparo J2"},
    {"Autofire speed", "Velocidad autodisparo"},
    {"Start screen", "Pantalla de inicio"},
    {"Player 1", "Jugador 1"},
    {"Player 2", "Jugador 2"},
    {"Debug mode (F3)", "Modo debug (F3)"},
    {"Service menu", "Menu de servicio"},
    {"Reset the machine", "Reiniciar la maquina"},
    /* menu: values */
    {"< Slot %d >", "< Ranura %d >"},
    {"Yes", "Si"},
    {"Off", "No"},
    {"Off (x%d)", "No (x%d)"},
    {"Red", "Roja"},
    {"Square", "Pixeles"},
    {"Sharp", "Nitido"},
    {"Smooth", "Suave"},
    {"Pixel", "Pixeles"},
    {"Own button", "Boton propio"},
    {"On fire", "En el disparo"},
    /* menu: help lines */
    {"Original: slows down with many sprites, like the arcade",
     "Original: se ralentiza con muchos sprites, como la placa"},
    {"4:3 like the original monitor, or square pixels", "4:3 como el monitor original, o pixeles cuadrados"},
    {"No tearing; at 60 Hz the game runs 1.4% faster", "Sin cortes en la imagen; a 60 Hz el juego va 1.4% mas rapido"},
    {"Original: as set in the service menu (BLOOD, off in the US)",
     "Original: la del menu de servicio (BLOOD, apagada en EE.UU.)"},
    {"Sharp: even pixels without blurry edges", "Nitido: pixeles parejos sin bordes borrosos"},
    {"Own button: B / G on the keyboard, RB on the gamepad", "Boton propio: B / G en el teclado, RB en el mando"},
    {"BIOS menu: difficulty, lives, time (also with F2)", "Menu del BIOS: dificultad, vidas, tiempo (tambien con F2)"},
    {"Listen to the music, voices and effects of the game", "Escucha la musica, las voces y los efectos del juego"},
    {"68000 load, slowdown, sprites, palette, RAM", "Carga del 68000, ralentizacion, sprites, paleta, RAM"},
    {"Show this menu when the game opens", "Mostrar este menu al abrir el juego"},
    {"Enter/A: select   </>: change", "Enter/A: elegir   </>: cambiar"},
    {"Enter/A: select   </>: change   Esc/B: back", "Enter/A: elegir   </>: cambiar   Esc/B: volver"},
    /* menu: control remapping */
    {"PLAYER %d CONTROLS", "CONTROLES DEL JUGADOR %d"},
    {"Press the key or button for:", "Pulsa la tecla o el boton para:"},
    {"Current: %s / gamepad %s", "Actual: %s / mando %s"},
    {"Del: keep current    Esc: finish", "Supr: dejar igual    Esc: terminar"},
    {"Controls saved", "Controles guardados"},
    {"Up", "Arriba"},
    {"Down", "Abajo"},
    {"Left", "Izquierda"},
    {"Right", "Derecha"},
    {"A (fire)", "A (disparo)"},
    {"B (jump)", "B (salto)"},
    {"C (grenade)", "C (granada)"},
    {"Coin", "Moneda"},
    {"Autofire", "Disparo automatico"},
    /* sound menu */
    {"MUSIC AND SOUNDS", "MUSICA Y SONIDOS"},
    {"Music", "Musica"},
    {"Effects", "Efectos"},
    {"Voices", "Voces"},
    {"Recent", "Recientes"},
    {"Nothing has played in the game yet", "Todavia no sono nada en el juego"},
    {"Empty", "Vacio"},
    {"RUMBLE", "VIBRA"},                 /* column header (uppercase key: shorter than "Rumble") */
    {"off", "no"},
    {"light", "leve"},
    {"medium", "media"},
    {"strong", "fuerte"},
    {"loop", "bucle"},
    {"Playing: %s  %02d:%02d", "Sonando: %s  %02d:%02d"},
    {"Type the name   Enter save   Esc cancel", "Escribe el nombre   Enter guardar   Esc cancelar"},
    {"Enter/A listen   Del/Y stop   F2 rename", "Enter/A escuchar   Supr/Y parar   F2 renombrar"},
    {"V/X rumble   </> LB/RB tab   Esc/B back", "V/X vibracion   </> LB/RB pestana   Esc/B volver"},
    /* built-in sound names (PROFILE_SOUNDS); "Track 23" and the like are built from these words */
    {"Track", "Pista"},
    {"Effect", "Efecto"},
    {"Voice", "Voz"},
    {"Mission complete (fanfare)", "Mision cumplida (fanfarria)"},
    {"Mission 1", "Mision 1"},
    {"Mission 2", "Mision 2"},
    {"Tank battle", "Combate contra el tanque"},
    {"How to play", "Como jugar"},
    {"Soldier select", "Seleccion de soldado"},
    {"Big explosion", "Explosion grande"},
    {"Slug cannon", "Canon del Slug"},
    {"Slug vulcan", "Vulcan del Slug"},
    {"Armor hit", "Impacto en blindaje"},
    {"Pistol", "Pistola"},
    /* window notices */
    {"State saved to slot %d", "Estado guardado en la ranura %d"},
    {"Could not save slot %d", "No se pudo guardar la ranura %d"},
    {"Slot %d is empty", "La ranura %d esta vacia"},
    {"State loaded from slot %d", "Estado de la ranura %d cargado"},
    {"Slot %d is from another port version", "La ranura %d es de otra version del port"},
    {"Gamepad %d: %s", "Mando %d: %s"},
    {"Gamepad %d disconnected", "Mando %d desconectado"},
    {"Machine reset", "Maquina reiniciada"},
    {"Paused", "Pausa"},
    {"Resume", "Continuar"},
    {"Screenshot: %s", "Captura: %s"},
    {"Esc: menu   F5/F9: save/load game", "Esc: menu   F5/F9: guardar/cargar partida"},
    {"No valid GOG copy of Metal Slug X was found.\n\n%s\n\n"
     "Choose the folder where you installed the game (or its resources\\game subfolder).",
     "No se encontró una copia válida de Metal Slug X de GOG.\n\n%s\n\n"
     "Elige la carpeta donde instalaste el juego (o su subcarpeta resources\\game)."},
};

const char *tr(const char *en)
{
    if (lang != LANG_ES || !en) return en;
    for (size_t i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++)
        if (!strcmp(TABLE[i].en, en)) return TABLE[i].es;
    return en;
}
