# Limpieza de código en `bonus-renderer`

## Objetivo

Esta limpieza elimina código que estaba compilado o almacenado en el proyecto,
pero que no participaba en el flujo real. No cambia el algoritmo del juego, la
sincronización ni la arquitectura P0/P1/P2/P3.

La decisión se tomó revisando referencias con `rg`, dependencias con `gcc -MM`,
símbolos del binario con `nm` y secciones descartables mediante
`-ffunction-sections -Wl,--gc-sections`.

## Acciones realizadas

| Acción | Motivo | Efecto |
|---|---|---|
| Eliminar `collision.c` y `collision.h` | `verificar_colision()` e `imprimir_vidas()` no tenían llamadas. La detección real se ejecuta en `collision_thread()` dentro de `main.c`. | Se retira una implementación antigua que solo detectaba misma celda. |
| Quitar `collision.c` del Makefile | El módulo dejó de existir y no aportaba símbolos utilizados. | El enlazador procesa menos código. |
| Quitar `#include "collision.h"` de `main.c` | Ninguna declaración de esa cabecera se utilizaba. | Las dependencias de `main.c` reflejan el código real. |
| Eliminar `detectar_colision()` de `ghost.c/.h` | No tenía llamadas y duplicaba la detección antigua de misma celda. | `ghost.c` conserva únicamente inicialización y movimiento. |
| Eliminar `imprimir_fantasmas()` de `ghost.c/.h` | No tenía llamadas en el flujo actual. | Se retira una utilidad de depuración abandonada. |
| Eliminar `utils.c` y `utils.h` | El Makefile no compilaba `utils.c`, ningún módulo incluía `utils.h` y todas sus funciones estaban desconectadas. | Se elimina un módulo heredado completo. |
| Retirar la segunda `construir_ruta()` | La versión de `utils.c` duplicaba el nombre de la función activa en `main.c`, pero con otra firma. Si se enlazaban ambas, se producía un símbolo duplicado. | Solo queda la implementación usada por el programa. |
| Mantener `pacman.c/.h` | `movement_executor_thread()` llama a `mover_pacman()`. | Se conserva el movimiento real de Pac-Man. |
| Mantener `map.c/.h` | El programa usa `cargar_mapa()`, `imprimir_mapa()` y `es_celda_valida()`. | Se conserva lectura y validación del mapa. |
| Mantener `ghost.c/.h` | P2 usa `inicializar_fantasmas_desde_shared()` y `mover_fantasma()`. | Se conserva solo la API necesaria de fantasmas. |
| Mantener `renderer.c` y `renderer_sdl.c` | Son implementaciones alternativas de la misma interfaz `renderer_process()`. El Makefile selecciona exactamente una. | Continúan disponibles ncurses y SDL2 sin enlazarse simultáneamente. |
| Agregar cabeceras como prerrequisitos del target | El Makefile anterior solo dependía de `.c`; un cambio en `.h` podía no provocar recompilación. | Cambiar una cabecera vuelve a generar el ejecutable. |
| Retirar `pacman_concurrente` del repositorio | Es un artefacto generado y ya está listado en `.gitignore`. Un binario versionado puede quedar desactualizado respecto al código. | El ejecutable se genera con `make` y no ensucia Git. |

## Detección de colisiones que permanece

La implementación vigente es `collision_thread()` en `main.c`. A diferencia de
las funciones eliminadas, esta lógica detecta:

- Pac-Man y fantasma en la misma celda;
- intercambio o cruce de posiciones durante el mismo tick;
- publicación sincronizada de `collision_detected`, `collision_tick` y
  `collision_ghost_id`.

P0 continúa consumiendo el evento mediante `procesar_colision_si_existe()`.

## Compilación

Renderer de consola:

```bash
make clean
make
```

Renderer SDL2:

```bash
make clean
make RENDER=sdl
```

El renderer SDL requiere `sdl2-config` y las cabeceras/bibliotecas de SDL2.

## Alcance

No se modificaron mutex, semáforos, colas, prioridades, procesos, hilos,
movimientos, reglas de colisión ni condiciones de cierre. La limpieza se limita
a código sin referencias, dependencias del build y artefactos generados.
