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

## 38. ¿Render a textura (`FB_W_SOF1` bit 24)?

**Pregunta.** ¿El juego apunta algún render a una textura en vez de a la pantalla?

**Cómo lo medí.** Sin tocar el runtime: con `gdb` sobre el lanzador de PC (no tiene símbolos de
depuración, pero sí nombres), un watchpoint de hardware sobre el registro `FB_W_SOF1` dentro de
`Core::regs_` (la base de los registros se encuentra buscando el `ID` `0x17FD11DB` desde `this` en la
primera llamada a `Core::write`; `FB_W_SOF1` está en +0x60). Cada escritura del guest se imprime.

```
# sof1b.gdb
break dream::pvr::Core::write
run
delete
find /w $rdi, +0x8000, 0x17FD11DB
set $sof1 = (unsigned int*)($_ + 0x60)
watch -l *$sof1
commands
silent
printf "SOF1 %08x\n", *$sof1
continue
end
continue

gdb -batch -x sof1b.gdb --args $B --config $T --rtc-seed 1 --press $P --max-frames N
```

**Resultado.**

| tramo | escrituras de `FB_W_SOF1` | valores | con bit 24 |
|---|---|---|---|
| cuadros 0–400 (logo, título) | 199 | `0x0026a000` ×100, `0x0066a000` ×99 | 0 |
| cuadros 0–2601 (título, menú, inicio del capítulo 1) | 2320 | `0x0026a000` ×1160, `0x0066a000` ×1160 | 0 |
| cuadros 0–9000 (capítulo 1 hasta donde llega sin más pulsaciones) | 8715 | `0x0026a000` ×4358, `0x0066a000` ×4357 | 0 |

Una escritura por render, alternando entre los dos búferes de pantalla (doble búfer). El reporte
además da `0 texture words` (nadie escribe por el camino de texturas del TA). Ningún render va a
textura en lo que alcancé. No llegué a los capítulos 2–5: con las pulsaciones guionadas el juego se
queda en el capítulo 1 (sin más entrada, la nave no sobrevive mucho), y no tengo una grabación de
entrada que llegue más lejos.

**Propuesta para el plan.** (a) **Borrar el paso 39** (dibujar a textura) y dejar el **38 cerrado
para el capítulo 1**. La comprobación de los capítulos 2–5 no se hace aparte: entra en el paso 72
(recorrer los capítulos en PPSSPP), donde la PSP ya cuenta `texture_renders` en el log
(`psp/ikaruga/main.cpp`, lambda `pvr.on_render`). Basta con que el paso 72 diga «`texture_renders`
sigue en 0; si no, abrir el paso de render a textura».

## 40–48. Sospechas de video: ¿Ikaruga las ejercita?

**Pregunta.** Para cada sospecha de `docs/plan.md` 40–48, ¿la usa el juego, en qué escena y en cuántos
polígonos?

**Cómo lo medí.** Volqué el TA del primer render en o después de 11 cuadros (título 300/600, menú
1100, inicio del capítulo 1 1500/2000, capítulo 1 2600/3500/4500/6000, 7500/9000 que dan la misma
lista) y lo decodifiqué con el mismo `dream::render::DisplayList` que usa la PSP, con un contador de
bits de ISP/TSP/TCW/PCW: `tools/analizar-ta.cpp` (herramienta de investigación, no prueba; se
compila con la línea que trae en la cabecera). Los registros del PVR salen de `--dump-vram` en 2000,
2600 y 3500.

```
for f in 300 600 1100 1500 2000 2600 3500 4500 6000 7500 9000; do
  $B --config $T --rtc-seed 1 --press $P --max-frames $((f+1)) --dump-ta-at-frame $f --dump-ta ta-$f.bin
  build/host/analizar-ta ta-$f.bin > analisis-$f.txt
done
$B --config $T --rtc-seed 1 --press $P --max-frames 2600 --dump-vram vram-2600.bin   # + .regs
```

Polígonos por render: 300: 25 · 1100: 102 · 1500: 814 · 2000: 2049 · 2600: 947 · 3500: 1771 ·
4500: 1761 · 6000: 1164 · 7500: 429. Ningún parámetro desconocido ni vértice tirado.

**Resultado.**

| paso | sospecha | ¿usada? | escena | polígonos (por render) |
|---|---|---|---|---|
| 42 | culling CW/CCW (ISP modo 2/3) | **no** | — | 0 en todas |
| 42 | culling «si es pequeño» (modo 1, `FPU_CULL_VAL` = 0,01) | sí | capítulo 1 | OP 552–1476, TR 353 (2600) |
| 43 | modifier volumes | listas sí, efecto **no** | todas | 8–36 triángulos de volumen, todos modo 0 (ninguno cierra un volumen); 0 polígonos con bit *shadow*/dos volúmenes; `FPU_SHAD_SCALE` = 0x1 |
| 44 | color offset (bit offset del PCW) | **sí** | capítulo 1 | OP 542 (2600) – 1471 (4500); TR 179–515 |
| 44 | …con offset distinto de 0 (se vería) | **sí** | desde 2000 | OP 68 (2600) – 733 (4500); TR 12–114 |
| 44 | color clamp (TSP bit 21) | **no** | — | 0 |
| 45 | niebla modo 0 (tabla) / 2 (ninguna) | sí | todas | OP 473–1412 en tabla |
| 45 | niebla modos 1 (vértice) y 3 (tabla 2) | **no** | — | 0 |
| 45 | translúcidas con niebla y origen ≠ SA/ONE | **no** | — | 0 (las TR solo usan SA/ONE y SA/ISA; las OP ONE/ZERO) |
| 46 | formatos 1555, 565, 4444 | sí | todas | 565 en todas las OP texturizadas |
| 46 | VQ, mipmaps | sí | capítulo 1 | VQ OP 339–1440; mip OP hasta 1394 |
| 46 | PAL4 / PAL8 | sí / casi no | título–capítulo 1 | PAL4 TR 1–204; **PAL8 2 polígonos** (2000–4500) |
| 46 | paleta 8888 (`PAL_RAM_CTRL` = 3) | **no** | — | `PAL_RAM_CTRL` = 2 (4444) en 2000/2600/3500 |
| 46 | YUV, bump | **no** | — | 0 |
| 46 | espejo/flip UV | **no** | — | 0 (sí clamp UV: OP 77–1471) |
| 46 | stride | **no** | — | 0; no-twiddled solo 6 polígonos 1555 del título |
| 46 | decal alpha / ignore_texture_alpha | **no** | — | 0: todo polígono texturizado usa *modulate alpha* (shading 3) |
| 48 | recorte de usuario modo 3 (fuera) | **no** | — | 0; modo 2 (dentro) en casi todo |
| 41 | perspectiva (1/w distinto dentro de una tira) | **sí** | capítulo 1 | tiras con 1/w máx/mín > 1,5: OP 124 + TR 154 (2600); > 3: OP 42 + TR 82 |
| 40 | rango de 1/w por escena | ancho | capítulo 1 | 5,4·10⁻⁵ … 1,0021 (relación hasta 18 487) |

Profundidad (paso 40), el detalle: la PSP reparte linealmente `min_z…max_z` del render en 16 bits
(`psp/stubs/video.cpp`, `video_draw_frame`, `zbias + (v.z - zmin) * zscale`), un paso de 1,53·10⁻⁵.
Pero el HUD y el fondo están en 1/w ≈ 1 y la escena en 10⁻³…10⁻¹, así que la escena cae en pocos
escalones:

| render | 1/w en [10⁻³, 10⁻²): valores distintos → escalones de 16 bits | [10⁻², 10⁻¹) |
|---|---|---|
| 2000 | 5253 → **77** | — |
| 2600 | 632 → 242 | 1782 → 1114 |
| 3500 | 2123 → 445 | 1441 → 326 |
| 4500 | 2615 → **180** | 361 → 96 |

Miles de profundidades distintas comparten escalón: con GEQUAL (el modo de casi todo, ISP 6) lo que
está detrás y se dibuja después gana. Es z-fighting probable, no visto (no corrí PPSSPP).

**Propuestas para el plan.**

- **42 → borrar.** El juego nunca pide culling por orientación; el «si es pequeño» solo quita
  polígonos de menos de 0,01 px², invisibles.
- **43 → borrar.** Hay triángulos de volumen, pero ningún polígono los consulta (sin bit shadow ni
  dos volúmenes) y ningún volumen se cierra. No hay sombra que dibujar.
- **44 → partir:** (44a) **color offset**, trabajo concreto en `psp/stubs/video.cpp`,
  `video_draw_frame`: una pasada aditiva sin textura con el color offset (`GU_ADD`, `GU_FIX` 1/1,
  profundidad EQUAL como la pasada de niebla) solo para las tiras con offset ≠ 0; cierra con la
  escena 4500 (733 polígonos) contra Flycast. Color clamp: **borrar**.
- **45 → reducir** a «niebla de tabla correcta» (ya hecha en el paso 14). Modos 1/3 y translúcidas
  con otro factor: **borrar**. El riesgo de la pasada EQUAL en hardware se mueve al **paso 82**.
- **46 → reducir** a: (i) PAL8, 2 polígonos: comprobar que se dibujan (una mirada al log
  `trazar=1` en 2600); (ii) contar texturas que no caben: ya está — el log de la PSP imprime
  `failed`/`over_budget` cada `kLogEvery` (`psp/ikaruga/main.cpp`, `on_vblank_out`); basta con que
  la prueba de imagen lo lea. Espejo, YUV, bump, paleta 8888, stride, decal, ignore_texture_alpha y
  la llave con `PAL_RAM_CTRL`: **borrar** (constante en 2).
- **48 → borrar el modo 3**; el parpadeo de PRESS START es cobertura de escenas y va al paso 85.
- **41 → trabajo concreto, con medida previa:** hay tiras con 1/w ×3 de un extremo a otro, así que
  la deformación afín del modo through existirá. Primero una captura de PSP del cuadro 2600 contra
  Flycast mirando el suelo/estructuras inclinadas (la sacará el paso 35); si se ve, subdividir en
  `video_draw_frame` las tiras con relación > 1,5 (≈280 por render) en vez de pasar a modo 3D.
- **40 → trabajo concreto:** en `video_draw_frame`, repartir la profundidad en log2(1/w) (como PC)
  o, más barato, calcular `zmin/zmax` sin los planos de HUD/fondo (1/w ≥ 1) y darles un escalón
  aparte. Se cierra con el render 2000 sin escalones compartidos entre polígonos que se tapan
  (contarlo con `tools/analizar-ta.cpp`) y la captura contra Flycast.

## 36. El centro del capítulo 1 a ≈16 dB

**Pregunta.** ¿Qué tiene el centro del cuadro 2600 que la PSP no dibuja igual?

**Cómo lo medí.**

1. La captura de PSP más reciente del cuadro 2600 (`psp-2600.png` de la verificación de video
   de develop, la misma de los 15,7 dB) contra capturas de PC de **varios cuadros presentados
   alrededor** (`--window --unthrottled --present-mode immediate --screenshot-at S`,
   S = 2596…2602, escaladas a 480×272 con `BOX` como `comparar-titulo.sh`), centro = x 105–375.
2. Descomposición del error: PSNR tras desenfoque gaussiano (lo que queda es baja frecuencia:
   color/brillo de zonas) y por regiones.
3. Flycast: `scripts/capturar-flycast.sh copia.chd --segundos 55 --fps 10` (sobre una copia del CHD) y
   PSNR del PC 2600 contra los cuadros 428–441 de Flycast.
4. Del TA del render capturado (`tools/analizar-ta.cpp` con `ANALIZAR_TA_POLIGONOS=1`): qué
   polígonos tienen color offset ≠ 0 y cuáles tienen coordenadas fuera del rango que el GE acepta en
   modo through (±2048 desde el centro, tras escalar a 480×272).

**Resultado.**

| PC, cuadro presentado (cuadro del guest) | centro PC vs PSP |
|---|---|
| 2596 (2598) | 13,77 dB |
| 2598 (2600), el que usa la prueba | 15,15–15,74 dB |
| **2599 (2601)** | **19,04 dB** |
| 2600 (2602) | 15,74 dB |
| 2601 (2603) | 14,28 dB |

- **≈3,3 dB del «16 dB» son desfase de un cuadro**: la prueba compara la PSP con un cuadro de PC que
  no es el mismo instante (en este tramo el escenario se mueve rápido y 1 cuadro de diferencia ya
  baja de 19 a 15,7 dB). El mismo PC con tarjeta (`--vmu`, como la PSP) da los mismos números: el
  desfase no es el render de menos del paso 37, es cómo `--screenshot-at` cuenta cuadros
  *presentados* (2 menos que los del guest) frente a `capturar_en`, que cuenta cuadros del guest.
- Alineado (19,04 dB), el error que queda es **mayormente de alta frecuencia**: tras desenfoque de
  radio 2 el centro sube a 23,8 dB y la baja frecuencia es solo el 33 % del error. Por regiones:
  estructura de la derecha 52 % del error (19,2 dB), cielo con nubes 35 % (17,0 dB), nave 4 %. El
  modelo de visión marca además un reflejo claro en la parte alta de la estructura que en la PSP
  sale mate.
- **Color offset**: en este render solo 68 polígonos opacos tienen offset ≠ 0, de ≤ 85 px² cada
  uno (detalles de la nave, offset `ff000000`, es decir solo el alfa de niebla de vértice, RGB 0):
  **no explica el centro**. En renders posteriores sí pesa (733 polígonos en 4500, paso 44).
- **Modifier volumes**: sin efecto (paso 43). **Filtrado**: la PSP usa `GU_LINEAR` donde el PVR pide
  bilineal (filtro 1 en todos los polígonos texturizados de escena), pero sin mipmaps en el GE
  (473 polígonos opacos con mip en 2600): una textura minificada sin mip sale con más aliasing →
  alta frecuencia, encaja con lo medido.
- **Polígonos enormes**: 172 tiras del render (73 OP 565, 73 TR PAL4 de las nubes, 26 TR más) tienen
  vértices fuera de ±2048 px una vez escalados. El GE en modo through no recorta en guarda; si las
  descarta o las deforma, faltaría parte del cielo/nubes (35 % del error). Hipótesis no comprobada.
- **Flycast** no sirve hoy para aislarlo en este tramo: el mejor cuadro de Flycast queda a 17,2 dB
  (centro 14,5) del PC, por el desfase de pulsaciones ya documentado en `docs/referencia-flycast.md`
  (diferencia 5); no hay el mismo instante en las dos series.

**Experimento listo (exige PPSSPP; no lo corrí).** Un render con traza y su lista:

```
# ikaruga.cfg: capturar_en=2600, pulsar=$P, rtc_seed=1, trazar=1, volcar_ta=1
PROBAR_CFG=ikaruga.cfg scripts/probar-ikaruga-ppsspp.sh build/psp/EBOOT.PBP <juego> 6000 psp.log
grep '^strip' psp.log > tiras-psp.txt
ANALIZAR_TA_POLIGONOS=1 build/host/analizar-ta ta-2600.bin | grep '^P' > tiras-pc.txt
```

y comparar contra el PC del cuadro *presentado 2599*. Contraprueba de las dos hipótesis, cada una por
separado en `psp/stubs/video.cpp` (`video_draw_frame`): (i) no dibujar las tiras con vértices fuera
de ±2048 — si el centro no cambia, no son ellas; (ii) forzar `GU_NEAREST` — si el centro baja en la
estructura, el filtrado/mip es la causa.

**Propuesta para el plan.** (c) **Partir el 36:**

- 36a. Las comparaciones de imagen contra PC alinean el instante: `comparar-titulo.sh` pide a PC el
  cuadro presentado que corresponde al cuadro del guest de la PSP (hoy `--screenshot-at N`, debería
  ser el que reporta `guest frame N+1`, o buscar el mejor de N−2…N+2 y reportarlo). Sube el centro de
  15,7 a 19,0 dB sin tocar el port.
- 36b. Polígonos fuera del rango del GE: recortarlos en `video_draw_frame` (recorte contra la banda
  de guarda, o pasar esas tiras a modo 3D con el clipper del GE). Cierra con el experimento de arriba.
- 36c. Mipmaps en el GE para las texturas con mip (`texture_psp.cpp` sube los niveles,
  `sceGuTexLevelMode`): solo si (ii) muestra que el filtrado es la causa.
- 36d (reflejo de la estructura): queda pendiente de 36b/36c; si sobrevive, es el color offset del
  paso 44a.

## 47. Denormales en el build `float`: ¿cambian algo visible?

**Pregunta.** ¿El vaciado de denormales (el SH-4 los aplana con FPSCR.DN; la FPU de la PSP no) produce
algo que se vea?

**Lo que hay.** El runtime de PC ya pone FTZ+DAZ en MXCSR cada vez que el guest escribe FPSCR con DN=1
(`third_party/dream-recomp/runtime/src/fenv.cpp`, `set_flush`; el FPSCR por defecto de Katana es
`0x00040001`, DN=1). El único lanzador de PC disponible (`dr-idle/build-rel`) es el build `float` y
no hay un build `double` ni uno sin FTZ en la máquina; el runtime fija MXCSR con la instrucción
`ldmxcsr` en línea, así que no se puede anular desde fuera con `LD_PRELOAD`.

**Lo que medí (control, no responde la pregunta).** Forzar FTZ/DAZ desde el arranque con un
`LD_PRELOAD` que hace `_mm_setcsr(... | 0x8040)` antes de `main`:

```
$B ... --max-frames 9000 --write-hash hash-normal.txt
LD_PRELOAD=ftz.so $B ... --max-frames 9000 --write-hash hash-ftz.txt
cmp hash-normal.txt hash-ftz.txt   # IGUALES, 9003 líneas
```

Idéntico hasta el cuadro 9000: el código del runtime anterior al primer `LDS FPSCR` del guest no
toca denormales. No dice nada de la PSP.

**Lo que ya se sabía** (`docs/velocidad-psp.md:215-220`): la primera divergencia PC↔PSP por
denormales es una escritura del cuadro 1983 (`njGetTranslation`, fr1 = 0 en PC y `0x057DE800` en la
PSP), es decir un valor de ≈10⁻³⁸ que en PC se aplana a 0. Una traslación de 10⁻³⁸ no mueve ningún
vértice un píxel; que se vea solo puede venir de que un camino del juego compare contra 0 exacto.

**Resultado.** No se puede medir en PC sin otro build; por la regla del proyecto (divergencias de
FPU en `float` no son bug) y porque el único caso conocido es del orden de 10⁻³⁸, no hay evidencia de
efecto visible.

**Propuesta para el plan.** (a) **Borrar el paso 47** como paso propio. Si la comparación escena por
escena contra Flycast (paso 35) o el recorrido de capítulos (72) muestra una diferencia de
comportamiento (no de píxeles) sin otra causa, entonces se abre un paso con el experimento:
build PC sin FTZ (`set_flush` vacío) contra el normal, `--write-hash` y captura en el punto de la
diferencia.

## 54. Costo del AICA

**Pregunta.** ¿Cuánto cuesta el AICA y cuánto ARM ejecuta por segundo de juego?

**Cómo lo medí.** El lanzador de PC no tiene interruptor para el mezclador ni para el DSP:
`--no-audio`/`--audio` solo deciden si la salida va al dispositivo
(`third_party/dream-recomp/runtime/src/aica/aica.cpp`, `Aica::sample_tick`: `arm.run`,
`step_timers`, `mixer.sample` se llaman siempre). No hay `perf` en la máquina (ya anotado en
`docs/medicion-pc.md:90`). Medí lo que sí se puede: tres corridas de 6000 cuadros con cada opción y el
reporte del AICA.

```
for m in --no-audio --audio; do for i in 1 2 3; do
  $B --config $T --rtc-seed 1 --press $P --max-frames 6000 --unthrottled $m | grep -E '^host|AICA'
done; done
```

**Resultado.**

- Tiempo de host: 10,22–10,46 s en las seis corridas, sin diferencia entre `--no-audio` y `--audio`
  (la salida no cuesta nada medible; lo caro se emula siempre).
- 100,1 s de guest (6000 cuadros): **291 012 163 instrucciones ARM = 2,9 M instr/s de juego**,
  4 414 537 muestras (44,1 kHz), 100 136 FIQ, 134 656 interrupciones de timer, 944 key-on, DSP en
  marcha, 4 260 887 de las 4 414 537 muestras distintas de 0 (el juego sí suena).
- Histograma de PC del ARM: las 11 direcciones más frecuentes son `0x1d10–0x1d48` y `0xd60/0xd64`;
  juntas son la gran mayoría de las muestras. Es un **bucle de espera del driver de sonido** (gira
  esperando el FIQ del timer), no trabajo.
- La parte del AICA en el tiempo de host sigue siendo la de `docs/medicion-pc.md:117-129`: ≈17 %
  (mezclador 10,8 %, de ello DSP 6,8 %; ARM7 5,1 %).

**Propuesta para el plan.** (b) **Fundir 54 en 55** y hacerlo concreto:

- 55a. Interruptor de compilación en `Aica::sample_tick` (`aica.cpp`): sin mezclador ni DSP mientras
  no haya salida de audio en la PSP (paso 71). Prueba: `velocidad` en PPSSPP antes/después;
  contraprueba: `--write-hash` del build `double` igual (el guest no lee la salida del mezclador; si
  el hash cambia, el juego lee algo del mezclador y el interruptor no sirve).
- 55b. Idle-skip del ARM: cuando el PC del ARM está en `0x1d10–0x1d48` sin FIQ pendiente, saltar al
  siguiente evento del timer (`Arm7::run`, `runtime/src/aica/arm7.cpp`). Medir antes cuántas de las
  2,9 M instr/s caen en ese bucle (el histograma del reporte ya lo da por muestra).

## 56. Otros bucles de espera

**Pregunta.** ¿Dónde gira el guest además de `kmWaitVBlank`?

**Cómo lo medí.** Muestreo de PC del SH-4 cada 20 000 ciclos en 6000 cuadros (1 000 996 muestras;
cada línea trae pc y pr), simbolizado con `games/ikaruga/symbols.tsv`, por ventanas de 250 cuadros:

```
$B --config $T --rtc-seed 1 --press $P --max-frames 6000 --sample 20000 --sample-file sample.txt
```

**Resultado.**

| bucle | dirección | cuándo | parte de las muestras |
|---|---|---|---|
| `kmWaitVBlank` → `kmyRequestCallback` → `kmyiDefaultCallback` (ya en `[idle]`) | `0x0C0370BA` / `0x0C03CD60` / `0x0C03B320` | siempre | 60–97 % según la ventana |
| espera del GD-ROM: `gdFsWaitCompleteReq` (`0x0C040DEC`) → `gdFsGetStat` (`0x0C0414A0`) → syscalls `gdGdcGetCmdStat` (`0x0C04B614`, desde `0x0C03F294`) y `gdGdcExecServer` (`0x0C04B628`, desde `0x0C03F482`) | bucle en `0x0C040E14–0x0C040E18` | arranque (0–250, 82 % de la ventana), menú→capítulo (1250–1300, 1400–1500, 1850–1950) | **3,7 %** del total (37 116 muestras) |
| `sdBankDownloadCalculateCheckSum` (`0x0C051028`) | — | arranque | 0,2 %; es trabajo, no espera |

No hubo pausa ni cambio de capítulo en la corrida (las pulsaciones no llegan), así que esos dos no
están medidos.

La espera del GD-ROM ya es barata en el runtime: el HLE adelanta el scheduler hasta el reloj del
guest en cada `GetCmdStat`/`ExecServer` (`runtime/src/hle/bios.cpp:488-512`), de modo que el bucle
da pocas vueltas por lectura; lo que cuesta es que cada vuelta es una llamada de syscall.

**Propuesta para el plan.** (c) **Reducir el 56:** solo cargas de ≈1–4 s de guest al arrancar y
al entrar al capítulo, ≈3,7 % del guest y fuera del juego. Un tipo nuevo de `[idle]`
(`gdfs_wait_complete`, cabeza en `0x0C040E14`: saltar hasta el siguiente evento `gdrom-hle` del
scheduler) es posible pero **va al final de la sección de velocidad**, porque no toca el capítulo 1.
Pausa y cambio de capítulo: medirlos con el mismo `--sample` cuando el paso 72 llegue a ellos.

## 71 (condición). ¿El driver de sonido del ARM se modifica en ARAM durante el juego?

**Pregunta.** Para traducir estáticamente el driver del ARM hace falta que su código no cambie.

**Cómo lo medí.** Volqué los 2 MB de ARAM en cuatro momentos (título 300, menú 1300, capítulo 1
2600 y 6000) y comparé palabra a palabra la zona de código: desde los vectores (`ea00003e…`, saltos
ARM en 0x0) hasta la última palabra con forma de instrucción incondicional (`cond = 0xE`) de los
primeros 64 KB, `0x9010`. Ahí están todos los PC que da el histograma del ARM (`0x1c4`, `0xac0`,
`0xd60`, `0x1d10–0x1d48`).

```
for f in 300 1300 2600 6000; do $B --config $T --rtc-seed 1 --press $P --max-frames $f --dump-aram aram-$f.bin; done
# python: palabras distintas en [0, 0x9020) entre 300→1300→2600→6000, y cuáles tienen forma de instrucción
```

**Resultado.** En `[0, 0x9020)` cambian **22 palabras** entre los cuatro volcados (`0x228, 0x30c,
0x3a8, 0xadc, 0xb5c–0xb60, 0x16a4–0x16e4, 0x1808–0x1814, 0x197c, 0x1a68, 0x1c0c, 0x1c54, 0x7370`), y
**ninguna tiene forma de instrucción en ningún volcado**: son contadores, punteros y estado
(p. ej. `0x3a8` vale `0x998/0x1d8/0x99c/0xd64`, un PC guardado; `0x30c` crece 0x15→0x19c). Las
instrucciones del driver son las mismas del título al cuadro 6000. Lo que cambia por encima de
`0xafe0` son búferes y muestras (bancos de sonido que el SH-4 descarga, `sdBankDownload…`).

**Propuesta para el plan.** (b) El paso 71 puede tachar «requiere comprobar que no se modifica en
ARAM» como **comprobado para título y capítulo 1** (variables de datos dentro de la imagen sí:
la traducción debe tratar `0x228…0x7370` como datos). Queda como condición para los capítulos 2–5
del paso 72: repetir los cuatro `--dump-aram` y la comparación al llegar ahí. El código del driver
lo carga el SH-4 una vez antes del cuadro 300; el traductor estático tendría que leerlo de ARAM tras
esa carga (o del archivo del disco del que sale), no del `1ST_READ.BIN`.

## 23. ¿Hay un punto seguro para volcar y reanudar el estado?

**Pregunta.** `docs/pruebas-rapidas.md` §3 (rama `feat/pruebas-rapidas`) propone volcar en
`on_vblank_out` cuando el guest está en su bucle de espera y reanudar con `resume_at`, y pide comprobar
que la dirección (`0x0c03e01c` en los logs) tiene entrada de reanudación.

**Cómo lo medí.** Leyendo el C++ generado del PC (`dr-idle/build-rel/games/ikaruga/gen/ikaruga.cpp`)
y `run_guest_impl`/`resume_at` (`third_party/dream-recomp/runtime/src/sh4/abi_bare.cpp:238-300`):

```
grep -n "case 0x0c0370bau\|idle_poll(c, m" gen/ikaruga.cpp
grep -n "0x0c03e01c" gen/ikaruga.cpp
python3: direcciones de retorno (c.pr = 0x…u) frente a etiquetas (case 0x…u: goto) del generado
```

**Resultado.**

- `0x0c03e01c` **no es el bucle de espera**: es un `jsr @r14` dentro de `kmyStartRender` (el PC que
  el log ve al llamar al callback de render). Su dirección de retorno `0x0c03e020` sí tiene etiqueta
  (`kmyStartRender__resume`, `case 0x0c03e020u`), pero no es un buen punto: está a mitad del envío al
  PVR.
- **El punto seguro es `0x0C0370BA`**, cabeza del bucle de `kmWaitVBlank` (la misma del `[idle]`):
  el generado tiene `case 0x0c0370bau: goto L_0c0370ba;` en `kmWaitVBlank__resume` y emite
  `idle_poll(c, m, 0x0c0370bau)` justo ahí, que el runtime entrega a `Hooks::on_idle`. Es decir,
  **ya hay un gancho que se ejecuta exactamente en ese PC**, con el guest fuera de interrupción, sin
  render a medias y una vez por vuelta de espera.
- Las 11 254 direcciones de retorno distintas del generado (`c.pr = …`) tienen etiqueta de
  reanudación: los cinco llamadores de `kmWaitVBlank` (`0x0c03118c`, `0x0c03152a`, `0x0c0317e6`,
  `0x0c0317f6`, …) también. Al reanudar, `run_guest_impl` sigue en `c.pr` cada vez que la función
  reanudada vuelve, y cada función restaura su `pr` de la pila del guest (`lds.l @r15+,pr`), así que
  la cadena de llamadores se rehace sola desde la RAM volcada.

**Lo que falta (no lo resuelve esta lectura).** (1) `run_guest` no tiene una forma de *arrancar*
reanudado: hace falta `run_guest_from(c, m, pc)` en `abi_bare.cpp` que entre al bucle de
`run_guest_impl` con `resumed = true`. (2) Todo el estado fuera del `Ctx` y la memoria: eventos
pendientes del scheduler (SPG, render en vuelo, timers del AICA, `gdrom-hle`), FIFO del TA, ARM7 y
mezclador, Maple, épocas de VRAM, y `in_exception_frame()` de los hooks (debe ser falso en el
volcado). La tabla de §3 de `pruebas-rapidas.md` ya lo lista.

**Propuesta para el plan.** (c) **Partir el 23**, condicionado a que el 22 se cierre:

- 23a. Volcar en `Hooks::on_idle` con `pc == 0x0C0370BA` y al cruzar el cuadro N (no en
  `on_vblank_out`, que corre dentro de un evento del scheduler): `Ctx` + RAM/VRAM/ARAM + scheduler.
- 23b. `run_guest_from` en `abi_bare.cpp` (parche al submódulo) y carga del volcado en
  `psp/ikaruga/main.cpp`.
- 23c. Serializar cada dispositivo (PVR/TA, AICA/ARM7, Maple, BIOS HLE). Cierra con `write_hash`
  idéntico en los 60 cuadros siguientes (build `double`).

## 50. Modo vertical (TATE) en el PC recompilado

**Pregunta.** ¿Cómo dibuja el juego el modo vertical: rota y llena 640×480, o dibuja una columna?

**Cómo lo medí.** Con pulsaciones: en el menú del título (`GAME START, PRACTICE, RANKING, CONFIG…`)
bajar 3 veces a CONFIG y entrar; la primera opción es `MONITOR TYPE` (valor inicial `HORIZONTAL 1`).
Cada `right` avanza `HORIZONTAL 1 → HORIZONTAL 2 → VERTICAL 1 → VERTICAL 2`. Con `VERTICAL 1`, salir
y empezar partida:

```
P=start@700,start@1000,start@1200,down@1400,down@1420,down@1440,a@1460,right@1630,right@1660,\
b@1720,up@1780,up@1800,up@1820,a@1860,start@2000,a@2200,start@2400,a@2600,start@2800,a@3000
XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 $B --config $T --rtc-seed 1 --press "$P" \
  --window --unthrottled --present-mode immediate --no-audio --screenshot-at S --max-frames S+8
# S = 1900, 3400, 4000
```

**Resultado.**

- Ya en el propio menú CONFIG, con `VERTICAL 1/2` el texto sale **girado 90°**.
- En juego (3400, 4000) la escena está **girada 90° y llena los 640×480**, sin bandas: el
  desplazamiento va de izquierda a derecha en pantalla, y el HUD (`1P`, `CHAIN STATUS`, puntuación,
  `PRESS START`) va girado en los bordes largos. En 1900 la pantalla está negra (transición).
- `VERTICAL 1` y `VERTICAL 2` son los dos sentidos de giro (`HORIZONTAL 1/2`, dos variantes del modo
  horizontal); no comparé el sentido de cada uno.

Es decir: **el giro lo hace el juego** en sus propias matrices/coordenadas de pantalla; el PVR recibe
polígonos ya girados en 640×480. El port no tiene que girar nada para obtener TATE.

**Propuesta para el plan.** (b) **Reescribir el 51** de forma más simple: en la PSP, TATE es elegir
`MONITOR TYPE = VERTICAL` en el juego (o dejarlo grabado en la VMU) y escalar los 640×480 del guest
a 272×363 girados 90° en el GE (`psp/stubs/video.cpp`, `video_draw_frame`: intercambiar x/y y la
escala al pasar los vértices; no hace falta pasada extra). El mapeo de controles TATE del paso 19
sigue haciendo falta. **Borrar el 50** como paso (contestado aquí). La medida del 49 (área 3:4 con
paneles) aplica a Steam, no al Dreamcast: en Dreamcast TATE es pantalla completa.
