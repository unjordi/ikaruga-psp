# Bitácora de la noche 2026-11-05 → 06

Objetivo: título de Ikaruga texturizado en PPSSPP. No se alcanzó. El trabajo se detuvo hacia la 01:00: la cuenta de Claude de la máquina de los agentes respondió HTTP 429 (cuota) a los dos agentes activos y la sesión coordinadora agotó su cuota semanal.

## Por fila

| Fila | Estado | Cómo se midió | PR |
|---|---|---|---|
| 0 framework | ✅ dream-recomp | reconstruido desde cero: capítulo 1 con texturas en PC, 60 s de juego en 11,7 s, 0 llamadas sin traducir | #2 |
| 1 docs | ✅ | citas comprobadas; C++ generado determinista (sha256 en `docs/reproducir-pc.md`) | #4 |
| 2 pc | ✅ | misma medición que la fila 0 | — |
| 3 toolchain | ✅ | `scripts/test-psp-hola.sh` desde cero: pasa; EBOOT idéntico byte a byte; EBOOT corrupto rechazado | #3 |
| 4 psp-compila | ✅ | build desde cero repetido: `-Os` 13,65 MB + 28,1 MB de guest ≈ 44–46 MB de 52 (cabe); `-O2` no cabe | #6 |
| 4b medir-pc | ✅ | gameplay a 4,6x tiempo real (repetido ±2 %); ≈58 % del host en la espera `kmWaitVBlank` | #5 |
| 5a arranque en PPSSPP | ⏸ cortada por la cuota | sin medir | #7 (borrador, sin revisar) |
| 5b idle-skip | ⏸ cortada por la cuota | sin medir | rama local `axon/fila-5b-idle-skip-de-kmwaitvblan-ty9ipq` (commit 4f7a2b9, sin revisar) |
| 5c, 6, 7, R, T | sin empezar | — | — |

Revisión semántica: cada fila cerrada repitió su evidencia y marcó sus afirmaciones como CONFIRMADO, CORREGIDO o SEGÚN en su documento.

## Siguiente paso
1. Revisar el borrador #7 (5a) y la rama de 5b; terminar las dos.
2. Con 5a: primera velocidad del juego en PPSSPP. Con 5b: idle-skip medido en PC (mismos cuadros, menos tiempo de host).
3. Fila 5c: geometría en pantalla.

## Lo que espera a Jordi
Nada bloqueante.
