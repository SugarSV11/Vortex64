# Contribuir a Vortex64

¡Gracias por ayudar! Vortex64 es un port a Linux de Project64 (GPLv2).

- **Bugs y peticiones:** abre un issue con la plantilla correspondiente. Dudas de uso: Discussions.
- **No publiques ROMs, BIOS/IPL ni contenido con copyright.** Saves y save states sí.
- **Pull requests:** haz un fork, crea una rama, compila con `cmake -S . -B build -G Ninja && cmake --build build`
  y describe cómo lo probaste. Un cambio por PR.
- **Estilo:** usa `Source/.clang-format`. Mantén los cambios de portabilidad bajo `#ifdef` cuando toquen código compartido con Windows.
- **Licencia:** al contribuir aceptas que tu código se distribuya bajo GPLv2.
