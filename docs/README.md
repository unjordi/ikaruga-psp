# Documentación

Dos grupos. Los **vigentes** describen cómo es el proyecto hoy y se actualizan en el mismo PR que
cambia lo que describen. Las **fotos de medición** (`mediciones/`) registran una medición de su
momento: no se actualizan y cada una empieza con un aviso que dice cuál es el doc vigente.

Todo el repo es sin datos del juego: necesitas tu propia copia del disco (`primer-uso.md`).

## Vigentes

| doc | responde |
|---|---|
| [`plan.md`](plan.md) | qué está hecho y qué sigue, una sola lista de pasos en orden |
| [`primer-uso.md`](primer-uso.md) | cómo ir de tu disco a la carpeta de la PSP, y todas las claves de `ikaruga.cfg` |
| [`reproducir-pc.md`](reproducir-pc.md) | cómo recompilar Ikaruga en PC con dream-recomp (el C++ generado y el lanzador de depuración) |
| [`arranque-psp.md`](arranque-psp.md) | qué hace el EBOOT al arrancar, qué archivos lee y escribe, el log de `tick` |
| [`controles-psp.md`](controles-psp.md) | cómo se mapea el control de la PSP al mando de Dreamcast |
| [`geometria-psp.md`](geometria-psp.md) | cómo se dibujan en el GE los polígonos del PVR2 y la prueba de geometría |
| [`titulo-psp.md`](titulo-psp.md) | cómo llegan las texturas al GE y la prueba del título |
| [`video-psp.md`](video-psp.md) | recorte de usuario y niebla, y su prueba |
| [`referencia-flycast.md`](referencia-flycast.md) | la referencia de imagen (Flycast): cómo se captura, cómo se compara, umbrales |
| [`correctitud-psp.md`](correctitud-psp.md) | qué garantiza el EBOOT frente al juego original y qué prueba cada garantía; el build `double` |
| [`idle-skip.md`](idle-skip.md) | el salto de la espera de `kmWaitVBlank` |
| [`estado-guardado.md`](estado-guardado.md) | volcar y cargar el estado completo de la máquina |
| [`pruebas-rapidas.md`](pruebas-rapidas.md) | PPSSPP sin limitador, `dibujar_desde`, y cómo correr las pruebas |
| [`velocidad-psp.md`](velocidad-psp.md) | cómo se mide la velocidad, dónde está y qué optimizaciones hay |
| [`memoria-psp.md`](memoria-psp.md) | la memoria del EBOOT contra el presupuesto |

Los scripts están en [`../scripts/README.md`](../scripts/README.md). `scripts/armar-docs.sh` junta el plan y los docs vigentes de esta tabla en un solo PDF.

## Fotos de medición (`mediciones/`)

Cada una conserva su texto tal como se midió.

| foto | qué registra |
|---|---|
| [`framework-comparacion.md`](mediciones/framework-comparacion.md) | la comparación de recompiladores de Dreamcast que decidió usar dream-recomp |
| [`medicion-pc.md`](mediciones/medicion-pc.md) | velocidad, perfil, FPU y cobertura de Ikaruga en el PC recompilado |
| [`tamano-psp.md`](mediciones/tamano-psp.md) | tamaño del `.elf` para la PSP contra el presupuesto |
| [`revision-optimizaciones.md`](mediciones/revision-optimizaciones.md) | revisión del diseño contra lo medido y catálogo de optimizaciones (los códigos A4, B6, C8, T5 del plan) |
| [`investigacion-pendientes.md`](mediciones/investigacion-pendientes.md) | investigación de los pasos abiertos de un plan anterior (sus números de paso no son los del plan actual) |
| [`arranque-psp.md`](mediciones/arranque-psp.md) | velocidad del arranque con y sin idle-skip y con `-O2` |
| [`correctitud-psp.md`](mediciones/correctitud-psp.md) | cómo se encontró y arregló cada divergencia con el PC, incluida la de 1983 |
| [`titulo-psp.md`](mediciones/titulo-psp.md) | las texturas del título, RAM y tiempo de cuadro de entonces |
| [`geometria-psp.md`](mediciones/geometria-psp.md) | tiempos de cuadro de la primera geometría y la corrida que moría con dos PPSSPP |
| [`video-psp.md`](mediciones/video-psp.md) | cómo se halló la causa de las columnas laterales y de la niebla |
| [`velocidad-psp.md`](mediciones/velocidad-psp.md) | cada cambio de velocidad probado, con su tabla |
| [`pruebas-rapidas.md`](mediciones/pruebas-rapidas.md) | tiempos de las pruebas antes y después del avance rápido, y la estimación del estado guardado |
