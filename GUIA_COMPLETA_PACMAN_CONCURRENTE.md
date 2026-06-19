# Guia completa para entender el proyecto Pac-Man concurrente

Esta guia esta pensada para que cualquier integrante del grupo pueda entender el proyecto desde cero: que se esta construyendo, que conceptos de Sistemas Operativos aparecen, que hace cada proceso, que hace cada hilo y como se conectan todas las piezas.

El objetivo no es solo programar un Pac-Man. El objetivo real es demostrar concurrencia, sincronizacion e IPC usando C y POSIX.

## 1. Resumen en una frase

El proyecto simula un Pac-Man donde un proceso principal `P0` funciona como scheduler, un proceso `P1` controla a Pac-Man, un proceso `P2` controla a los fantasmas, y dentro de esos procesos se usan hilos, semaforos, mutex y memoria compartida para coordinar el juego.

## 2. Mapa visual rapido

```mermaid
flowchart LR
    P0["P0 Scheduler<br/>decide ticks, prioridades y fin del juego"]
    P1["P1 Pac-Man<br/>lee movimientos y publica posicion"]
    P2["P2 Fantasmas<br/>mueve enemigos y detecta colisiones"]
    SHM[("Memoria compartida<br/>estado global del juego")]
    SEM["Semaforos<br/>dan permiso de turno"]
    MTX["Mutex<br/>protegen datos compartidos"]

    P0 --> SEM
    SEM --> P1
    SEM --> P2
    P0 <--> SHM
    P1 <--> SHM
    P2 <--> SHM
    MTX --> SHM
```

Lectura rapida:

- `P0` decide quien avanza.
- `P1` mueve a Pac-Man.
- `P2` mueve fantasmas y avisa colisiones.
- La memoria compartida permite que los procesos se comuniquen.
- Los semaforos controlan turnos.
- Los mutex evitan datos corruptos.

## 3. Que esta evaluando realmente el proyecto

La profesora no esta pidiendo solamente un juego bonito. Esta pidiendo una simulacion de Sistemas Operativos.

Lo importante es demostrar:

- Procesos creados con `fork()`.
- Hilos creados con `pthread_create()`.
- Comunicacion entre procesos mediante memoria compartida.
- Coordinacion mediante semaforos.
- Proteccion de datos compartidos mediante mutex.
- Un scheduler propio por ticks y prioridades.
- Manejo de condiciones de carrera.
- Separacion clara de responsabilidades entre procesos.

Pac-Man es el escenario. Sistemas Operativos es el tema central.

## 4. Conceptos que todos deben entender

### Proceso

Un proceso es un programa en ejecucion con su propio espacio de memoria.

En este proyecto:

- `P0` es el proceso scheduler.
- `P1` es el proceso de Pac-Man.
- `P2` es el proceso de enemigos.

Normalmente, un proceso no puede modificar directamente la memoria de otro proceso. Por eso necesitamos memoria compartida.

### Hilo

Un hilo es una unidad de ejecucion dentro de un proceso.

Los hilos del mismo proceso comparten memoria entre ellos. Esto es util, pero tambien peligroso: si dos hilos modifican el mismo dato al mismo tiempo, puede aparecer una race condition.

En este proyecto:

- `P1` puede tener hilos para leer movimientos, ejecutar movimientos y publicar estado.
- `P2` puede tener hilos para cada fantasma, para rastrear a Pac-Man y para detectar colisiones.

### Memoria compartida

La memoria compartida es una zona que pueden ver varios procesos.

Sirve para que `P0`, `P1` y `P2` compartan informacion como:

- Tick actual.
- Estado de fin del juego.
- Posicion de Pac-Man.
- Vidas de Pac-Man.
- Prioridades.
- Evento de colision.
- Mapa del laberinto.

Sin memoria compartida, `P0` no podria saber facilmente donde esta Pac-Man o si `P2` detecto una colision.

### Semaforo

Un semaforo sirve para bloquear o despertar procesos/hilos.

En este proyecto, `P0` usara semaforos para dar permiso de ejecucion:

- `sem_pacman_turn`: permite avanzar a `P1`.
- `sem_enemy_turn`: permite avanzar a `P2`.

La idea es que `P1` y `P2` no avancen cuando quieran. Deben esperar a que el scheduler les de turno.

### Mutex

Un mutex protege una seccion critica.

Una seccion critica es una parte del codigo donde se lee o escribe un dato compartido.

Ejemplo:

```c
pthread_mutex_lock(&state_mutex);
shared->pacman_x = nuevo_x;
shared->pacman_y = nuevo_y;
pthread_mutex_unlock(&state_mutex);
```

El mutex evita que otro hilo/proceso lea una posicion incompleta mientras Pac-Man se esta actualizando.

### Race condition

Una race condition ocurre cuando dos o mas hilos/procesos acceden al mismo dato al mismo tiempo y el resultado depende del orden exacto de ejecucion.

Ejemplo peligroso:

- `movement_executor_thread` cambia la posicion de Pac-Man.
- `pacman_publisher_thread` intenta publicarla al mismo tiempo.
- `collision_thread` lee esa posicion justo en medio del cambio.

Solucion: proteger esos datos con mutex.

### Deadlock

Un deadlock ocurre cuando dos o mas hilos/procesos quedan esperando para siempre.

Ejemplo:

- Hilo 1 tiene `mutex_A` y espera `mutex_B`.
- Hilo 2 tiene `mutex_B` y espera `mutex_A`.

Ninguno puede avanzar.

Para evitarlo, el proyecto debe:

- Usar pocos mutex bien definidos.
- Tomar mutex siempre en el mismo orden.
- Liberar mutex rapidamente.
- No hacer operaciones lentas mientras se sostiene un mutex.

### Scheduler

Un scheduler decide quien ejecuta.

En un sistema operativo real, el scheduler decide que proceso usa CPU. En este proyecto, `P0` simula esa idea:

- Incrementa ticks.
- Revisa prioridades.
- Decide si avanza Pac-Man o enemigos.
- Libera el semaforo correspondiente.

### Tick

Un tick es una unidad logica de tiempo del juego.

En cada tick, el scheduler decide que proceso puede ejecutar una accion.

Ejemplo:

```text
Tick 1: ejecuta P2
Tick 2: ejecuta P2
Tick 3: ejecuta P1
Tick 4: ejecuta P2
```

### Prioridad

Cada proceso tiene una prioridad.

Si `prioridad_enemy > prioridad_pacman`, entonces `P2` tiene preferencia.

Si `prioridad_pacman > prioridad_enemy`, entonces `P1` tiene preferencia.

Si ambas prioridades empatan, se usa Round Robin.

### Round Robin

Round Robin significa alternar turnos cuando hay empate.

Ejemplo:

```text
prioridad_pacman = 30
prioridad_enemy = 30

Tick 1: P1
Tick 2: P2
Tick 3: P1
Tick 4: P2
```

Esto evita que un proceso monopolice la simulacion.

### IPC

IPC significa Inter-Process Communication, o comunicacion entre procesos.

En este proyecto hay IPC porque:

- `P1` publica la posicion de Pac-Man.
- `P2` lee la posicion de Pac-Man.
- `P2` publica eventos de colision.
- `P0` lee esos eventos y actualiza vidas.
- `P1` y `P2` piden cambios de prioridad a `P0`.

## 5. Vision general de la arquitectura

```mermaid
flowchart TD
    P0["P0 scheduler_process"]
    P1["P1 pacman_process"]
    P2["P2 enemy_process"]
    SHM["Memoria compartida"]
    MAP["map.txt"]
    PM["pacman_moves.txt"]
    G1["ghost_1_moves.txt"]
    G2["ghost_2_moves.txt"]
    G3["ghost_3_moves.txt"]
    G4["ghost_4_moves.txt"]

    MAP --> P0
    P0 --> SHM
    P0 -- "sem_pacman_turn" --> P1
    P0 -- "sem_enemy_turn" --> P2
    P1 --> SHM
    P2 --> SHM
    PM --> P1
    G1 --> P2
    G2 --> P2
    G3 --> P2
    G4 --> P2
```

La idea principal:

- `P0` manda.
- `P1` mueve a Pac-Man cuando recibe permiso.
- `P2` mueve fantasmas cuando recibe permiso.
- La memoria compartida conecta a todos.

## 6. Que hace P0

`P0` es el proceso principal y representa al scheduler.

No debe mover directamente a Pac-Man. No debe mover directamente a los fantasmas.

Su trabajo es coordinar.

Responsabilidades de `P0`:

- Leer `map.txt`.
- Validar que el mapa tenga `P`, `A`, `B`, `C`, `D`.
- Crear e inicializar memoria compartida.
- Inicializar variables globales:
  - `global_tick`
  - `max_ticks`
  - `game_over`
  - `pacman_lives`
  - `prioridad_pacman`
  - `prioridad_enemy`
- Crear semaforos.
- Crear mutex.
- Crear procesos hijos `P1` y `P2` usando `fork()`.
- Ejecutar el ciclo de ticks.
- Decidir que proceso ejecuta en cada tick.
- Procesar solicitudes de cambio de prioridad.
- Procesar eventos de colision.
- Reducir vidas de Pac-Man.
- Decidir cuando termina el juego.
- Despertar a `P1` y `P2` para que puedan terminar ordenadamente.

Lo mas importante de `P0`: centraliza las decisiones globales.

## 7. Que hace P1

`P1` controla a Pac-Man.

No decide cuando juega. Solo actua cuando `P0` le da permiso con `sem_pacman_turn`.

Responsabilidades de `P1`:

- Leer `pacman_moves.txt`.
- Guardar movimientos en una cola interna.
- Esperar permiso del scheduler.
- Consumir una instruccion por turno.
- Validar si el movimiento choca contra pared.
- Actualizar posicion local de Pac-Man.
- Actualizar puntaje si corresponde.
- Publicar posicion y puntaje en memoria compartida.
- Si lee `SET_PRIORITY <NUMBER>`, no cambia la prioridad directamente.
- En vez de eso, escribe una solicitud para que `P0` la procese.

Hilos recomendados dentro de `P1`:

```text
P1 pacman_process
  movement_reader_thread
  movement_executor_thread
  pacman_publisher_thread
```

### movement_reader_thread

Lee `pacman_moves.txt` y mete instrucciones en una cola.

Ejemplo de instrucciones:

```text
RIGHT
DOWN
LEFT
SET_PRIORITY 35
```

Este hilo produce movimientos.

### movement_executor_thread

Espera permiso del scheduler.

Cuando `P0` hace `sem_post(&sem_pacman_turn)`, este hilo puede consumir una instruccion de la cola.

Luego:

- Calcula nueva posicion.
- Revisa si la celda es pared `X`.
- Si es pared, no se mueve.
- Si es camino, actualiza la posicion.

Este hilo consume movimientos.

### pacman_publisher_thread

Publica el estado actualizado en memoria compartida.

Publica datos como:

- `pacman_x`
- `pacman_y`
- `pacman_score`

Debe usar mutex para no publicar informacion incompleta.

## 8. Que hace P2

`P2` controla a los fantasmas.

Tampoco decide cuando juega. Solo actua cuando `P0` le da permiso con `sem_enemy_turn`.

Responsabilidades de `P2`:

- Controlar 4 fantasmas.
- Leer un archivo distinto para cada fantasma.
- Mover fantasmas cuando el scheduler da turno.
- Validar paredes.
- Leer la posicion actual de Pac-Man desde memoria compartida.
- Detectar colisiones.
- Publicar un evento de colision.

Importante: `P2` no debe quitar vidas directamente. Solo avisa que hubo una colision. `P0` es quien reduce vidas.

Hilos recomendados dentro de `P2`:

```text
P2 enemy_process
  enemy_controller_thread
  ghost_thread_1
  ghost_thread_2
  ghost_thread_3
  ghost_thread_4
  pacman_tracker_thread
  collision_thread
```

### enemy_controller_thread

Espera el turno de enemigos.

Cuando `P0` libera `sem_enemy_turn`, este hilo despierta internamente a los hilos de fantasmas.

Su papel es coordinar el movimiento conjunto de enemigos.

### ghost_thread_1 a ghost_thread_4

Cada fantasma tiene su propio hilo.

Cada hilo:

- Lee su propio archivo.
- Consume una instruccion cuando `P2` tiene turno.
- Calcula nueva posicion.
- Valida paredes.
- Actualiza su posicion interna.

Archivos:

```text
ghost_1_moves.txt
ghost_2_moves.txt
ghost_3_moves.txt
ghost_4_moves.txt
```

### pacman_tracker_thread

Lee la posicion de Pac-Man desde memoria compartida.

Mantiene una copia local dentro de `P2`.

Esto permite que `collision_thread` no tenga que leer memoria compartida todo el tiempo.

### collision_thread

Compara:

- Posicion local de Pac-Man.
- Posiciones internas de los fantasmas.

Si detecta choque, publica en memoria compartida:

- `collision_detected = 1`
- `collision_tick = global_tick`
- `collision_ghost_id = id_del_fantasma`

Despues `P0` procesa ese evento.

## 9. Como funciona un tick

Un tick representa un paso logico del juego.

```mermaid
sequenceDiagram
    participant P0 as P0 Scheduler
    participant P1 as P1 Pac-Man
    participant P2 as P2 Enemigos
    participant SHM as Memoria Compartida

    P0->>SHM: Incrementa global_tick
    P0->>SHM: Revisa prioridades y solicitudes
    P0->>P1: Si gana P1, sem_post sem_pacman_turn
    P1->>SHM: Publica nueva posicion de Pac-Man
    P0->>P2: Si gana P2, sem_post sem_enemy_turn
    P2->>SHM: Publica colision si ocurre
    P0->>SHM: Procesa colision y actualiza vidas
```

En una implementacion estricta, en cada tick se permite avanzar a un proceso:

- O avanza `P1`.
- O avanza `P2`.

Si le toca a `P1`, Pac-Man consume una instruccion.

Si le toca a `P2`, cada fantasma puede consumir una instruccion dentro de ese turno.

## 10. Archivos de entrada

Cada caso tiene su propia carpeta.

```text
cases/
  Caso1/
    map.txt
    pacman_moves.txt
    ghost_1_moves.txt
    ghost_2_moves.txt
    ghost_3_moves.txt
    ghost_4_moves.txt
```

### map.txt

Define el laberinto.

Simbolos:

```text
X = pared
O = camino libre
P = posicion inicial de Pac-Man
A = fantasma 1
B = fantasma 2
C = fantasma 3
D = fantasma 4
* = power pellet opcional
```

Ejemplo:

```text
XXXXXXXXXXXX
XPOOOOOOOOAX
XOXXXXXXOOOX
XBOOOOOOOOCX
XOOXXXXXXOOX
XDOOOOOOOOOX
XXXXXXXXXXXX
```

El programa debe detectar:

- Donde empieza Pac-Man.
- Donde empieza cada fantasma.
- Que celdas son paredes.
- Que celdas son transitables.

### pacman_moves.txt

Contiene instrucciones de Pac-Man.

Ejemplo:

```text
RIGHT
RIGHT
DOWN
LEFT
SET_PRIORITY 35
```

### ghost_N_moves.txt

Cada fantasma tiene su propio archivo.

Ejemplo:

```text
LEFT
UP
RIGHT
DOWN
```

Esto permite que los fantasmas se muevan de forma concurrente dentro de `P2`.

## 11. Memoria compartida

La memoria compartida debe contener la informacion que varios procesos necesitan ver.

Variables sugeridas:

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

## 12. Quien escribe y quien lee cada dato

| Dato | Lo escribe | Lo lee | Proteccion |
| --- | --- | --- | --- |
| `global_tick` | `P0` | `P0`, `P1`, `P2` | `state_mutex` |
| `game_over` | `P0` | `P1`, `P2` | `state_mutex` |
| `pacman_x`, `pacman_y` | `P1` | `P0`, `P2` | `state_mutex` |
| `pacman_lives` | `P0` | `P0`, opcionalmente `P1` | `state_mutex` |
| `prioridad_pacman` | `P0` | `P0` | `priority_mutex` |
| `prioridad_enemy` | `P0` | `P0` | `priority_mutex` |
| `priority_request_active` | `P1` | `P0` | `priority_mutex` |
| `enemy_priority_request_active` | `P2` | `P0` | `priority_mutex` |
| `collision_detected` | `P2` | `P0` | `collision_mutex` |
| `collision_tick` | `P2` | `P0` | `collision_mutex` |
| `collision_ghost_id` | `P2` | `P0` | `collision_mutex` |
| `map_grid` | `P0` al inicio | `P1`, `P2` | Solo lectura despues de inicializar |

Regla mental:

- Si varios escriben, casi seguro necesita mutex.
- Si uno escribe y otros leen mientras el programa corre, tambien conviene proteger.
- Si se inicializa una vez y luego solo se lee, no necesita tanto bloqueo.

## 13. Como se procesa SET_PRIORITY

`SET_PRIORITY` no debe cambiar directamente la prioridad del proceso.

El proceso debe pedir el cambio.

Ejemplo:

```text
P1 lee: SET_PRIORITY 35
```

Flujo correcto:

```mermaid
sequenceDiagram
    participant P1 as P1 Pac-Man
    participant SHM as Memoria Compartida
    participant P0 as P0 Scheduler

    P1->>SHM: pending_priority_pacman = 35
    P1->>SHM: priority_request_active = 1
    P0->>SHM: Lee solicitud al inicio del siguiente tick
    P0->>P0: Valida rango permitido
    P0->>SHM: prioridad_pacman = 35
    P0->>SHM: priority_request_active = 0
```

Por que hacerlo asi:

- Mantiene a `P0` como autoridad central.
- Evita que `P1` o `P2` modifiquen prioridades unilateralmente.
- Hace que el scheduler sea real, no decorativo.

## 14. Como se detecta una colision

Una colision ocurre si Pac-Man y un fantasma terminan en la misma posicion.

Tambien podria considerarse colision por cruce:

```text
Antes:
Pac-Man en (1,1)
Fantasma en (2,1)

Despues:
Pac-Man en (2,1)
Fantasma en (1,1)
```

En ese caso se cruzaron.

Flujo basico:

```text
P2 mueve fantasmas
P2 compara posiciones con Pac-Man
P2 publica collision_detected
P0 lee collision_detected
P0 reduce pacman_lives
P0 decide si game_over = 1
```

P2 detecta. P0 decide.

Esa separacion es importante para la arquitectura.

## 15. Race conditions importantes del proyecto

| Situacion peligrosa | Por que es peligrosa | Solucion |
| --- | --- | --- |
| `P1` actualiza posicion mientras `P2` la lee | `P2` podria leer una posicion incompleta | Proteger con `state_mutex` |
| Varios fantasmas actualizan `ghost_positions[]` | `collision_thread` podria leer datos mezclados | Proteger con mutex interno de `P2` |
| `P1` escribe solicitud de prioridad mientras `P0` la lee | `P0` podria leer solicitud incompleta | Proteger con `priority_mutex` |
| `P2` publica colision mientras `P0` la procesa | Se podria perder o duplicar evento | Proteger con `collision_mutex` |
| `P1` consume cola mientras otro hilo inserta movimientos | La cola podria corromperse | Mutex de cola y semaforo de items |
| `game_over` cambia mientras hilos esperan | Pueden quedar bloqueados | `P0` debe liberar semaforos al terminar |

## 16. Flujo completo del juego

```mermaid
flowchart TD
    A["Inicio"] --> B["P0 lee map.txt"]
    B --> C["P0 inicializa memoria compartida"]
    C --> D["P0 crea semaforos y mutex"]
    D --> E["P0 crea P1 y P2 con fork"]
    E --> F["P1 y P2 esperan turno"]
    F --> G["P0 incrementa tick"]
    G --> H["P0 procesa solicitudes de prioridad"]
    H --> I{"Quien tiene mayor prioridad?"}
    I -->|Pac-Man| J["P0 libera sem_pacman_turn"]
    I -->|Enemigos| K["P0 libera sem_enemy_turn"]
    I -->|Empate| L["Round Robin"]
    L --> J
    L --> K
    J --> M["P1 ejecuta una instruccion"]
    K --> N["P2 mueve fantasmas"]
    M --> O["Actualizar memoria compartida"]
    N --> P["Detectar/publicar colision"]
    O --> Q["P0 revisa fin del juego"]
    P --> Q
    Q -->|Continua| G
    Q -->|Termina| R["P0 activa game_over y libera recursos"]
```

## 17. Estado actual del proyecto

Actualmente ya existe una base inicial con:

- Estructura de carpetas.
- `Makefile`.
- Archivos separados para `P0`, `P1`, `P2`.
- Lector de `map.txt`.
- Validacion de simbolos del mapa.
- Deteccion de posiciones iniciales:
  - `P`
  - `A`
  - `B`
  - `C`
  - `D`
- Caso de prueba `Caso1`.

Archivos principales actuales:

```text
src/
  main.c
  common.c
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
```

Todavia falta implementar:

- `fork()`.
- Memoria compartida real con `mmap()` o `shm_open()`.
- Semaforos reales.
- Mutex interproceso.
- Threads reales con `pthread_create()`.
- Movimiento de Pac-Man.
- Movimiento de fantasmas.
- Cola de movimientos.
- Scheduler por prioridades.
- `SET_PRIORITY`.
- Colisiones.
- Condicion de fin.

## 18. Como explicar el avance actual

Una explicacion clara:

```text
En este avance se construyo la base del proyecto. Ya se separo la arquitectura en tres modulos principales: P0, P1 y P2. P0 representa al scheduler, P1 representa al proceso de Pac-Man y P2 representa al proceso de fantasmas. Tambien se implemento la lectura de map.txt, validando los simbolos permitidos y detectando las posiciones iniciales de Pac-Man y los cuatro fantasmas. Este avance todavia no ejecuta procesos reales con fork, pero deja preparada la estructura para hacerlo en la siguiente etapa.
```

## 19. Orden recomendado para continuar

El siguiente orden tiene sentido:

1. Convertir `P0`, `P1`, `P2` en procesos reales con `fork()`.
2. Crear memoria compartida real.
3. Copiar `shared_state_t` a memoria compartida.
4. Crear semaforos `sem_pacman_turn` y `sem_enemy_turn`.
5. Hacer que `P1` y `P2` esperen su semaforo.
6. Implementar scheduler basico por ticks.
7. Implementar movimiento de Pac-Man.
8. Implementar movimiento de fantasmas.
9. Agregar threads en `P1`.
10. Agregar threads en `P2`.
11. Proteger recursos con mutex.
12. Agregar colisiones.
13. Agregar prioridades y Round Robin.
14. Agregar `SET_PRIORITY`.
15. Agregar finalizacion ordenada.

## 20. Que debe poder responder cada integrante

Todos deberian poder responder:

- Que hace `P0`.
- Que hace `P1`.
- Que hace `P2`.
- Por que usamos procesos.
- Por que usamos hilos.
- Que datos estan en memoria compartida.
- Que datos deben protegerse con mutex.
- Como se evitan race conditions.
- Como el scheduler decide turnos.
- Que pasa cuando hay colision.
- Por que `P2` no baja vidas directamente.
- Por que `P1` no cambia su prioridad directamente.
- Como se termina el juego.

## 21. Idea clave para no perderse

La arquitectura se entiende mejor con esta regla:

```text
P0 decide.
P1 mueve a Pac-Man.
P2 mueve fantasmas y detecta colisiones.
La memoria compartida conecta a todos.
Los semaforos dan permiso.
Los mutex protegen datos.
```

Si recuerdan eso, el proyecto completo empieza a tener sentido.
