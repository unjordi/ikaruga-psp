# Controles en la PSP (fila T5)

`psp/stubs/input.cpp` lee el control de la PSP en cada vblank del guest con
`sceCtrlPeekBufferPositive` (inicializado una vez con `sceCtrlSetSamplingCycle(0)` y
`PSP_CTRL_MODE_ANALOG`). Lo convierte en un mando de Dreamcast en el bus Maple y lo combina con
OR con las pulsaciones guionadas (`pulsar=`); las pruebas que usan `pulsar=` no cambian.

## Mapeo por defecto y su fuente

Fuente: manual japonés de Ikaruga para Dreamcast (ESP, 2002), página 3 (uso del mando);
escaneo en archive.org, ítem `Ikaruga_2002_ESP_JP`
(<https://archive.org/details/Ikaruga_2002_ESP_JP>). Lo que dice de cada control:

| Dreamcast | Manual (texto) | Qué hace |
|---|---|---|
| Botón A | ショット、決定 | disparo; confirmar |
| Botón B | 属性変更、キャンセル | cambiar polaridad; cancelar |
| A+B a la vez | 力の解放 | soltar la energía |
| Gatillo R | 力の解放 | soltar la energía |
| Gatillo L, X, Y, stick analógico | 使用しません | no se usan |
| Cruceta | プレイヤー機の移動、項目の選択 | mover la nave; elegir en menús |
| START | ゲームスタート、プレイ中に押すとポーズ | empezar; pausa durante el juego |

El mismo manual dice que los botones se reasignan en CONFIG → CONTROLLER SETTING (p. 6), así que
el reparto fino también puede hacerse dentro del juego.

Por defecto la PSP reproduce el mando de Dreamcast en su sitio:

| PSP | Dreamcast | En Ikaruga |
|---|---|---|
| Cruceta | cruceta | mover |
| Stick analógico | cruceta (más allá del umbral) | mover |
| Cruz | A | disparo / confirmar |
| Círculo | B | polaridad / cancelar |
| R | gatillo R (255) | soltar la energía |
| L | gatillo L (255) | nada (el juego no lo usa) |
| Cuadrado | X | nada |
| Triángulo | Y | nada |
| START | START | empezar / pausa |
| SELECT | nada | — |

Cruz+Círculo también suelta la energía (A+B del manual).

## Claves de `ikaruga.cfg`

Una por línea, `clave=valor`, junto al EBOOT (lo lee `host::input_config`):

| Clave | Valores | Default |
|---|---|---|
| `boton_cruz`, `boton_circulo`, `boton_cuadrado`, `boton_triangulo`, `boton_l`, `boton_r`, `boton_start`, `boton_select` | `a b c x y z d start up down left right l r nada` (`l`/`r` = gatillo a fondo) | tabla de arriba |
| `umbral_stick` | distancia al centro, 0–127, desde la que el stick cuenta como cruceta | `64` |
| `tate` | `0` vertical normal; `1` / `antihorario` consola girada 90° a la izquierda (el borde derecho de la PSP queda arriba); `2` / `horario` girada a la derecha | `0` |
| `control` | `0` ignora el control real (solo quedan las pulsaciones guionadas) | `1` |

TATE solo rota la cruceta y el stick: con `tate=1` derecha→arriba, arriba→izquierda,
izquierda→abajo, abajo→derecha; `tate=2` es la rotación contraria. Los botones no se rotan
(reasígnalos con `boton_*` si hace falta). La rotación de la imagen es otra fila.

Ejemplo: disparo en Círculo y polaridad en Cruz, energía también en Cuadrado:

```
boton_cruz=b
boton_circulo=a
boton_cuadrado=r
```

## Cómo se probó (PPSSPP 1.20.4 bajo Xvfb, disco real, `rtc_seed=1`)

La tecla de START por defecto en PPSSPP 1.20.4 es la barra espaciadora:
`Core/KeyMapDefaults.cpp:28` `{CTRL_START, NKCODE_SPACE}` en `defaultQwertyKeyboardKeyMap`
(tag `v1.20.4` de hrydgard/ppsspp). El `ppsspp.ini` privado de la corrida no la cambia.

`scripts/probar-ikaruga-ppsspp.sh` acepta `PROBAR_TECLA=space@700`: cuando `ikaruga.log` muestra
un `hash` en el cuadro ≥ 700 (con `hash_every=10`), mueve el puntero sobre la ventana de PPSSPP,
le da el foco y mantiene `space` con `xdotool keydown` hasta el cuadro ≥ 720. Lo hecho queda en
`<salida>.tecla.log`. Xvfb no tiene gestor de ventanas; `xdotool windowfocus --sync` falló
(«xdo_focus_window reported an error») hasta buscar la ventana por `--onlyvisible` (ventana
«Ikaruga (dream-recomp, headless)»).

| Corrida (`capturar_en=1243`, sin `pulsar=`) | Resultado |
|---|---|
| Tecla espacio en el cuadro 700 | **título** (kanji 斑鳩, «PRESS START BUTTON», fondo de color; media RGB 90/75/64) |
| `pulsar=start@700`, sin tecla (referencia guionada) | título; PSNR 35,1 dB contra la de la tecla |
| Tecla espacio en el cuadro 700 con `control=0` (contraprueba) | **pantalla de arranque negra** (media RGB 1,3) |

El `write_hash` de la corrida con tecla y la guionada coincide en 700 y difiere desde 720: la tecla
se mantiene ~20 cuadros y la pulsación guionada 8, así que el juego no recibe la misma entrada;
la comparación vale por la captura del título. Además, sin pulsaciones el arranque sigue igual a
PC hasta el cuadro 240 (`HASTA=240 scripts/test-psp-igual-pc.sh`, 4 cuadros iguales).

Las capturas quedan en `build/psp/evidencia/t5-{tecla,guion,control0}/` (gitignored).
