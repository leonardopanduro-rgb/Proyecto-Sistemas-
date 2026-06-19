# Plan del Proyecto: Pac-Man Concurrente en C con POSIX

## 1. Idea central del proyecto

El proyecto no busca hacer solamente un Pac-Man visual. La idea principal es demostrar conceptos de Sistemas Operativos usando una simulacion concurrente:

- Procesos con `fork()`.
- Hilos con `pthread`.
- Memoria compartida.
- Semaforos POSIX.
- Mutex para proteger secciones criticas.
- Scheduler por ticks y prioridades.
- Deteccion y mitigacion de race conditions.

Pac-Man, los fantasmas y el scheduler son una forma practica de mostrar como varios procesos e hilos pueden coordinarse sin corromper datos compartidos.

## 2. Arquitectura propuesta

### P0: `scheduler_process`

Proceso principal. No mueve directamente a Pac-Man ni a los fantasmas.

Responsabilidades:

- Inicializar memoria compartida.
- Leer `map.txt`.
- Crear semaforos y mutex.
- Inicializar `global_tick`, prioridades, vidas y estado del juego.
- Crear los procesos `P1` y `P2` con `fork()`.
- Decidir en cada tick quien ejecuta: Pac-Man o enemigos.
- Aplicar prioridades y desempate Round Robin.
- Procesar solicitudes `SET_PRIORITY`.
- Procesar eventos de colision publicados por `P2`.
- Finalizar ordenadamente la simulacion.

Hilos recomendados:

- `tick_thread`: incrementa o coordina el tick global.
- `scheduler_thread`: decide el turno segun prioridades.
- `signal_thread`: libera el semaforo del proceso autorizado.

### P1: `pacman_process`

Proceso encargado de Pac-Man.

Responsabilidades:

- Leer movimientos desde `pacman_moves.txt`.
- Mover a Pac-Man si el scheduler le da turno.
- Validar paredes usando `map_grid`.
- Actualizar puntaje.
- Publicar posicion y puntaje en memoria compartida.
- Solicitar cambios de prioridad usando buzon compartido.

Hilos recomendados:

- `movement_reader_thread`: lee movimientos y los inserta en una cola.
- `movement_executor_thread`: espera turno y consume movimientos de la cola.
- `pacman_publisher_thread`: publica estado actualizado en memoria compartida.

### P2: `enemy_process`

Proceso encargado de los fantasmas.

Responsabilidades:

- Controlar 4 fantasmas.
- Cada fantasma lee su propio archivo de movimientos.
- Validar movimientos contra paredes.
- Leer la posicion actual de Pac-Man desde memoria compartida.
- Detectar colisiones.
- Publicar eventos de colision para que `P0` reduzca vidas.

Hilos recomendados:

- `enemy_controller_thread`: espera permiso de `P0` y despierta a los fantasmas.
- `ghost_thread_1`: controla fantasma A.
- `ghost_thread_2`: controla fantasma B.
- `ghost_thread_3`: controla fantasma C.
- `ghost_thread_4`: controla fantasma D.
- `pacman_tracker_thread`: copia localmente la posicion de Pac-Man.
- `collision_thread`: compara Pac-Man contra fantasmas y publica colision.

## 3. Estructura recomendada de carpetas

```text
pacman_concurrente/
  Makefile
  PLAN_PROYECTO_PACMAN.md

  src/
    main.c
    common.h
    shared_state.h
    map.c
    map.h
    p0_scheduler.c
    p0_scheduler.h
    p1_pacman_process.c
    p1_pacman_process.h
    p2_enemy_process.c
    p2_enemy_process.h
    movement_queue.c
    movement_queue.h
    sync_utils.c
    sync_utils.h

  cases/
    Caso1/
      map.txt
      pacman_moves.txt
      ghost_1_moves.txt
      ghost_2_moves.txt
      ghost_3_moves.txt
      ghost_4_moves.txt

    Caso2/
      map.txt
      pacman_moves.txt
      ghost_1_moves.txt
      ghost_2_moves.txt
      ghost_3_moves.txt
      ghost_4_moves.txt

    Caso3/
      map.txt
      pacman_moves.txt
      ghost_1_moves.txt
      ghost_2_moves.txt
      ghost_3_moves.txt
      ghost_4_moves.txt
```

## 4. Variables sugeridas en memoria compartida

```c
typedef struct {
    int global_tick;
    int max_ticks;
    int game_over;

    int pacman_x;
    int pacman_y;
    int pacman_score;
    int pacman_lives;

    int collision_detected;
    int collision_tick;
    int collision_ghost_id;

    int prioridad_pacman;
    int prioridad_enemy;

    int pending_priority_pacman;
    int priority_request_active;

    int pending_priority_enemy;
    int enemy_priority_request_active;

    char map_grid[MAX_Y][MAX_X];

    sem_t sem_pacman_turn;
    sem_t sem_enemy_turn;

    pthread_mutex_t state_mutex;
    pthread_mutex_t priority_mutex;
    pthread_mutex_t collision_mutex;
} shared_state_t;
```

Importante: si los mutex y semaforos estaran en memoria compartida entre procesos, deben inicializarse correctamente para uso interproceso.

## 5. Orden recomendado de desarrollo

Este orden esta alineado con la rubrica:

1. Definir arquitectura `P0`, `P1`, `P2`.
2. Crear estructura de carpetas y archivos base.
3. Implementar lectura de `map.txt`.
4. Detectar posiciones iniciales `P`, `A`, `B`, `C`, `D`.
5. Implementar memoria compartida.
6. Crear procesos con `fork()`.
7. Crear semaforos de turno.
8. Implementar scheduler basico por ticks.
9. Agregar prioridades y Round Robin.
10. Implementar hilos de `P1`.
11. Implementar hilos de `P2`.
12. Proteger recursos con mutex.
13. Agregar deteccion de colisiones.
14. Agregar vidas y condiciones de fin.
15. Agregar `SET_PRIORITY`.
16. Preparar logs claros para validar el funcionamiento.
17. Opcional: implementar `P3` renderer.
