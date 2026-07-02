# Organización del proyecto

El código se divide por responsabilidad y por proceso POSIX. La reorganización
no cambia el protocolo P0/P1/P2/P3 ni las reglas del juego.

## Estructura

```text
.
├── main.c
├── include/
│   ├── game.h
│   ├── shared.h
│   ├── map.h
│   ├── pacman.h
│   ├── ghost.h
│   ├── p0_threads.h
│   ├── p1_threads.h
│   ├── p2_threads.h
│   └── renderer.h
└── src/
    ├── shared_state.c
    ├── process_utils.c
    ├── validation.c
    ├── game_control.c
    ├── scheduler_process.c
    ├── p0_threads.c
    ├── p1_process.c
    ├── p1_threads.c
    ├── p2_process.c
    ├── p2_threads.c
    ├── map.c
    ├── pacman.c
    ├── ghost.c
    ├── renderer.c
    └── renderer_sdl.c
```

## Responsabilidades

| Módulo | Responsabilidad |
|---|---|
| `main.c` | Interpreta argumentos y llama a `scheduler_process()`. |
| `shared_state.c` | Crea, inicializa, consulta y libera `SharedData`. |
| `process_utils.c` | Relación padre-hijo, cierre por error, `waitpid` y timeout de P3. |
| `validation.c` | Limpia y valida instrucciones; extrae y valida prioridades. |
| `game_control.c` | Buzones, EOF, selección por prioridad, colisiones y estado del tick. |
| `scheduler_process.c` | Entry point de P0: mapa, forks, ciclo global y cleanup. |
| `p0_threads.c` | `tick_thread`, `scheduler_thread` y `signal_thread`. |
| `p1_process.c` | Crea, une y destruye los recursos de P1. |
| `p1_threads.c` | Reader, executor, publisher y cola productor-consumidor. |
| `p2_process.c` | Crea, une y destruye los recursos de P2. |
| `p2_threads.c` | Controller, cuatro ghosts, tracker y collision thread. |
| `map.c` | Lectura y validación completa de `map.txt`. |
| `pacman.c` / `ghost.c` | Aplicación de movimientos sobre una celda validada. |
| `renderer.c` | Backend P3 ncurses. |
| `renderer_sdl.c` | Backend P3 SDL2. |

## Dependencias

- `game.h` expone la API común entre procesos.
- `p0_threads.h`, `p1_threads.h` y `p2_threads.h` contienen estructuras internas
  de coordinación; no forman parte de `SharedData`.
- `shared.h` contiene únicamente estado que debe ser visible entre procesos.
- Los módulos incluyen headers mediante `-Iinclude`; no incluyen archivos `.c`.

## Compilación

```bash
make normal
./pacman_concurrente Caso1 30
```

Renderer ncurses:

```bash
make run-render
```

Renderer SDL2:

```bash
SDL_VIDEODRIVER=dummy make run-sdl  # prueba headless
make run-sdl                        # ventana normal
```

ThreadSanitizer:

```bash
make tsan
TSAN_OPTIONS="halt_on_error=0 exitcode=66" \
./pacman_concurrente Caso1 100
```

TSan puede abortar con `unexpected memory mapping` en algunos layouts del
kernel; ese fallo ocurre antes de `main()` y no implica un error de compilación.

## Regla para cambios futuros

- Lógica exclusiva de P1 va en `p1_*`.
- Lógica exclusiva de P2 va en `p2_*`.
- P0 decide turnos y procesa estado; P1/P2 no cambian prioridades efectivas ni
  vidas directamente.
- Todo acceso entre procesos debe pasar por `SharedData` y su mutex o por los
  semáforos compartidos.
- Las validaciones de texto no deben mezclarse con movimiento ni scheduling.
