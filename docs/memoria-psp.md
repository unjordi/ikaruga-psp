# Memoria real del EBOOT

Medido en PPSSPP (PSP con `MEMSIZE=1`), build `mix` (runtime `-O2`, generado `-Os`, `float`), con
la clave `memoria=1` de `ikaruga.cfg`. Unidades en MiB (1 MiB = 1 048 576 bytes, como el
presupuesto del plan). PPSSPP no es la consola (ver «Qué no dice esta medición»).

## Cómo se mide

```
PROBAR_DISCO=<DISCO-real.ISO> scripts/medir-memoria.sh build/psp/ikaruga/EBOOT.PBP <dir-juego-extraido> \
    [--salida DIR] [--texturas-kb N] [--solo titulo|2350|2600]
```

Corre tres puntos sin limitador de PPSSPP: título (`capturar_en=1243`, `start@700`) y capítulo 1
(`2350` y `2600`, las pulsaciones de `medir-velocidad.sh`). Con `memoria=1` el EBOOT escribe en
`ikaruga.log` líneas `mem:` (arranque, tras crear el `System`, tras cargar el programa, tras armar
los dispositivos, cada `memoria_cada` cuadros —60 por omisión— y al capturar) con
`sceKernelTotalFreeMemSize`, `sceKernelMaxFreeMemSize`, `mallinfo` (`arena` = lo que newlib ha pedido
al sistema con `sbrk`, `in_use` = lo que el programa tiene dentro) y los máximos vistos; el pico se
muestrea en cada cuadro y al terminar cada render. Sin `memoria=1` no hay ninguna de esas lecturas.
La línea `heap-at-start` mide el heap entero antes de que el guest pida nada (sonda de `malloc`);
la línea `memory:` de la captura es la sonda de bloque mayor. El tamaño de la imagen sale de
`psp-size` en el build.

## Qué hay en la memoria

Heap total (`heap-at-start`: arena + bloque mayor) = 37,11 MiB. Fuera del heap: imagen, pila del
hilo principal (`PSP_MAIN_THREAD_STACK_SIZE_KB(4096)`) y 0,75 MiB que el kernel deja libres
(`sceKernelTotalFreeMemSize` = 764 KB en todas las líneas; `sceKernelMaxFreeMemSize` = 512 KB).

| pieza | presupuesto | título 1243 | cap. 1 2350 | cap. 1 2600 |
|---|---|---|---|---|
| Guest: RAM 16 + VRAM 8 + ARAM 2 + flash 0,125 + OCRAM 0,008 | 28,1 (con Boot ROM 2) | 26,13 | 26,13 | 26,13 |
| Copia de `1ST_READ.BIN` que `main` conserva todo el run | — | 0,98 | 0,98 | 0,98 |
| Host y dispositivos (heap al arrancar 0,20 + PVR/AICA/Maple/VMU 0,14) | parte de «1–2» | 0,34 | 0,34 | 0,34 |
| Caché de texturas (`texturas_kb`, `bytes` del log) | 2,5 | 0,83 | 2,49 | 2,50 |
| Resto del heap que crece al dibujar (decodificación temporal, `Frame`, vectores) | hasta 2 | 0,13 | 2,91 | 3,48 |
| **Heap en uso (`in_use`, pico)** | — | **28,41** | **32,86** | **33,43** |
| Libre dentro del arena (fragmentación) | — | 2,10 | 1,62 | 1,92 |
| **Arena (pico de `sbrk`)** | — | **30,51** | **34,47** | **35,35** |
| Heap libre sin pedir (37,11 − arena) | — | 6,60 | 2,64 | 1,76 |
| Bloque libre mayor (sonda de `malloc`) | — | 6,59 | 2,67 | 1,80 |
| Código (`.text` 12,56 incl. `.eh_frame`) | 13,8 | 12,56 | 12,56 | 12,56 |
| Data + bss (3,5 KB + 1,57; en el bss: lista del GE 1,0, tabla del SH-4 0,5) | 0,6 | 1,57 | 1,57 | 1,57 |
| Pila del hilo principal | parte de «1–2» | 4,00 | 4,00 | 4,00 |
| **Pedido al sistema (imagen 14,13 + pila 4,00 + arena)** | **≈49** (suma del presupuesto: 28,1 + 13,8 + 0,6 + 2,5 + 2 + 2) | **48,64** | **52,60** | **53,48** |

El `in_use` de cada punto es el máximo del run hasta ahí; en el capítulo 1 el máximo (35 053 896 B) se
alcanza antes del cuadro 2520 y no sube hasta el 2600. El «Resto del heap» es la diferencia
(`in_use` − `in_use` tras armar el guest − `bytes` de la caché); no está desglosado más.

## Lo que no cuadra con el presupuesto

- **Pedido al sistema en el capítulo 1: 53,48 MiB contra ≈52 útiles del plan (+1,48 MiB), 4,5 MiB
  sobre la suma del presupuesto (≈49).** PPSSPP no lo nota porque da más: imagen 14,13 + pila 4,00 +
  heap 37,11 + 1,0 de margen del heap (`PSP_HEAP_SIZE_KB(-1024)`) = 56,24 MiB (≈ los 56 MiB de
  0x08800000–0x0C000000, inferido). Si la consola da 52, el capítulo 1 no cabe como está.
- **Pila:** 4,00 MiB reservados contra «pilas, listas GE y newlib 1–2». No se midió cuánta usa.
- **Data + bss:** 1,57 contra 0,6; casi todo es la lista del GE (1,0) y la tabla de despacho (0,5).
- **Resto del heap en el capítulo 1: 3,48 contra «hasta 2» de decodificación.** Constante con
  `texturas_kb` 1024/2560/4096 (3,48/3,48/3,49), o sea que no depende de la caché.
- **Boot ROM:** no se reserva (patch 0008); por ahí no queda nada que recortar.
- **Copia de `1ST_READ.BIN`:** 0,98 MiB que el presupuesto no tiene (`main` guarda el vector después
  de copiarlo a la RAM del guest).
- **Fragmentación:** 1,6–2,1 MiB libres dentro del arena en trozos que no sirven para un bloque grande.
- A favor: guest 26,13 contra 28,1 (sin Boot ROM) y código 12,56 contra 13,8.

## Riesgo: margen libre menor a 2 MiB

En el pico del capítulo 1 queda 1,76 MiB de heap sin pedir y el bloque libre mayor es 1,80 MiB
(`< 2`); sumando los 0,75 MiB que el kernel deja fuera del heap son 2,5 MiB en PPSSPP, y
con un techo de 52 MiB es −1,48. Con `texturas_kb=4096` el margen baja a 0,89 MiB. No se ha recortado
nada. Orden de recortes del plan: 1) Boot ROM — hecho; 2) código frío a
`-Os` o a overlay; 3) ARAM solo si el audio es HLE; RAM y VRAM del guest no se tocan. La medición
agrega candidatos que el plan no lista, sin aplicar: la copia de `1ST_READ.BIN` (0,98), la pila
(4,00 reservados) y la lista del GE (1,0).

## Con `texturas_kb` distinto (cuadro 2600)

| `texturas_kb` | `bytes` de la caché | `in_use` pico | arena | heap libre sin pedir |
|---|---|---|---|---|
| 1024 | 0,99 | 31,93 | 34,06 | 3,05 |
| 2560 (por omisión) | 2,50 | 33,43 | 35,35 | 1,76 |
| 4096 | 3,61 | 34,56 | 36,22 | 0,89 |

El `in_use` sube lo mismo que los `bytes` de la caché (1,50 y 1,13 MiB contra 1,51 y 1,11).

## Qué no dice esta medición

- Es PPSSPP: la memoria útil de la consola real (52 o la que sea) no se midió; el 56,24 sale de sumar
  los trozos y el 52 es el número del plan.
- Solo el título (1243) y el capítulo 1 hasta el 2600; capítulos 2 a 5 y jefes pueden pedir más.
- Pila usada y desglose del «resto del heap»: sin medir.
- `usmblks` de newlib vale 0 en la PSP; el pico de `sbrk` es el máximo muestreado de `arena` (columna
  `peak arena`), que puede bajar un poco entre cuadros (newlib devuelve la cola) y se muestrea por
  cuadro y por render, no en cada `malloc`: un pico más corto que eso no se ve.
