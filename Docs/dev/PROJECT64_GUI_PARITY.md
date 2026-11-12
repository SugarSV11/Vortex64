# Paridad de la GUI: Project64 (WTL) ↔ Vortex64 (Qt)

> **Frontend Vortex64 (2026-10-02).** La interfaz visible se sustituyó por Vortex64 (ver `VORTEX64_GUI.md`): biblioteca
> propia en lugar del ROM Browser, menús solo con un juego en marcha, ajustes por categorías, depurador con pestañas,
> Input rediseñado, Acerca de y Bienvenida nuevos. Las filas de abajo describen la paridad *funcional* con upstream; donde
> dicen «menú», «ROM Browser» o «árbol de Settings» léase el equivalente de Vortex64 de la tabla «Ventanas del frontend» de
> `VORTEX64_GUI.md`. Los ID de comando (`ID_*`) siguen todos procesados por `QtMainMenu::ProcessMessage`; solo cuatro dejaron de tener
> entrada de menú porque su función tiene sitio propio: `ID_LANG_*` (Ajustes → General), `ID_FILE_ROMDIRECTORY` y
> `ID_FILE_REFRESHROMLIST` (Ajustes → Directorios; botón de reescaneo y F5), `ID_FILE_STARTEMULATION` («Iniciar» siempre arranca el
> juego) y `ID_HELP_SUPPORT_PROJECT64` (validación de un servicio web de upstream, decisión D).

Generado/curado a mano a partir de `Source/Project64/UserInterface` y `Source/Project64-gui` (2026-10-01).
Estados: **L** igual o equivalente · **B** adaptación necesaria en Linux · **D** diferencia justificada · **C** sin portar (justificado) · **E** no aplicable en Linux.

## Funciones de usuario

| Función de upstream | Vortex64 | Estado | Diferencia | Acción |
|---|---|---|---|---|
| Menú File/System/Options/Debugger/Help: 95 entradas con sus `ID_*` | las 95 existen con los mismos ID, textos de idioma, atajos (`MenuShortCuts`) y estados habilitado/marcado | L | — | `mainmenu-test`, `mainwindow-test` |
| Menú de depuración del RSP (`ID_PLUGIN_MENU`, un `HMENU` de Win32 que da el plugin RSP) | no existe | E | el depurador del RSP de upstream es Win32 (`Project64-rsp/Debugger`) | documentado |
| Pantalla completa (`ID_OPTIONS_FULLSCREEN`), salir al perder foco, cursor oculto, barra de estado y menú ocultos | ventana Qt a pantalla completa; el plugin sigue el tamaño del widget | B | upstream cambia el modo de vídeo; aquí lo hace el compositor. Antes no se ocultaba el cursor | cursor oculto añadido; pruebas `GLIDEN64_TEST_FULLSCREEN` |
| Volver de pantalla completa | recupera el tamaño que pidió el plugin | L | antes volvía a 640×480 | corregido |
| Navegador de ROM: columnas, orden, ordenar por columna, menú contextual, Return, arrastrar y soltar, vigilancia del directorio, caché | portado (`QtRomBrowser`) | L | la ordenación usaba el índice visual tras reordenar columnas | corregido; `rombrowser-test` |
| Información de ROM, Cheats, Enhancements, Log options, Welcome, About | portados | L | Welcome aplicaba sus opciones al cerrar la ventana (solo debe el OK); cheats/enhancements seguían abiertos al terminar el juego | corregido |
| «Support Project64» (ventana con código de seguidor y validación contra el servidor) | abre la página de soporte en el navegador; el nombre de seguidor no se muestra en About | D | el código de seguidor valida contra un servicio web de upstream y solo cambia un reconocimiento; no hay sustituto local | decisión documentada |
| Settings: 12 páginas, global y por juego, RDB editor | todas (`settings-test dump`, `sweep`) | L | las páginas Advanced/Defaults aparecían solo tras Apply | corregido (`OnBasicMode`) |
| Settings › Shell integration (asociar extensiones de ROM) | no hay página | E | Linux: el tipo MIME lo instala `project64-mime.xml` | documentado |
| Atajos de teclado (página + `Project64.sc3`) | portado | L | — | `settings-test` |
| Configuración de plugins (Video 4 pestañas, Audio, Input, RSP sin configuración en upstream) | portadas en `*-qtconfig.so` | L | la lista de resoluciones a pantalla completa de Glide64 estaba vacía | entrada «Native» |
| Debugger: Commands, Memory, Search, Dump, Symbols, Stack, Stack trace, TLB, Exceptions, DMA, CPU log, registros, breakpoints, scripts | portado | L | ver sección Debugger | `debugger-test`, `script-test` |
| ROM recientes, directorios recientes, idioma, guardar/cargar estado (slots, como, `.pj.zip`), capturas, pausa, reset suave/duro, velocidad | portado | L | — | `mainwindow-test`, sesión |
| Barra de estado (mensajes, velocidad, ocultar con juego) | portada | L | — | `mainwindow-test` |
| Always on top, pausar al perder el foco, protector de pantalla, Discord RPC | portado | B | el protector se inhibe por D-Bus (sin servicio no se envía) | `mainwindow-test` |
| Comprobación de «ya hay una instancia» | portada | L | — | — |
| Modo básico (oculta menús/ajustes avanzados) | portado | L | — | `rombrowser-test` |

## Entradas de menú de upstream (`MainMenu.cpp`)

| ID | Texto (English) | En Qt |
|---|---|---|
| `ID_CURRENT_SAVE_1` | (plugin/dinámico) | sí |
| `ID_CURRENT_SAVE_10` | (plugin/dinámico) | sí |
| `ID_CURRENT_SAVE_2` | (plugin/dinámico) | sí |
| `ID_CURRENT_SAVE_3` | (plugin/dinámico) | sí |
| `ID_CURRENT_SAVE_4` | (plugin/dinámico) | sí |
| `ID_CURRENT_SAVE_5` | (plugin/dinámico) | sí |
| `ID_CURRENT_SAVE_6` | (plugin/dinámico) | sí |
| `ID_CURRENT_SAVE_7` | (plugin/dinámico) | sí |
| `ID_CURRENT_SAVE_8` | (plugin/dinámico) | sí |
| `ID_CURRENT_SAVE_9` | (plugin/dinámico) | sí |
| `ID_CURRENT_SAVE_DEFAULT` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_APPLOG_FLUSH` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_BREAKPOINTS` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_CPULOG` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_DMALOG` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_DUMPMEMORY` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_EXCBREAKPOINTS` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_GENERATELOG` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_INTERRUPT_AI` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_INTERRUPT_DP` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_INTERRUPT_PI` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_INTERRUPT_SI` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_INTERRUPT_SP` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_INTERRUPT_VI` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_LOGOPTIONS` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_MEMORY` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_SCRIPTS` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_SEARCHMEMORY` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_STACKTRACE` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_STACKVIEW` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_SYMBOLS` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TLBENTRIES` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_APPCLEANUP` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_APPINIT` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_AUDIO` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_AUDIOPLUGIN` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_CONTROLLERPLUGIN` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_GFXPLUGIN` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_MD5` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_N64SYSTEM` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_PLUGINS` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_RECOMPILER` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_REGISTERCACHE` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_RSP` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_RSPPLUGIN` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_SETTINGS` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_TLB` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_UNKNOWN` | (plugin/dinámico) | sí |
| `ID_DEBUGGER_TRACE_USERINTERFACE` | (plugin/dinámico) | sí |
| `ID_DEBUG_BREAK_ON_ADDRESS_ERROR` | (plugin/dinámico) | sí |
| `ID_DEBUG_BREAK_ON_UNHANDLED_MEM` | (plugin/dinámico) | sí |
| `ID_DEBUG_DISABLE_GAMEFIX` | (plugin/dinámico) | sí |
| `ID_DEBUG_END_ON_PERM_LOOP` | (plugin/dinámico) | sí |
| `ID_DEBUG_LANGUAGE` | (plugin/dinámico) | sí |
| `ID_DEBUG_RECORD_RECOMPILER_ASM` | (plugin/dinámico) | sí |
| `ID_DEBUG_SHOW_DLIST_COUNT` | (plugin/dinámico) | sí |
| `ID_DEBUG_SHOW_PIF_ERRORS` | (plugin/dinámico) | sí |
| `ID_DEBUG_SHOW_RECOMP_MEM_SIZE` | (plugin/dinámico) | sí |
| `ID_DEBUG_STEP_ON_BREAK_OPCODE` | (plugin/dinámico) | sí |
| `ID_FILE_ENDEMULATION` | End Emulation | sí |
| `ID_FILE_EXIT` | Exit | sí |
| `ID_FILE_OPEN_COMBO` | Open Combo | sí |
| `ID_FILE_OPEN_ROM` | Open ROM | sí |
| `ID_FILE_REFRESHROMLIST` | Refresh ROM List | sí |
| `ID_FILE_ROMDIRECTORY` | Choose ROM Directory... | sí |
| `ID_FILE_ROM_INFO` | ROM Info... | sí |
| `ID_FILE_STARTEMULATION` | Start Emulation | sí |
| `ID_HELP_ABOUT` | About Project64 | sí |
| `ID_HELP_DISCORD` | Discord | sí |
| `ID_HELP_SUPPORT_PROJECT64` | Support Project64 | sí |
| `ID_HELP_WEBSITE` | Website | sí |
| `ID_OPTIONS_ALWAYSONTOP` | Always on top | sí |
| `ID_OPTIONS_CONFIG_AUDIO` | Audio Settings | sí |
| `ID_OPTIONS_CONFIG_CONT` | Input Settings | sí |
| `ID_OPTIONS_CONFIG_GFX` | Graphics Settings | sí |
| `ID_OPTIONS_CONFIG_RSP` | RSP Settings | sí |
| `ID_OPTIONS_CPU_USAGE` | Show CPU Stats | sí |
| `ID_OPTIONS_FULLSCREEN` | Fullscreen | sí |
| `ID_OPTIONS_SETTINGS` | Configuration | sí |
| `ID_PLUGIN_MENU` | (plugin/dinámico) | NO |
| `ID_PROFILE_GENERATELOG` | (plugin/dinámico) | sí |
| `ID_PROFILE_PROFILE` | (plugin/dinámico) | sí |
| `ID_PROFILE_RESETCOUNTER` | (plugin/dinámico) | sí |
| `ID_SYSTEM_BITMAP` | Capture screenshot | sí |
| `ID_SYSTEM_CHEAT` | Cheats... | sí |
| `ID_SYSTEM_ENHANCEMENT` | Enhancements... | sí |
| `ID_SYSTEM_GSBUTTON` | GS Button | sí |
| `ID_SYSTEM_LIMITFPS` | Limit FPS | sí |
| `ID_SYSTEM_LOAD` | Load... | sí |
| `ID_SYSTEM_PAUSE` | Resume | sí |
| `ID_SYSTEM_RESET_HARD` | Hard Reset | sí |
| `ID_SYSTEM_RESET_SOFT` | Reset | sí |
| `ID_SYSTEM_RESTORE` | Load State | sí |
| `ID_SYSTEM_SAVE` | Save State | sí |
| `ID_SYSTEM_SAVEAS` | Save As... | sí |
| `ID_SYSTEM_SWAPDISK` | Swap Disk | sí |

## Configuración gráfica y de entrada (auditoría de la fase 3)

| Diálogo | Estado | Comprobación |
|---|---|---|
| Glide64 (`Project64-video/ConfigQt.cpp`, páginas Basic / Emulation / Debug / Texture enhancement) | Los 74 `IDC_*` del `.rc` de upstream están cubiertos (los que no aparecen por nombre son rótulos o los 11 combos de trazas, que están por `SettingId`); `Settings.cpp`, `Settings.h` y `SettingsID.h` son idénticos a upstream salvo 1–2 líneas; mismas listas, rangos y valores por defecto; ajustes globales (Glide64 no tiene perfil por juego en la interfaz) | `plugin-config-test video prep/write/read`: 48 controles de todas las páginas cambiados, OK, reabiertos y comparados |
| GLideN64 (`GLideNUI` del submódulo: el mismo `ConfigDialog`/`configDialog.ui` de upstream, solo cambia `Config_linux.cpp` y la lista de resoluciones de `fullscreenresolutions_linux.cpp`) | Todas las opciones de la versión incluida; ajustes globales + perfil del juego (`GLideN64.custom.ini`, solo las diferencias); `threadedVideo`, tamaño de fuente OSD y atajos son globales por diseño de upstream | `gliden64-test` con `GLIDEN64_TEST_CONFIGRT=1`: 67 controles, OK, reabiertos y comparados |
| Input (`Project64-input/ConfigQt.cpp`) | ver `PROJECT64_PARITY.md` | `input-capture-test`, `input-e2e-test` |

Diferencias deliberadas: en Linux no hay selector de modo de pantalla completa por resolución real (la lista sale de las pantallas de Qt y una sola frecuencia), ni "borderless/monitor" de Win32 (`M64P_GLIDENUI`/Win32 `#ifndef`).
