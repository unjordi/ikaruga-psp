# Scenes the image tests compare against the Flycast reference series (build/flycast/ref/p480,
# 10 fps, scripts/capturar-flycast.sh with the chapter-1 presses). Sourced by the tests.
# VENTANA_*: the series indices that hold the scene, with margin. Flycast runs by wall clock, so a
# scene's index moves a few frames between recordings; comparar-flycast.sh answers "ESCENA NO
# ENCONTRADA" (exit 3) when the best image is a window edge that still improves outward.
# CUADRO_*: the guest frame the PSP is captured at for that scene (docs/referencia-flycast.md).
PRESSES_CAP1="start@700,start@1000,start@1200,a@1400,start@1600,a@1800,start@2000,a@2200"
VENTANA_TITULO="170-205"   # settled title (logo, PRESS START)
VENTANA_SELECT="250-312"   # SELECT GAME MODE, before the pause menu fades in
VENTANA_CAP1="380-420"     # start of chapter 1 (ship in front of the tower)
CUADRO_SELECT=1750
CUADRO_CAP1=2350
