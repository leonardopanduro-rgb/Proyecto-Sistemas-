# Plan de correccion y experimentacion

## 1. Objetivo

Corregir, uno por uno, los puntos 2, 6, 7, 10 y 13 del enunciado del
proyecto Pac-Man Concurrente.

Cada punto seguira este ciclo:

1. Ejecutar la prueba que actualmente falla.
2. Guardar la evidencia del estado `No cumple` o `Cumple parcialmente`.
3. Aplicar una correccion pequena y facil de explicar.
4. Repetir exactamente la misma prueba.
5. Completar la fila vacia de revalidacion con el nuevo resultado.
6. Crear un commit exclusivo para ese punto.

No se mezclaran varias correcciones grandes en un mismo commit.

## 2. Reglas de implementacion

- Mantener la arquitectura y nombres actuales.
- Evitar reescribir todo el proyecto.
- Usar funciones pequenas con una sola responsabilidad.
- Reutilizar los mutex y semaforos existentes cuando tenga sentido.
- P0 seguira siendo la unica autoridad sobre `game_over`, vidas y prioridades.
- P1 solo controlara Pac-Man.
- P2 solo controlara fantasmas y publicara eventos de colision.
- Cada error debe propagarse hacia P0.
- Cada cambio debe incluir una prueba reproducible.

## 3. Orden de trabajo

El orden de correccion sera:

1. Punto 7: validacion estricta de `map.txt`.
2. Punto 6: lectura y validacion de movimientos.
3. Punto 10: condiciones de finalizacion.
4. Punto 13: race conditions.
5. Punto 2: arquitectura de P0 y colisiones completas.

El punto 2 se deja al final porque agregar los hilos de P0 y corregir los
cruces requiere que el mapa, los archivos y la finalizacion ya sean
confiables.

---

# Punto 7: validacion estricta de map.txt

## Problemas actuales

- Se aceptan filas de diferente ancho.
- Se aceptan simbolos desconocidos.
- Se acepta mas de un Pac-Man.
- Se aceptan personajes repetidos.
- Las lineas demasiado largas pueden truncarse.
- Cualquier simbolo diferente de `X` puede quedar como transitable.

## Solucion simple

Modificar `cargar_mapa` para validar completamente cada fila antes de
copiarla a memoria compartida.

La logica sera:

1. Leer una fila.
2. Eliminar `\n` y `\r`.
3. Comprobar que la fila no exceda `MAX_X`.
4. La primera fila define el ancho esperado.
5. Las siguientes filas deben tener exactamente el mismo ancho.
6. Aceptar solamente `X`, `O`, `P`, `A`, `B`, `C` y `D`.
7. Contar cuantas veces aparecen `P`, `A`, `B`, `C` y `D`.
8. Rechazar el mapa si alguno falta o aparece mas de una vez.
9. Copiar el mapa a `shared->map_grid` solamente si la validacion termino.

Se agregaran funciones pequenas:

```c
int es_simbolo_mapa_valido(char celda);
int validar_cantidad_personajes(...);
```

## Archivos a tocar

- `map.c`
- `map.h`, solo si se necesita declarar una funcion de prueba

## Pruebas

- Mapa oficial Caso1: debe cargar.
- Mapa con filas desiguales: debe fallar.
- Mapa con `Z`: debe fallar.
- Mapa con dos `P`: debe fallar.
- Mapa sin un fantasma: debe fallar.
- Mapa mayor a `MAX_X` o `MAX_Y`: debe fallar.

## Resultado esperado

P0 debe terminar antes de crear P1 y P2 y devolver codigo diferente de cero
cuando `map.txt` sea invalido.

---

# Punto 6: archivos e instrucciones de movimiento

## Problemas actuales

- Los espacios finales convierten instrucciones validas en desconocidas.
- Un archivo faltante solo genera un mensaje local.
- P0 no recibe el error.
- Una linea vacia puede interpretarse como fin del archivo.
- No existe una validacion unica para las instrucciones.

## Solucion simple

Crear una funcion que limpie y valide cada instruccion antes de insertarla en
la cola o ejecutarla.

La funcion:

1. Eliminara espacios, tabulaciones, `\n` y `\r` al inicio y al final.
2. Aceptara `UP`, `DOWN`, `LEFT` y `RIGHT`.
3. Aceptara `SET_PRIORITY <numero>` cuando tenga exactamente dos elementos.
4. Rechazara instrucciones desconocidas con un mensaje claro.
5. Diferenciara linea vacia, fin de archivo y error de lectura.

Se agregaran estados compartidos simples:

```c
int input_error;
int input_error_process;
```

Si P1 o P2 no puede abrir su archivo, publicara el error bajo
`mutex_shared`. P0 lo procesara y terminara la simulacion.

## Archivos a tocar

- `main.c`
- `shared.h`
- `pacman.c`, si la validacion final se coloca junto al movimiento
- `ghost.c`, si la validacion final se coloca junto al movimiento

La primera opcion sera centralizar la limpieza en `main.c` para evitar
duplicar logica.

## Pruebas

- Instrucciones normales.
- `DOWN ` con espacios finales.
- Lineas vacias entre movimientos.
- `SET_PRIORITY 15`.
- `SET_PRIORITY texto`.
- Movimiento desconocido.
- `pacman_moves.txt` inexistente.
- Cada `ghost_N_moves.txt` inexistente.

## Resultado esperado

Las instrucciones validas se ejecutan una sola vez. Los errores de archivo se
publican a P0 y la simulacion termina con codigo diferente de cero.

---

# Punto 10: limite de ticks y finalizacion

## Problemas actuales

- P0 continua generando ticks cuando ya no quedan movimientos.
- P1 y P2 reciben turnos vacios repetidos.
- `max_ticks` esta fijo en 10.
- Los hijos no comunican claramente que agotaron sus entradas.
- P0 no diferencia fin normal de error de entrada.

## Solucion simple

Agregar banderas de finalizacion en memoria compartida:

```c
int pacman_moves_finished;
int ghost_moves_finished[NUM_GHOSTS];
```

Cada lector activara su bandera cuando llegue realmente a EOF.

P0 terminara cuando ocurra cualquiera de estas condiciones:

1. Se alcanza `max_ticks`.
2. Pac-Man pierde sus tres vidas.
3. Se publica un error de entrada.
4. P1 termino su archivo y los cuatro fantasmas terminaron los suyos.

Para mantener una logica sencilla, `max_ticks` podra recibirse como segundo
argumento:

```bash
./pacman_concurrente Caso1 20
```

Si no se indica, se usara el valor por defecto.

## Archivos a tocar

- `shared.h`
- `main.c`

## Pruebas

- Archivos con una sola instruccion.
- Todos los archivos vacios.
- Pac-Man termina antes que los fantasmas.
- Fantasmas terminan antes que Pac-Man.
- Finalizacion por `max_ticks`.
- Finalizacion por vidas en cero.
- Finalizacion por error de entrada.

## Resultado esperado

P0 activa `game_over`, libera los semaforos necesarios, espera P1/P2 mediante
`waitpid` y devuelve el codigo de salida correcto.

---

# Punto 13: mitigacion de race conditions

## Problemas actuales

ThreadSanitizer detecto carreras sobre las banderas `terminar` de P1 y P2.

Las lecturas y escrituras se realizan desde varios threads sin usar siempre
el mismo mecanismo de proteccion.

## Solucion simple

No se usaran atomicos nuevos para mantener el codigo dentro de los conceptos
ya empleados.

Para P1:

- Proteger `data->terminar` con `mutex_cola`.
- Crear funciones:

```c
int pacman_debe_terminar(PacmanThreadData *data);
void marcar_pacman_terminar(PacmanThreadData *data);
```

Para P2:

- Agregar `pthread_mutex_t mutex_terminar`.
- Crear funciones:

```c
int enemy_debe_terminar(EnemyThreadData *data);
void marcar_enemy_terminar(EnemyThreadData *data);
```

Todos los threads deberan usar estas funciones. Ningun thread leera o
escribira `terminar` directamente.

Los mutex se inicializaran y destruiran junto con los demas recursos del
proceso.

## Archivos a tocar

- `main.c`

## Pruebas

- Caso1 con ThreadSanitizer.
- Caso2 con ThreadSanitizer.
- Caso3 con ThreadSanitizer.
- Ejecuciones repetidas con `timeout`.
- Finalizacion por `max_ticks`.
- Finalizacion por error de archivo.

## Resultado esperado

ThreadSanitizer debe terminar con cero reportes de race conditions.

---

# Punto 2: arquitectura de procesos, threads y colisiones

## Problemas actuales

- P0 no crea `tick_thread`, `scheduler_thread` ni `signal_thread`.
- La colision se comprueba solamente despues de mover fantasmas.
- Si Pac-Man entra en una posicion ocupada y el fantasma se aleja, el choque
  se pierde.
- No se detecta intercambio de posiciones.

## Parte A: colisiones completas

Se mantendran las posiciones de fantasmas dentro de P2.

`EnemyThreadData` guardara tambien:

```c
int pacman_previous_y;
int pacman_previous_x;
int ghost_previous_y[NUM_GHOSTS];
int ghost_previous_x[NUM_GHOSTS];
```

El turno de P2 sera:

1. Actualizar la posicion actual de Pac-Man.
2. Comprobar colision antes de mover fantasmas.
3. Guardar posiciones anteriores de los fantasmas.
4. Mover los cuatro fantasmas.
5. Comprobar choque y cruce despues del movimiento.
6. Publicar un solo evento de colision.
7. Avisar a P0 mediante `sem_turn_done`.

Existe cruce cuando:

```text
Pac-Man actual == posicion anterior del fantasma
y
Pac-Man anterior == posicion actual del fantasma
```

P2 seguira sin modificar vidas ni `game_over`.

## Parte B: threads de P0

Se agregara una estructura local de P0:

```c
typedef struct {
    SharedData *shared;
    int turno_actual;
    int terminar;
    pthread_mutex_t mutex_scheduler;
    sem_t sem_tick_start;
    sem_t sem_tick_ready;
    sem_t sem_turn_ready;
    sem_t sem_tick_finished;
} SchedulerThreadData;
```

Responsabilidades:

- `tick_thread`: espera `sem_tick_start`, incrementa `global_tick` bajo mutex
  y publica `sem_tick_ready`.
- `scheduler_thread`: procesa solicitudes, compara prioridades y escribe
  `turno_actual`.
- `signal_thread`: libera `sem_pacman_turn` o `sem_enemy_turn`, espera
  `sem_turn_done` y confirma el final del tick.

La coordinacion sera secuencial mediante semaforos para que el flujo sea facil
de seguir:

```text
tick_thread
    -> scheduler_thread
        -> signal_thread
            -> P1 o P2
```

No se permitira busy waiting.

## Archivos a tocar

- `main.c`
- `shared.h`, solamente si una posicion adicional debe ser compartida

Las posiciones anteriores de fantasmas permaneceran locales a P2.

## Pruebas

- Verificar P0, P1 y P2 con `ps -L` o trazas de inicio de threads.
- Prioridades iguales: alternancia Round Robin.
- Prioridad P1 mayor: P1 recibe turno.
- Prioridad P2 mayor: P2 recibe turno.
- Pac-Man entra en celda ocupada antes de que el fantasma se mueva.
- Pac-Man y fantasma intercambian posiciones.
- Dos fantasmas llegan a Pac-Man en el mismo turno.
- Finalizacion sin threads bloqueados.

## Resultado esperado

- P0 crea exactamente sus tres threads.
- P1 y P2 conservan sus threads actuales.
- Cada tick mantiene un orden determinista.
- Choques y cruces generan un evento.
- Solo P0 reduce vidas y decide `game_over`.

---

# 4. Archivos previstos

| Archivo | Cambios previstos |
| --- | --- |
| `main.c` | Lectura de movimientos, finalizacion, races, threads P0 y colisiones |
| `shared.h` | Banderas de error y finalizacion de archivos |
| `map.c` | Validacion estricta de dimensiones, simbolos y personajes |
| `map.h` | Solo declaraciones adicionales si son necesarias |
| `pacman.c` | Ajuste minimo si la validacion de instrucciones lo requiere |
| `ghost.c` | Ajuste minimo si la validacion de instrucciones lo requiere |

No se modificaran todos los archivos al mismo tiempo.

# 5. Estrategia de commits

Los commits previstos son:

```text
Fix point 7 map validation
Fix point 6 movement input handling
Fix point 10 termination conditions
Fix point 13 thread races
Fix point 2 scheduler threads and collisions
Complete experimentation results
```

# 6. Criterio para completar la tabla

Una fila de revalidacion solo se marcara como `Cumple` cuando:

1. La prueba original que fallo ahora pase.
2. Los tres casos oficiales sigan funcionando.
3. No aparezcan nuevos deadlocks.
4. AddressSanitizer y UndefinedBehaviorSanitizer no reporten errores.
5. ThreadSanitizer no reporte carreras para el punto 13.
6. P0 espere correctamente a P1 y P2.

# 7. Alcance de este primer commit

Este commit crea solamente el plan.

Todavia no modifica:

- Procesos.
- Threads.
- Semaforos.
- Mutex.
- Movimientos.
- Colisiones.
- Scheduler.
