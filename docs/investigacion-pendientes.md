# Investigación de los pasos abiertos del plan

Propósito: contestar con evidencia las preguntas abiertas de `docs/plan.md` para que cada paso quede
como (a) se borra, (b) trabajo concreto con archivo:función, o (c) partido en pasos más chicos. No se
implementa ningún arreglo aquí.

Base: develop `24d2d31`, submódulo `third_party/dream-recomp` `51ca276`. Sin PPSSPP (la máquina lo
estaba usando otra verificación); donde una respuesta lo exige se deja el comando.

Herramientas usadas:

- PC recompilado: `B=~/code/ajenos/dr-idle/build-rel/games/ikaruga/ikaruga_boot`,
  `T=~/code/ajenos/dr-idle/games/ikaruga/ikaruga.toml`, siempre con `--rtc-seed 1` y las pulsaciones
  estándar de las pruebas `P=start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200`.
- Evidencia: el sandbox no dejó escribir en `~/code/ajenos/ikaruga-tareas/investigacion-evidencia/`;
  quedó en `build/investigacion-evidencia/` del worktree (gitignored, el arnés la borra). Los números
  de este documento son la copia que queda.

Corrida base (cuadro 2600, capítulo 1):

```
$B --config $T --rtc-seed 1 --press $P --max-frames 2601 \
   --dump-ta-at-frame 2600 --dump-ta ta-2600.bin --report report-2600.txt
```

Reporte: `PVR: 2320 renders (2319 done), 2319 frame swaps`; el render capturado es el 2320, 47 112
palabras. Listas de ese render: opaca 142 polígonos + 7 sprites; opaca-modificador 8 volúmenes;
translúcida 183 polígonos + 8 sprites; translúcida-modificador 8 volúmenes; punch-through 1 polígono
+ 7 sprites.

---

## 37. Un render de menos (PSP 2319 vs PC 2320 al cuadro 2600)

**Pregunta.** ¿Qué hace que, en el mismo cuadro, la PSP lleve un render menos que PC?

**Lectura de código.** En los dos lanzadores el conteo sale del mismo sitio: `Core::start_render()`
(`third_party/dream-recomp/runtime/src/pvr/core.cpp:175`) suma `renders` y llama a `on_render`
cuando el guest escribe `STARTRENDER`. `psp/ikaruga/main.cpp` (lambda `pvr.on_render`) cuenta los
suyos con `++renders` en cada llamada, sin filtrar nada. `boot_main.cpp:1492` solo filtra los renders
"pequeños" al volcar, no al contar. Ningún lanzador provoca un render de más ni de menos: el conteo
es del guest. La diferencia de configuración entre los dos es que **la PSP siempre conecta una
tarjeta de memoria** (`maple.attach_expansion(0, 0, card)`, en blanco y formateada si falta
`VMU.BIN`), mientras que PC sin `--vmu` no conecta ninguna (ya anotado en
`docs/geometria-psp.md:157`).

**Medición.** El mismo PC con y sin `--vmu` (tarjeta nueva en blanco), cuadro por cuadro:

```
for f in 60 200 400 700 1000 1300 1700 2100; do
  $B --config $T --rtc-seed 1 --press $P --max-frames $f                 | grep -o '[0-9]* renders'
  rm -f v.bin; $B ... --max-frames $f --vmu v.bin                       | grep -o '[0-9]* renders'
done
$B ... --max-frames 2601 --vmu vmu-blank.bin --dump-ta-at-frame 2600 --dump-ta ta-2600-vmu.bin
```

| cuadro | renders sin tarjeta | renders con tarjeta en blanco |
|---|---|---|
| 60 | 0 | 0 |
| 200 | 1 | 1 |
| 400 | 199 | 198 |
| 700 | 499 | 498 |
| 1300 | 1096 | 1095 |
| 2100 | 1819 | 1818 |
| 2600 | 2320 | **2319** |

Con tarjeta, PC da 2319 renders al cuadro 2600, igual que la PSP, y la lista volcada es idéntica
byte a byte a la de sin tarjeta (`cmp` igual, 47 112 palabras). El render se pierde entre los
cuadros 200 y 400 (arranque: el juego revisa/lee la tarjeta, 15 lecturas y 144 escrituras de bloque
hasta el 2600) y después la diferencia se queda en 1 fija.

**Resultado.** No es un bug del port: es el juego tardando un cuadro más en el arranque cuando hay
tarjeta. La escena al cuadro 2600 es la misma.

**Propuesta para el plan.** (a) **Borrar el paso 37.** Como consecuencia, que las herramientas que
comparan contra PC (`comparar-geometria.sh`, `test-psp-igual-pc.sh`, `comparar-titulo.sh`) lancen PC
con `--vmu` sobre una tarjeta en blanco nueva, para que PC y PSP vean la misma consola. Esto entra en
el paso 85 (red de pruebas), no como paso aparte. Contra Flycast hay que recordar lo mismo: la
captura de Flycast debe tener la VMU en el mismo estado (ver paso 36).
