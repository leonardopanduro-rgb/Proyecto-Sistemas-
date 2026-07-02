# Auditoría contra PDF y rúbrica

> Rama auditada: `bonus-renderer`. No se encontró PDF, README ni PLAN activo;
> se tomó como especificación contractual la lista de requisitos proporcionada.
> La auditoría corresponde al código posterior al commit `6886faf`.

## 1. Resumen ejecutivo

| Área | Estado | Severidad | Comentario breve |
|---|---|---|---|
| Procesos P0/P1/P2 | Cumple | Baja | P0 crea P1/P2 con `fork()`, controla el ciclo y recoge ambos con `waitpid()`. |
| Hilos requeridos | Cumple | Baja | Están los 3 hilos de P0, 3 de P1 y 7 de P2 con nombres y responsabilidades esperadas. |
| Scheduler por ticks | Cumple | Baja | Cadena P0-main → tick → scheduler → signal → P1/P2, bloqueada por semáforos. |
| Prioridades y Round Robin | Cumple | Baja | Mayor prioridad gana; en empate alterna usando `ultimo_turno`. |
| `SET_PRIORITY` | Cumple | Baja | P1/P2 publican buzones; P0 valida 1–100 y aplica al inicio del tick siguiente. |
| Memoria compartida | Cumple | Baja | `mmap(MAP_SHARED | MAP_ANONYMOUS)`, mutex process-shared y semáforos `pshared=1`. |
| Validación de mapa | Cumple | Baja | Valida tamaño, rectangularidad, símbolos, vacíos y unicidad de P/A/B/C/D. |
| Archivos de movimientos | Cumple | Baja | Valida formato, faltantes, vacíos, EOF e instrucciones demasiado largas. |
| Movimiento | Cumple | Baja | Límites y paredes se validan; un intento contra pared consume turno sin mover. |
| Colisiones y vidas | Parcial | Alta | Misma celda y cruce existen, pero “celda ocupada” puede declarar colisión sin intercambio real. |
| Finalización normal | Cumple | Media | Despierta, hace `join`, `waitpid`, destruye recursos y ejecuta `munmap`. |
| Cierre anormal/orphans | No cumple | Alta | No hay `sigaction`, `PDEATHSIG` ni protocolo si P0 muere abruptamente. |
| Race conditions | Parcial | Alta | Flags `terminar` están protegidos, pero hay lecturas/escrituras directas de `game_over` y lectura de `global_tick` sin mutex. |
| ThreadSanitizer | No determinable | Media | Compila, pero aborta al iniciar por `unexpected memory mapping`; no valida ausencia de carreras. |

**Dictamen ejecutivo:** la arquitectura pedida está presente y el caso normal
funciona, pero no es correcto afirmar que el programa está libre de carreras.
Además, la regla extra de “celda ocupada” puede descontar vidas indebidamente.

## 2. Evidencia de arquitectura

| Requisito del PDF | Encontrado en el código | Archivo:línea | Cumple | Observación |
|---|---|---|---|---|
| P0 = `scheduler_process` | Función principal del scheduler | `main.c:1632` | Sí | Carga mapa, crea hijos, inicia hilos y controla cierre. |
| P1 = `pacman_process` | Proceso de Pac-Man | `main.c:878` | Sí | Crea reader, executor y publisher. |
| P2 = `enemy_process` | Proceso de enemigos | `main.c:1389` | Sí | Crea controller, cuatro ghosts, tracker y collision. |
| P0 crea P1 con `fork()` | Primer hijo | `main.c:1700-1710` | Sí | P1 entra en `pacman_process`. |
| P0 crea P2 con `fork()` | Segundo hijo | `main.c:1715-1725` | Sí | P2 entra en `enemy_process`. |
| P0 controla avance global | Ciclo y cadena de hilos | `main.c:1774-1827` | Sí | P0-main dispara y espera un tick completo. |
| P1 espera permiso | `sem_pacman_turn` | `main.c:782-824` | Sí | Executor no consume cola antes de `sem_wait`. |
| P2 espera permiso | `sem_enemy_turn` | `main.c:1299-1351` | Sí | Controller coordina todo el turno de P2. |
| Un proceso por tick | `signal_thread` publica un solo semáforo | `main.c:1601-1613` | Sí | Espera `sem_turn_done` antes de finalizar tick. |
| Memoria común antes de `fork()` | `mmap`, inicialización y mapa previos | `main.c:127-214`, `main.c:1642-1700` | Sí | Orden correcto. |
| P3 renderer opcional | `fork()` condicionado por `--render` | `main.c:1727-1745` | Extra | No reemplaza P0/P1/P2. |

## 3. Verificación exacta de procesos e hilos

| Proceso | Hilo requerido por PDF | Función real encontrada | `pthread_create` | `pthread_join` | Estado | Evidencia |
|---|---|---|---|---|---|---|
| P0 | `tick_thread` | `tick_thread` | Sí | Sí | Cumple | `main.c:1521`, `1774`, `1858` |
| P0 | `scheduler_thread` | `scheduler_thread` | Sí | Sí | Cumple | `main.c:1555`, `1775`, `1859` |
| P0 | `signal_thread` | `signal_thread` | Sí | Sí | Cumple | `main.c:1588`, `1776`, `1860` |
| P1 | `movement_reader_thread` | `movement_reader_thread` | Sí | Sí | Cumple | `main.c:714`, `892`, `910` |
| P1 | `movement_executor_thread` | `movement_executor_thread` | Sí | Sí | Cumple | `main.c:776`, `893`, `896` |
| P1 | `pacman_publisher_thread` | `pacman_publisher_thread` | Sí | Sí | Cumple | `main.c:841`, `894`, `911` |
| P2 | `enemy_controller` | `enemy_controller` | Sí | Sí | Cumple | `main.c:1292`, `1411`, `1421` |
| P2 | `ghost_thread_1` | wrapper de `ghost_thread_generico` | Sí | Sí | Cumple | `main.c:1091`, `1413`, `1435` |
| P2 | `ghost_thread_2` | wrapper de `ghost_thread_generico` | Sí | Sí | Cumple | `main.c:1095`, `1414`, `1436` |
| P2 | `ghost_thread_3` | wrapper de `ghost_thread_generico` | Sí | Sí | Cumple | `main.c:1099`, `1415`, `1437` |
| P2 | `ghost_thread_4` | wrapper de `ghost_thread_generico` | Sí | Sí | Cumple | `main.c:1103`, `1416`, `1438` |
| P2 | `pacman_tracker_thread` | `pacman_tracker_thread` | Sí | Sí | Cumple | `main.c:1113`, `1418`, `1440` |
| P2 | `collision_thread` | `collision_thread` | Sí | Sí | Cumple parcial | `main.c:1163`, `1419`, `1441`; la detección contiene un falso positivo posible. |

Observaciones estrictas:

- P0 implementa realmente las tres responsabilidades: incremento en
  `main.c:1534-1537`, selección en `main.c:1568-1575` y señal/espera en
  `main.c:1605-1617`.
- P1 usa una cola acotada protegida por `mutex_cola`, `sem_hay_espacio` y
  `sem_hay_movimientos` (`main.c:651-705`).
- P2 coordina tracker → cuatro fantasmas → colisión de forma determinista
  (`main.c:1312-1344`). Los fantasmas se mueven concurrentemente, pero cada uno
  adquiere el mismo `mutex_ghosts`, por lo que sus actualizaciones quedan
  serializadas (`main.c:1055-1059`).
- No se comprueban los códigos de retorno de `pthread_create`, `pthread_join`,
  `sem_init` ni `pthread_mutex_init`. Un fallo de creación podría llevar a
  `join` inválido o bloqueo indefinido.

## 4. Scheduler, prioridades y SET_PRIORITY

### Flujo por tick

1. P0-main publica `sem_tick_start` y espera `sem_tick_finished`
   (`main.c:1801-1805`).
2. `tick_thread` incrementa `global_tick` bajo `mutex_shared`
   (`main.c:1528-1542`).
3. `scheduler_thread` procesa solicitudes y elige turno
   (`main.c:1562-1575`).
4. `signal_thread` despierta exclusivamente P1 o P2 y espera
   `sem_turn_done` (`main.c:1595-1617`).
5. P1 confirma en `main.c:865`; P2 confirma en `main.c:1351`.

### Prioridades y Round Robin

- Prioridades iniciales: 30/30 (`main.c:182-183`).
- Mayor prioridad selecciona P1 o P2 (`main.c:509-519`).
- En empate, `ultimo_turno` alterna 1 ↔ 2 (`main.c:521-529`).
- Se inicializa en 2 para que el primer empate entregue P1 (`main.c:1475-1479`).

**Conclusión:** el Round Robin sí alterna; no favorece siempre al mismo proceso.

### `SET_PRIORITY`

- El parser acepta exactamente comando + entero (`main.c:273-283`).
- P1 publica solicitud, no cambia prioridad efectiva (`main.c:799-801`,
  `438-448`).
- Cada ghost puede publicar solicitud de P2 (`main.c:1049-1054`, `450-460`).
- P0 consume buzones al inicio de la etapa scheduler del siguiente tick
  (`main.c:1568`, `462-501`).
- Rango válido 1–100 (`main.c:341-343`).
- Buzones y actualización están protegidos por `mutex_shared`
  (`main.c:438-501`).

Riesgo menor: los cuatro ghosts pueden ejecutar `SET_PRIORITY` en el mismo turno;
como comparten un solo buzón, la última escritura que obtenga el mutex gana. El
resultado es seguro a nivel de memoria, pero el orden entre solicitudes de P2
es no determinista y no existe una cola de solicitudes.

## 5. Memoria compartida y sincronización

| Campo esperado | Existe | Protección actual | Archivo:línea | Riesgo |
|---|---|---|---|---|
| `global_tick` | Sí | Escritura con mutex; algunas lecturas directas | `shared.h:13`, `main.c:1534-1537`, `1784-1785` | Alto: carrera P0 tick/main. |
| `max_ticks` | Sí | P0 lo configura antes de `fork`; después solo lectura | `shared.h:14`, `main.c:1649-1651` | Bajo. |
| `game_over` | Sí | Algunas escrituras con mutex; muchas lecturas directas | `shared.h:15`, `main.c:785`, `850`, `1040`, `1122`, `1172`, `1301`, `1784` | Alto. |
| `pacman_x/y` | Sí | Movimiento y tracker usan `mutex_shared` | `shared.h:22-23`, `main.c:813-817`, `1126-1131` | Bajo; inicialización previa a fork. |
| `pacman_score` | Sí | Actualizado dentro del lock de P1 | `shared.h:24`, `main.c:813-817` | Bajo. |
| `pacman_lives` | Sí | P0 reduce bajo `mutex_shared`; lecturas finales directas | `shared.h:25`, `main.c:546-569`, `1838` | Medio. |
| `collision_detected/tick/id` | Sí | P2 y P0 usan `mutex_shared` | `shared.h:38-40`, `main.c:1246-1271`, `546-569` | Bajo. |
| `prioridad_pacman/enemy` | Sí | Actualización con mutex; elección sin mutex compartido | `shared.h:42-43`, `main.c:462-501`, `504-519` | Bajo/medio; hoy solo scheduler escribe efectivas. |
| Buzón P1 | Sí | `mutex_shared` | `shared.h:45-46`, `main.c:438-444` | Bajo. |
| Buzón P2 | Sí | `mutex_shared` | `shared.h:48-49`, `main.c:450-456` | Bajo; última solicitud simultánea gana. |
| `map_grid` | Sí | Escrito antes de fork; después solo lectura | `shared.h:20`, `map.c:170-188` | Bajo. |
| `ghost_x/y` visibles | Sí | P2 publica y renderer lee con mutex | `shared.h:35-36`, `main.c:1246-1255`, `renderer.c:123-145` | Bajo. |
| Flags EOF | Sí | `mutex_shared` | `shared.h:70-71`, `main.c:387-435` | Bajo. |
| Cola P1 | Local a P1 | Mutex + dos semáforos | `main.c:615-619`, `651-705` | Bajo. |
| `terminar` P1 | Sí, local | `mutex_cola` | `main.c:635-649` | Bajo. |
| `terminar` P2 | Sí, local | `mutex_terminar` | `main.c:978-992` | Bajo. |
| `terminar` P0 | Sí, local | `mutex_scheduler` | `main.c:1501-1515` | Bajo. |

La memoria se crea correctamente con `PROT_READ | PROT_WRITE` y
`MAP_SHARED | MAP_ANONYMOUS` (`main.c:127-135`). El mutex usa
`PTHREAD_PROCESS_SHARED` (`main.c:199-203`) y los semáforos entre procesos usan
`pshared=1` (`main.c:205-213`). Los semáforos internos de cada proceso usan
correctamente `pshared=0`.

## 6. Validación de mapa y archivos

### Mapa

| Comprobación | Estado | Evidencia |
|---|---|---|
| `map.txt` se abre en P0 antes de fork | Cumple | `main.c:1656-1670`; forks desde `1700`. |
| Símbolos `X O P A B C D` | Cumple | `map.c:10-17`. |
| Máximo `MAX_Y/MAX_X` | Cumple | `map.c:80-92`. |
| Filas rectangulares | Cumple | `map.c:94-106`. |
| Fila vacía | Cumple | `map.c:74-78`. |
| Símbolo desconocido | Cumple | `map.c:108-118`. |
| Un único Pac-Man | Cumple | `map.c:120-124`, `155-159`. |
| Un único A/B/C/D | Cumple | `map.c:126-132`, `161-168`. |
| Archivo completamente vacío | Cumple | `map.c:150-153`. |
| Publicación solo tras validar todo | Cumple | `map.c:170-188`. |

### Movimientos

- Se esperan los cinco nombres correctos (`main.c:882-884`, `932-942`).
- Cada ghost abre su propio archivo (`main.c:1017-1024`).
- Se aceptan `UP/DOWN/LEFT/RIGHT/SET_PRIORITY entero`
  (`main.c:265-284`).
- Se recortan espacios y CR/LF (`main.c:238-259`).
- Línea demasiado larga, formato inválido y error de lectura se reportan
  (`main.c:290-324`).
- Archivo faltante publica error y P0 termina con exit 1 (`main.c:719-732`,
  `1023-1031`, `362-380`, `1904`).
- EOF activa flags por actor (`main.c:819-822`, `1067-1072`) y P0 termina cuando
  todos están agotados (`main.c:418-435`, `1792-1799`).

Prueba observada: con los cinco archivos vacíos el programa realizó dos ticks
de coordinación para que P1 y P2 descubrieran sus EOF y luego terminó por
agotamiento, no avanzó hasta `max_ticks=100`. Esto es aceptable funcionalmente,
aunque no es terminación previa al primer tick.

## 7. Colisiones y vidas

| Regla | Estado | Evidencia/observación |
|---|---|---|
| Misma celda | Cumple | `main.c:1224-1227`. |
| Cruce/intercambio exacto | Cumple | Compara actual Pac-Man con anterior ghost y viceversa en `main.c:1211-1220`. |
| Conserva posiciones anteriores | Cumple | Pac-Man `1135-1144`; ghosts `1318-1324`. |
| P2 solo publica evento | Cumple | `collision_thread` escribe evento en `main.c:1257-1271`; no toca vidas. |
| P0 reduce vidas | Cumple | `main.c:532-569`. |
| P0 activa `game_over` al llegar a cero | Cumple | `main.c:553-561`. |
| Evento se consume una vez | Cumple | P0 limpia campos en `main.c:564-566`. |
| Sin falsos positivos | **No cumple** | `main.c:1228-1235` marca colisión si Pac-Man entra a la posición anterior del ghost aunque el ghost se haya movido a otra celda distinta de la posición anterior de Pac-Man. Eso no es necesariamente misma celda ni intercambio. |

El bloque “celda ocupada” extiende la regla solicitada y puede descontar una
vida cuando ambos actores terminaron separados y tampoco intercambiaron celdas.
Debe eliminarse o justificarse explícitamente en el enunciado.

## 8. Race conditions probables

| Variable/recurso | Quién escribe | Quién lee | Protección actual | Riesgo | Corrección recomendada |
|---|---|---|---|---|---|
| `global_tick` | `tick_thread` | P0-main, P1/P2 logs y condiciones | Escritor con mutex; varios lectores sin lock | Alto | Encapsular toda lectura/escritura bajo `mutex_shared` o usar snapshot protegido. |
| `game_over` | P0 y renderer SDL | P0/P1/P2/P3 y múltiples hilos | Parcial; muchas lecturas directas | Alto | Helpers `get/set_game_over` con `mutex_shared` o tipo atómico compatible con diseño. |
| `pacman_lives` | P0 | P0-main/renderer | Escritura protegida; cierre lee directo | Medio | Leer causa final dentro de lock/snapshot. |
| Prioridades efectivas | `scheduler_thread` | `scheduler_thread` | Escritura bajo mutex, elección fuera del mutex | Bajo/medio | Elegir dentro del mismo lock o copiar ambas prioridades bajo lock. |
| Buzón prioridad P2 | 4 ghost threads | P0 scheduler | Mutex correcto, un único slot | Lógico medio | Cola o política explícita si llegan varias solicitudes en un turno. |
| Evento de colisión | P2 collision | P0 | `mutex_shared` en ambos | Bajo | Mantener. |
| Posición Pac-Man | P1 executor | P2 tracker/P3 | `mutex_shared` | Bajo | Mantener; evitar lecturas de inicialización posteriores al fork sin lock. |
| Posiciones locales ghosts | ghost threads/controller/collision | P2 | `mutex_ghosts` + semáforos de fase | Bajo | Mantener. |
| Cola P1 | reader/executor | P1 | `mutex_cola` + semáforos | Bajo | Mantener. |
| `lector_termino` | reader | executor | `mutex_cola` | Bajo | Mantener. |
| `terminar` P1 | P1 process/threads | P1 threads | `mutex_cola` | Bajo | Mantener. |
| `terminar` P2 | P2 process/controller/workers | P2 threads | `mutex_terminar` | Bajo | Mantener. |
| `terminar` P0 | P0-main/hilos | P0 threads | `mutex_scheduler` | Bajo | Mantener. |

Carrera concreta por estructura: `tick_thread` escribe `global_tick` bajo
`mutex_shared` (`main.c:1534-1537`), mientras P0-main lo lee en la condición
`main.c:1784-1785` sin adquirir ese mutex. Usar mutex solo en el escritor no
sincroniza al lector.

TSan no pudo confirmar ni descartar estas carreras: abortó antes de ejecutar el
programa. Además, TSan no cubre adecuadamente todos los accesos entre procesos
por `mmap`; el análisis estático sigue siendo obligatorio.

## 9. Pruebas ejecutadas

| Comando | Resultado | Salida relevante | Estado |
|---|---|---|---|
| `make clean && make` | Exit 0 | Compiló `main.c map.c pacman.c ghost.c renderer.c` con `-Wall -Wextra -g`; sin warnings. | OK |
| `timeout 15s ./pacman_concurrente Caso1 30` | Exit 0 | Ejecutó ticks 1–10 y terminó por vidas agotadas. | OK |
| Caso temporal sin `pacman_moves.txt` | Exit 1 | “Error de entrada reportado por P1”, `game_over=1`, fin por error. | OK |
| Caso temporal con cinco archivos vacíos, `max_ticks=100` | Exit 0 | Dos ticks; luego “Fin por agotamiento de entradas de P1 y P2”. | OK |
| Caso temporal con `UP` y luego `Z` | Exit 1 | “Instruccion de movimiento invalida: Z”; fin por error. | OK |
| `make ... -fsanitize=thread` | Exit 0 | Binario TSan construido. | OK build |
| `TSAN_OPTIONS='halt_on_error=0 exitcode=66' timeout 20s ./pacman_concurrente Caso1 20` | Exit 66 | `FATAL: ThreadSanitizer: unexpected memory mapping ...` | No determinable |
| Búsqueda estática con `rg` de `pthread_create`, semáforos, mutex, `fork/waitpid/mmap`, `SET_PRIORITY`, colisiones | Completada | Confirmó ubicaciones indicadas en este reporte. | OK |

El Makefile no ofrece targets `tsan` ni `normal`; el sanitizer se inyectó por
variables de línea de comandos. Al terminar se restauró el build normal.

## 10. Puntuación estimada según rúbrica

| Criterio | Máximo | Puntaje estimado | Justificación | Para llegar al máximo |
|---|---:|---:|---|---|
| Arquitectura de procesos e hilos | 4 | **4.0** | P0/P1/P2 y los 13 hilos requeridos existen, se crean y se unen; responsabilidades reales coinciden. | Comprobar retornos de creación/join aumentaría robustez, pero la arquitectura está. |
| Sincronización y mitigación de carreras | 3 | **2.0** | Colas, flags `terminar`, estados locales y eventos principales usan mutex/semáforos; persisten accesos inseguros a `game_over/global_tick`. | Proteger todos los accesos compartidos y volver a ejecutar TSan en entorno compatible. |
| Scheduler y prioridades | 3 | **3.0** | Tick, decisión, señal, espera, prioridad, RR y buzones funcionan según requisito. | Definir política para solicitudes simultáneas de ghosts. |
| Memoria compartida, archivos y reglas | 4 | **3.2** | mmap, mapa, movimientos, paredes, EOF y cleanup normal están bien; falso positivo de colisión y cierre anormal restan. | Corregir “celda ocupada”, señales/orphans y manejo de fallos parciales. |
| **Total estimado** | **14** | **12.2 / 14** | Cumplimiento estructural alto con riesgos concurrentes y lógicos concretos. | Resolver P0/P1 de la sección siguiente. |

La puntuación es una estimación técnica, no sustituye la decisión del docente.

## 11. Lista priorizada de correcciones

| Prioridad | Problema | Archivo probable | Función probable | Cambio recomendado | Riesgo si no se corrige |
|---|---|---|---|---|---|
| P0 | Carrera sobre `global_tick` entre P0-main y `tick_thread` | `main.c` | `scheduler_process`, `tick_thread` | Leer condición y valor mediante snapshot bajo `mutex_shared`. | Comportamiento indefinido; ticks/cierre inconsistentes. |
| P0 | `game_over` se lee y escribe sin disciplina uniforme | `main.c`, `renderer_sdl.c` | executor, publisher, controller, workers, scheduler | Centralizar get/set bajo mutex o usar atómico. | Hilos/procesos pueden no observar cierre o actuar un turno adicional. |
| P0 | Falso positivo “celda ocupada” | `main.c` | `collision_thread` | Mantener solo misma celda e intercambio exacto, salvo regla explícita del PDF. | Descuento incorrecto de vidas y final prematuro. |
| P1 | Hijos huérfanos si P0 muere abruptamente | `main.c` | `scheduler_process`, P1/P2 | Añadir protocolo de señales/PDEATHSIG y cleanup; probar con `kill -KILL`/`pgrep`. | P1/P2 pueden quedar vivos y bloqueados. |
| P1 | Fallo parcial de `fork()` no limpia hijos previos | `main.c` | `scheduler_process` | Si falla P2/P3, señalizar y recoger hijos ya creados antes de `munmap`. | Uso de memoria compartida destruida, huérfanos o bloqueo. |
| P1 | Retornos POSIX ignorados | `main.c` | inicializadores y creación de hilos | Verificar `pthread_create/join`, `sem_init`, mutex y `waitpid`; propagar errores. | Se informa “correctamente” aunque falle la creación/salida. |
| P1 | `waitpid` no interpreta `status` | `main.c` | `scheduler_process` | Usar `WIFEXITED`, `WEXITSTATUS`, `WIFSIGNALED`. | Falsos mensajes de éxito. |
| P2 | Múltiples `SET_PRIORITY` de ghosts pisan el mismo buzón | `main.c` | `solicitar_prioridad_enemy` | Definir última-gana formalmente o implementar cola. | Resultado no determinista entre archivos de fantasmas. |
| P2 | TSan no ejecutable en este entorno | Makefile/documentación | target nuevo `tsan` | Añadir target y probar en host compatible; complementar con Helgrind/DRD según disponibilidad. | Carreras futuras pueden pasar inadvertidas. |
| P2 | Salida duplicada al redirigir por buffers heredados en `fork()` | `main.c` | `scheduler_process` | `fflush(NULL)` antes de los forks o ajustar buffering. | Logs repetidos; afecta evidencia, no lógica central. |

## Conclusión honesta

**Entregable con riesgos.** El proyecto cumple de forma sólida la arquitectura,
los nombres y responsabilidades de hilos, el scheduler, las prioridades,
`SET_PRIORITY`, la memoria compartida, el mapa y los archivos. No recomiendo
presentarlo como “libre de race conditions”: hay accesos compartidos sin mutex y
TSan no pudo ejecutarse. Antes de una entrega definitiva deberían corregirse
como mínimo la carrera de `global_tick`, la disciplina de `game_over` y el falso
positivo de colisión por “celda ocupada”.

## 12. Estado posterior a las correcciones

Esta sección registra los cambios realizados en la rama
`fix/auditoria-rubrica`. Las secciones 1–11 se conservan como baseline de la
auditoría previa.

| Hallazgo baseline | Corrección aplicada | Validación | Estado nuevo |
|---|---|---|---|
| Carrera entre P0-main y `tick_thread` sobre `global_tick` | P0-main obtiene un snapshot de control bajo `mutex_shared`; los logs de P1/P2 usan `obtener_global_tick()`. | Revisión estática: todo acceso concurrente restante está dentro de `mutex_shared`. | Corregido |
| Lecturas directas de `game_over` | Se añadieron `obtener_game_over()`, `establecer_game_over()` y `obtener_control_juego()`. | Caso normal, faltante, vacío e inválido pasan con los códigos esperados. | Corregido |
| Lectura de prioridades fuera del mutex | `elegir_turno_por_prioridad()` copia ambas prioridades bajo `mutex_shared`. | Build limpio y ejecución de scheduler correcta. | Corregido |
| Falso positivo por “celda ocupada” | Se eliminó esa tercera condición; permanecen misma celda e intercambio exacto. | Revisión estática de `collision_thread()`. | Corregido |
| P1/P2/P3 huérfanos al morir P0 | Cada hijo configura `PR_SET_PDEATHSIG` con `SIGTERM` y verifica el PPID después de `prctl()`. | Se capturaron tres hijos, se mató P0 con `SIGKILL` y `CHILDREN_ALIVE_AFTER` quedó vacío. | Corregido en Linux |
| Fallo de segundo/tercer `fork()` deja hijos previos | `terminar_hijos_por_error()` marca cierre, despierta, señaliza y recoge hijos antes de `munmap`. | Revisión estática de todas las ramas de error de `fork()`. | Corregido |
| `waitpid()` siempre reportaba éxito | `esperar_hijo_correctamente()` interpreta `WIFEXITED`, `WEXITSTATUS` y `WIFSIGNALED`; el exit global refleja fallos de hijos. | Regresión normal muestra P1/P2 con salida correcta. | Corregido |
| No había target TSan | Se añadieron `make normal` y `make tsan`. | El binario enlaza `libtsan`; el runtime todavía aborta por `unexpected memory mapping`. | Target corregido; ejecución no determinable |

### Regresión posterior

| Prueba | Resultado posterior |
|---|---|
| `make normal` | Exit 0, sin warnings. |
| Caso1 normal | Exit 0, fin por vidas agotadas. |
| Movimiento faltante | Exit 1, fin por error de entrada. |
| Cinco movimientos vacíos | Exit 0, fin por agotamiento. |
| Movimiento inválido | Exit 1, fin por error de entrada. |
| Renderer ncurses | Compila sin warnings. |
| Renderer SDL2 | Compila sin warnings. |
| Muerte forzada de P0 | P1/P2/P3 terminan; no quedaron hijos vivos. |
| TSan | Build correcto; runtime no compatible en este entorno. |

### Riesgos que permanecen

- No se comprueban todavía todos los retornos de `pthread_create`, `sem_init`,
  `pthread_mutex_init` y `pthread_join`.
- `PR_SET_PDEATHSIG` es una extensión Linux, adecuada para el entorno probado,
  pero no POSIX portable.
- El único buzón de prioridad de P2 conserva política implícita “última
  solicitud gana” cuando varios fantasmas solicitan prioridad en el mismo turno.
- TSan continúa sin producir un análisis dinámico por incompatibilidad del
  runtime con el mapeo de memoria del entorno.

**Dictamen posterior:** **entregable**, con robustez POSIX mejorable. Los tres
fallos urgentes que afectaban la lógica o sincronización de la rúbrica fueron
corregidos y la regresión funcional no detectó pérdidas de comportamiento.

## 13. Validación reforzada del renderer P3

Se alineó SDL con el protocolo bloqueante de la rama `bonus-renderer-sdl`:
cada `sem_wait(sem_render_turn)` consume exactamente una solicitud, se copia el
estado bajo `mutex_shared`, se dibuja fuera del lock y se confirma mediante
`sem_render_done`.

Resultados medidos:

| Variante | Ticks de P0 | Frames confirmados por P3 | Exit |
|---|---:|---:|---:|
| ncurses | 10 | 10 | 0 |
| SDL2 con driver dummy | 10 | 10 | 0 |

P0 ahora usa una espera temporizada de cinco segundos. Si P3 termina o deja de
confirmar frames, P0 lo recoge o finaliza, deshabilita el renderer y continúa la
simulación. En la prueba de fallo se mató P3 con `SIGKILL`; P0 imprimió
“Renderer deshabilitado”, continuó hasta vidas agotadas y terminó con exit 0.

El Makefile también fuerza la compilación correcta en `make run-render` y
`make run-sdl`, evitando ejecutar por accidente un binario construido con el
backend anterior. El target TSan usa `-fno-pie -no-pie`, combinación que evitó
el fallo `unexpected memory mapping` observado en este entorno.
