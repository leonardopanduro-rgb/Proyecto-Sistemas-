#!/usr/bin/env python3
import csv,re
from pathlib import Path
R=Path(__file__).resolve().parent/'results'
TESTS=[('T01_baseline_normal','normal','10 ticks','caso normal'),('T02_shutdown_stress_normal','normal','1000 corridas','inicio y cierre'),('T03_p1_terminar_tsan','tsan','20 corridas','bandera terminar de P1'),('T04_p2_terminar_tsan','tsan','20 corridas','bandera terminar de P2'),('T05_queue_p1_100k','normal','100000 movimientos P1','cola productor-consumidor'),('T06_ghosts_100k','normal','100000 movimientos por fantasma','posiciones y mutex de fantasmas'),('T07_collision_stress','normal','1000 corridas','colisiones repetidas'),('T08_game_over_shutdown_stress','normal','1000 corridas','game_over y cierre'),('T09_priority_mailbox_stress','normal','10000 ticks','buzón SET_PRIORITY'),('T10_100k_full_run','normal','100000 ticks','carga completa')]
labels={'elapsed_wall_clock_time':'Elapsed (wall clock) time','user_time_seconds':'User time (seconds)','system_time_seconds':'System time (seconds)','cpu_percent':'Percent of CPU this job got','voluntary_context_switches':'Voluntary context switches','involuntary_context_switches':'Involuntary context switches'}
def txt(p): return p.read_text(errors='replace') if p.exists() else ''
def tm(p):
 d={k:'' for k in labels}
 for line in txt(p).splitlines():
  for k,v in labels.items():
   if v in line:d[k]=re.search(r': ([^: ]+(?::[^: ]+)*)$',line).group(1).rstrip('%')
 return d
rows=[]
for n,b,l,note in TESTS:
 log=txt(R/f'{n}.tsan.log'); out=txt(R/f'{n}.out')+txt(R/f'{n}.err'); st=txt(R/f'{n}.status').strip() or 'no ejecutado'; m=re.search(r'TOTAL_RUNS=(\d+)',out); l=(m.group(1)+' corridas') if m else l; race=bool(re.search(r'WARNING: ThreadSanitizer: data race',log)); timeout=bool(re.search(r'TIMEOUTS=[1-9]|exit=124',out)) or st=='124'; notes=[note]
 
 if 'FATAL: ThreadSanitizer' in log:notes.append('algunas corridas TSan fallaron por unexpected memory mapping')
 if not (R/f'{n}.time').exists():notes.append('sin métricas')
 rows.append(dict(test_name=n,binary_type=b,iterations_or_ticks=l,**tm(R/f'{n}.time'),race_condition_detected='sí' if race else 'no',deadlock_or_timeout_detected='sí' if timeout else 'no',exit_status=st,notes='; '.join(notes)))
cols=['test_name','binary_type','iterations_or_ticks',*labels,'race_condition_detected','deadlock_or_timeout_detected','exit_status','notes']
R.mkdir(parents=True,exist_ok=True)
with (R/'metrics.csv').open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=cols);w.writeheader();w.writerows(rows)
L=['# Resumen de experimentos','','| Test | Qué estresa | Iteraciones/ticks | Race condition | Deadlock/timeout | Elapsed | User time | System time | CPU % | Voluntary CS | Involuntary CS | OK |','|---|---|---:|---|---|---:|---:|---:|---:|---:|---:|---|']
for x in rows:
 ok=x['exit_status']=='0' and x['race_condition_detected']=='no' and x['deadlock_or_timeout_detected']=='no'; L.append(f"| {x['test_name']} | {x['notes']} | {x['iterations_or_ticks']} | {x['race_condition_detected']} | {x['deadlock_or_timeout_detected']} | {x['elapsed_wall_clock_time'] or 'N/D'} | {x['user_time_seconds'] or 'N/D'} | {x['system_time_seconds'] or 'N/D'} | {x['cpu_percent'] or 'N/D'} | {x['voluntary_context_switches'] or 'N/D'} | {x['involuntary_context_switches'] or 'N/D'} | {'Sí' if ok else 'No'} |")
L += ['','## Observaciones automáticas','','- `max_ticks` estaba fijo en 10. Se parametrizó con `--max-ticks N` o `MAX_TICKS=N`, sin cambiar la sincronización.','- T05 contiene 100 000 movimientos de Pac-Man y usa 200 000 ticks para darle 100 000 turnos por Round Robin.','- T06 contiene 100 000 movimientos por fantasma y usa 200 000 ticks para dar 100 000 turnos a P2.','- T10 ejecuta 100 000 ticks reales. No se crean hilos adicionales.','- `game_over` queda como riesgo parcial: existen accesos fuera de `mutex_shared` y TSan no cubre de forma fiable carreras entre procesos.','- `SET_PRIORITY` existe; sus campos pendientes se protegen con `mutex_shared`, por lo que T09 aplica.','','## Conclusión para el informe','','El programa fue probado con los mismos procesos e hilos definidos por la arquitectura. La carga aumentó mediante corridas, ticks y movimientos, no creando miles de hilos. Las métricas de `/usr/bin/time -v` muestran el costo real de sincronización. Los *voluntary context switches* representan bloqueos voluntarios por semáforos, mutex, esperas o coordinación entre procesos e hilos.','','ThreadSanitizer se usó para detectar carreras entre hilos. Sus tiempos no son métricas finales de rendimiento porque agrega sobrecarga. La evidencia principal esperada está en las banderas `terminar` de P1 y P2, pero solo se afirma una carrera si aparece en los logs. Los tests que no fallan también confirman que ciertos recursos ya estaban protegidos.']
(R/'summary.md').write_text('\n'.join(L)+'\n')
T=['# Resumen de ThreadSanitizer','']
for n in ('T03_p1_terminar_tsan','T04_p2_terminar_tsan'):
 log=txt(R/f'{n}.tsan.log'); T.append(f"- **{n}:** {len(re.findall('WARNING: ThreadSanitizer: data race',log))} reportes de carrera; {len(re.findall('FATAL: ThreadSanitizer',log))} errores fatales de TSan.")
T += ['','Los logs `*.tsan.log` conservan la salida completa.']; (R/'tsan_summary.md').write_text('\n'.join(T)+'\n')
