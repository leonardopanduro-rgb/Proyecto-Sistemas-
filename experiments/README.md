# Experimentos de concurrencia

Esta carpeta mide la línea base rota de `checkpoint-13-race-conditions`. No corrige sincronización. El único cambio funcional permite configurar `max_ticks` mediante `--max-ticks N` o `MAX_TICKS=N`.

## Ejecución

```bash
make experiments
# o
./experiments/run_experiments.sh
```

La configuración predeterminada ejecuta 1 000 corridas en los tests repetidos y 20 con TSan. Para validar rápidamente la infraestructura: `REPEAT_RUNS=10 TSAN_RUNS=2 ./experiments/run_experiments.sh`.

Cada test genera `.time`, `.out`, `.err` y `.status` en `experiments/results/`; T03/T04 también guardan `.tsan.log`. Se requieren GCC/TSan, GNU `time`, `timeout`, Python 3 y herramientas POSIX.

T05 genera 100 000 movimientos P1 y usa 200 000 ticks para ofrecerle 100 000 turnos. T06 genera 100 000 movimientos por fantasma y también usa 200 000 ticks. T10 ejecuta 100 000 ticks reales. La cantidad de procesos e hilos permanece fija.

TSan detecta carreras entre hilos, pero no demuestra seguridad completa en memoria compartida entre procesos creados con `fork()`. Por eso `game_over` sigue siendo riesgo parcial aunque no aparezca un aviso. Los tiempos TSan no se comparan con rendimiento normal debido a su sobrecarga.
