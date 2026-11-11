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
