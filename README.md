# Vortex64 ⚠️ BETA

> **⚠️ Versión BETA** — Este es un programa en desarrollo beta. Puede contener bugs,
> comportamiento inestable y fallas. No todos los juegos están garantizados de funcionar.
> Úsalo bajo tu propio riesgo y reporta cualquier problema que encuentres.

## ¿Qué es Vortex64?

Vortex64 es un emulador de consolas Nintendo 64 para Linux, escrito en C++.
Se basa en la arquitectura de Project64 y está enfocado en llevar un emulador
nativo a Linux (binario ELF, sin Wine ni Proton), usando SDL2 para el video,
el audio (SDL2/PipeWire), el input y una primera GUI (Qt6).

En esta **versión beta**:

- El núcleo, RSP, video (SDL2/OpenGL), audio, input, saves, save states,
  frontend CLI y una primera GUI están funcionando y se ha verificado el
  funcionamiento con una ROM real de extremo a extremo.
- Aún quedan cosas pendientes: un recompiler JIT completamente terminado,
  el video embebido en la ventana de la GUI, y el empaquetado completo
  (AppImage / Flatpak).
- Es una versión de prueba: es posible que algunos juegos no funcionen.

Para más detalles del estado del port a Linux, consulta [`LINUX_PORT.md`](LINUX_PORT.md).

## Cómo ejecutarlo

Este repositorio acompaña el binario `Vortex64-x86_64.AppImage` (AppImage).
Para ejecutarlo:

```bash
chmod +x Vortex64-x86_64.AppImage && ./Vortex64-x86_64.AppImage
```

> El archivo `.AppImage` debe tener permisos de ejecución. Asegúrate de que
> tengas una ROM **tuya propia** (sin contenido con derechos de autor de Nintendo
> incluidos en este repositorio) para cargar dentro del emulador.

## Reportar bugs

Si encuentras algún fallo, abre una **Issue** en el
[repositorio de GitHub](https://github.com/SugarSV11/Vortex64/issues)
describiendo el problema, la versión, tu sistema y cómo reproducirlo.
Gracias por ayudarnos a mejorar Vortex64.

## Licencia

Vortex64 es software libre basado en la arquitectura del núcleo de Project64
y se distribuye bajo los términos de la **GNU General Public License v2 (GPLv2)**.
Ver el archivo [`LICENSE`](LICENSE) para el texto completo.

Vortex64 incorpora o enlaza varias bibliotecas de terceros (GLideN64, AsmJit,
SDL2, Duktape, Discord RPC, 7-Zip C SDK, zlib/minizip, Berkeley SoftFloat y Qt6).
Consulta [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) y el directorio
`licenses/` para la atribución completa.
