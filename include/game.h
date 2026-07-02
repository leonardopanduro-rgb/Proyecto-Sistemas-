#ifndef GAME_H
#define GAME_H

#include <stdio.h>
#include <sys/types.h>

#include "shared.h"

#define PRIORIDAD_MIN 1
#define PRIORIDAD_MAX 100

#define LECTURA_FIN 0
#define LECTURA_OK 1
#define LECTURA_INVALIDA -1
#define LECTURA_ERROR -2

SharedData *crear_memoria_compartida(void);
void inicializar_shared(SharedData *shared);
void liberar_memoria_compartida(SharedData *shared);

void construir_ruta(char destino[], int tam,
                    const char *carpeta_caso, const char *archivo);
int obtener_game_over(SharedData *shared);
void establecer_game_over(SharedData *shared);
int obtener_global_tick(SharedData *shared);
void obtener_control_juego(SharedData *shared, int *game_over,
                           int *global_tick, int *max_ticks,
                           int *pacman_lives);

void configurar_muerte_con_padre(pid_t pid_padre_esperado);
void terminar_hijos_por_error(SharedData *shared, pid_t pid_pacman,
                              pid_t pid_enemy, pid_t pid_render);
int esperar_hijo_correctamente(pid_t pid, const char *nombre);
int esperar_renderer_done(SharedData *shared, pid_t pid_render);

void limpiar_espacios_movimiento(char movimiento[]);
int instruccion_movimiento_valida(const char *movimiento);
int leer_movimiento(FILE *archivo, char movimiento[], int tam);
int extraer_prioridad(const char *movimiento, int *nueva_prioridad);
int prioridad_valida(int prioridad);

void publicar_error_entrada(SharedData *shared, int process_id);
int procesar_error_entrada(SharedData *shared);
void publicar_pacman_agotado(SharedData *shared);
void publicar_fantasma_agotado(SharedData *shared, int ghost_id);
int entradas_agotadas(SharedData *shared);
void solicitar_prioridad_pacman(SharedData *shared, int nueva_prioridad);
void solicitar_prioridad_enemy(SharedData *shared, int nueva_prioridad);
void procesar_solicitudes_prioridad(SharedData *shared);
int elegir_turno_por_prioridad(SharedData *shared, int *ultimo_turno);
void procesar_colision_si_existe(SharedData *shared);
void imprimir_estado_tick(SharedData *shared);

void pacman_process(SharedData *shared, const char *carpeta_caso);
void enemy_process(SharedData *shared, const char *carpeta_caso);
int scheduler_process(const char *carpeta_caso, int max_ticks_arg,
                      int render_enabled);

#endif
