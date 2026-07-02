# Pac-Man concurrente en C con POSIX

Simulación de Pac-Man desarrollada para Sistemas Computacionales. El programa
coordina procesos e hilos mediante memoria compartida, mutex y semáforos POSIX.
Incluye ejecución por consola y dos renderers opcionales: ncurses y SDL2.

## Arquitectura

El programa comienza en `main()` y delega la simulación a P0:

```text
main.c
└── scheduler_process()                         P0
    ├── fork() → pacman_process()               P1
    │            ├── movement_reader_thread
    │            ├── movement_executor_thread
    │            └── pacman_publisher_thread
    ├── fork() → enemy_process()                P2
    │            ├── enemy_controller
    │            ├── ghost_thread_1..4
    │            ├── pacman_tracker_thread
    │            └── collision_thread
    ├── fork() → renderer_process()              P3, solo con --render
    └── tick_thread → scheduler_thread → signal_thread
```

- **P0** carga y valida el mapa, crea los procesos, incrementa el tick global,
  procesa prioridades y colisiones, decide el turno y realiza el cierre.
- **P1** lee las instrucciones de Pac-Man mediante una cola
  productor-consumidor y ejecuta una instrucción cuando P0 le da permiso.
- **P2** coordina cuatro fantasmas, mantiene una copia de la posición de
  Pac-Man y publica colisiones directas o por intercambio de posiciones.
- **P3** es opcional. Lee una instantánea del estado y dibuja un frame por tick.

P0 crea P1, P2 y, opcionalmente, P3 con `fork()`. Cada hijo recibe `SIGTERM` si
P0 desaparece. Al terminar, P0 despierta procesos bloqueados, ejecuta
`pthread_join()` para sus hilos, recoge hijos con `waitpid()` y libera el mmap.

## Sincronización

`SharedData`, definido en `include/shared.h`, se crea con `mmap()` usando
`MAP_SHARED | MAP_ANONYMOUS`.

- `mutex_shared` usa `PTHREAD_PROCESS_SHARED` y protege tick, estado del juego,
  posiciones, score, vidas, prioridades, buzones y eventos de colisión.
- `sem_pacman_turn` y `sem_enemy_turn` autorizan una acción de P1 o P2.
- `sem_turn_done` impide que P0 adelante el tick antes de acabar el turno.
- `sem_render_turn` y `sem_render_done` forman la barrera opcional P0-P3.
- P1 protege su cola y su bandera de cierre con `mutex_cola`.
- P2 separa la protección de fantasmas, copia local de Pac-Man y cierre mediante
  `mutex_ghosts`, `mutex_pacman_local` y `mutex_terminar`.
- Los semáforos internos de P0 y P2 ordenan sus fases para que ninguna lea
  datos mientras otra todavía los modifica.

## Estructura de archivos

```text
.
├── main.c                         Entrada y argumentos
├── Makefile                       Compilación y ejecución
├── Caso1/, Caso2/, Caso3/         Mapas y movimientos de ejemplo
├── include/
│   ├── game.h                     API común del programa
│   ├── shared.h                   Memoria compartida y semáforos globales
│   ├── p0_threads.h               Estado interno de hilos de P0
│   ├── p1_threads.h               Cola e hilos de P1
│   ├── p2_threads.h               Estado e hilos de P2
│   ├── map.h, pacman.h, ghost.h   Reglas del juego
│   └── renderer.h                 API de P3
└── src/
    ├── scheduler_process.c        Ciclo de vida y scheduler P0
    ├── p0_threads.c               Tick, selección y señal de turno
    ├── p1_process.c               Creación y cierre de hilos P1
    ├── p1_threads.c               Reader, executor, publisher y cola
    ├── p2_process.c               Creación y cierre de hilos P2
    ├── p2_threads.c               Controller, ghosts, tracker y collision
    ├── shared_state.c             mmap, mutex y semáforos compartidos
    ├── process_utils.c            Señales, waitpid y cierre de hijos
    ├── validation.c               Validación de instrucciones
    ├── game_control.c             Prioridad, EOF, colisiones y estado
    ├── map.c                      Lectura y validación del mapa
    ├── pacman.c, ghost.c          Aplicación de movimientos
    ├── renderer.c                 Renderer ncurses
    └── renderer_sdl.c             Renderer SDL2
```

Cada caso debe contener:

```text
map.txt
pacman_moves.txt
ghost_1_moves.txt
ghost_2_moves.txt
ghost_3_moves.txt
ghost_4_moves.txt
```

Las instrucciones aceptadas son `UP`, `DOWN`, `LEFT`, `RIGHT` y
`SET_PRIORITY N`, donde `N` está entre 1 y 100.

## Dependencias

Base requerida:

```bash
sudo apt update
sudo apt install build-essential
```

Para el renderer de consola ncurses:

```bash
sudo apt install libncurses-dev
```

Para el renderer gráfico SDL2:

```bash
sudo apt install libsdl2-dev
```

El modo normal también enlaza ncurses porque el backend se selecciona al
compilar, aunque P3 solo se crea cuando se añade `--render`.

## Compilar y ejecutar

### 1. Ejecución normal, sin renderer

```bash
make normal
./pacman_concurrente Caso1
```

Puede indicarse un máximo de ticks:

```bash
./pacman_concurrente Caso1 100
```

También pueden usarse `Caso2` y `Caso3`.

### 2. Renderer de consola con ncurses

Requiere una terminal interactiva y `libncurses-dev`:

```bash
make run-render
```

El objetivo `run-render` compila el backend ncurses, ejecuta `Caso1` con 40
ticks y habilita P3. Los mensajes de diagnóstico se guardan en
`pacman_debug.log`, mientras ncurses utiliza la terminal real.

Comando equivalente manual:

```bash
make normal
./pacman_concurrente Caso1 40 --render
```

Si no existe `/dev/tty`, P3 mantiene la sincronización sin dibujar para que el
scheduler no se bloquee.

### 3. Renderer gráfico con SDL2

Requiere `libsdl2-dev` y un entorno gráfico disponible:

```bash
make run-sdl
```

El Makefile recompila el mismo ejecutable seleccionando `renderer_sdl.c`. Para
compilar o ejecutar manualmente:

```bash
make clean
make RENDER=sdl
./pacman_concurrente Caso1 40 --render
```

En un servidor sin pantalla puede comprobarse el protocolo del renderer con:

```bash
SDL_VIDEODRIVER=dummy ./pacman_concurrente Caso1 40 --render
```

## Otros objetivos del Makefile

```bash
make clean      # elimina pacman_concurrente
make normal     # compilación normal con warnings y símbolos de depuración
make tsan       # compilación con ThreadSanitizer
```

Ejemplo con ThreadSanitizer:

```bash
make tsan
TSAN_OPTIONS="halt_on_error=0 exitcode=66" ./pacman_concurrente Caso1 100
```

TSan puede fallar antes de `main()` con `unexpected memory mapping` en algunos
sistemas Linux; ese mensaje corresponde a una incompatibilidad del entorno y
no a un error de compilación del proyecto.

## Flujo de un tick

1. `main()` llama a `scheduler_process()`.
2. P0 valida `map.txt`, inicializa `SharedData` y crea los hijos.
3. `tick_thread` incrementa `global_tick` bajo `mutex_shared`.
4. `scheduler_thread` procesa solicitudes y elige P1 o P2 por prioridad; en
   empate alterna mediante Round Robin.
5. `signal_thread` publica el semáforo del proceso elegido.
6. P1 ejecuta una instrucción, o P2 mueve sus cuatro fantasmas y comprueba
   colisiones.
7. El proceso elegido publica `sem_turn_done`.
8. P0 procesa el evento de colisión, imprime el estado y solicita un frame a P3
   si el renderer está activo.
9. El ciclo acaba por `game_over`, vidas agotadas, `max_ticks`, error de entrada
   o agotamiento de todos los archivos de movimientos.

## Limpieza

```bash
make clean
```

Los ejecutables, objetos y logs están ignorados por Git y no forman parte de la
entrega.
