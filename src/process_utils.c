#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <semaphore.h>

#include "game.h"

/*
 * Utilidades del ciclo de vida de procesos.
 *
 * Estas funciones cierran correctamente P1, P2 y P3 cuando P0 termina o
 * detecta un error: despiertan procesos bloqueados, evitan huérfanos y usan
 * waitpid() para que ningún hijo terminado permanezca como zombie.
 */

/*
 * Configura al hijo para recibir SIGTERM si desaparece su padre.
 *
 * pid_padre_esperado es el PID de P0 capturado antes de fork(). Se compara
 * con el PPID actual porque P0 podría morir entre fork() y prctl(). La función
 * no retorna valor; ante un error termina al hijo, pues sin P0 no tendría
 * scheduler y podría quedar bloqueado en un semáforo.
 */
void configurar_muerte_con_padre(pid_t pid_padre_esperado) {
    if (prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
        perror("Error al configurar PR_SET_PDEATHSIG");
        exit(1);
    }

    /*
        El padre puede morir entre fork() y prctl(). Verificar PPID despues
        cierra esa ventana y evita que el hijo continue adoptado.
    */
    if (getppid() != pid_padre_esperado) {
        exit(1);
    }
}

/*
 * Cierre de emergencia iniciado por P0.
 *
 * Publica game_over bajo mutex_shared y hace sem_post() sobre cada semáforo de
 * turno: un hijo dormido en sem_wait() no puede observar game_over hasta ser
 * despertado. Luego envía SIGTERM y recoge cada PID válido con waitpid(),
 * evitando huérfanos y zombies antes de liberar la memoria compartida.
 */
void terminar_hijos_por_error(
    SharedData *shared,
    pid_t pid_pacman,
    pid_t pid_enemy,
    pid_t pid_render
) {
    establecer_game_over(shared);

    sem_post(&shared->sem_pacman_turn);
    sem_post(&shared->sem_enemy_turn);
    sem_post(&shared->sem_render_turn);

    if (pid_pacman > 0) {
        kill(pid_pacman, SIGTERM);
        waitpid(pid_pacman, NULL, 0);
    }

    if (pid_enemy > 0) {
        kill(pid_enemy, SIGTERM);
        waitpid(pid_enemy, NULL, 0);
    }

    if (pid_render > 0) {
        kill(pid_render, SIGTERM);
        waitpid(pid_render, NULL, 0);
    }
}

/*
 * Espera un hijo y traduce su estado POSIX a un resultado booleano.
 * pid identifica al hijo y nombre es la etiqueta de diagnóstico. Retorna 1
 * solo si salió normalmente con código 0; retorna 0 ante cualquier otro caso.
 */
int esperar_hijo_correctamente(pid_t pid, const char *nombre) {
    int status;

    if (waitpid(pid, &status, 0) == -1) {
        perror("Error en waitpid");
        return 0;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        printf("[P0] %s finalizó correctamente\n", nombre);
        return 1;
    }

    if (WIFEXITED(status)) {
        printf("[P0] %s terminó con código %d\n",
               nombre,
               WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf("[P0] %s terminó por señal %d\n",
               nombre,
               WTERMSIG(status));
    }

    return 0;
}

/*
 * Barrera P0-P3 con tiempo límite.
 *
 * sem_timedwait() evita que un renderer caído congele al scheduler. EINTR
 * repite una espera interrumpida y WNOHANG permite distinguir un renderer ya
 * terminado de uno bloqueado. Retorna 1 al recibir confirmación, 0 ante
 * timeout/error y -1 si P3 ya había terminado.
 */
int esperar_renderer_done(SharedData *shared, pid_t pid_render) {
    struct timespec limite;

    if (clock_gettime(CLOCK_REALTIME, &limite) == -1) {
        perror("Error en clock_gettime");
        return 0;
    }

    limite.tv_sec += 5;

    while (sem_timedwait(&shared->sem_render_done, &limite) == -1) {
        if (errno == EINTR) {
            continue;
        }

        if (errno == ETIMEDOUT) {
            int status;
            pid_t resultado = waitpid(pid_render, &status, WNOHANG);

            if (resultado == pid_render) {
                printf("[P0] P3 terminó antes de confirmar el frame\n");
                return -1;
            } else {
                printf("[P0] Timeout esperando frame de P3\n");
            }
        } else {
            perror("Error esperando sem_render_done");
        }

        return 0;
    }

    return 1;
}
