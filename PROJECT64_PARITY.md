# PROJECT64_PARITY — matriz de paridad Vortex64 ↔ Project64 upstream

Regla: Vortex64 es un **port directo** de Project64. Todo código portable se
conserva literalmente; solo cambia lo que depende de Windows. Esta matriz es
el registro de cada diferencia contra upstream.

## Referencia

| Campo | Valor |
|---|---|
| Repositorio oficial | https://github.com/project64/project64 (`origin` de la referencia) |
| Copia local (solo lectura) | `~/Escritorio/project64-upstream-reference` |
| Commit de referencia (develop) | `24376e868350792a181d2121fec20d37354559d6` |
| Punto de bifurcación de Vortex64 | `777fbdfb23ffccabc3703d996544b9db8e135833` (merge-base verificado) |
| Ref local en este repo | `refs/remotes/upstream/develop` (importado con `git fetch` desde la copia local) |

Reproducir el diff del port: `git diff upstream/develop -- Source/`
(o `git diff 777fbdfb2 HEAD` para el delta puro sobre la base).

## Clases

- **A** idéntico a upstream
- **B** cambio necesario para Linux
- **C** función upstream aún no portada
- **D** cambio Vortex64 innecesario/divergente
- **E** no aplicable en Linux

## Estado de la auditoría

Auditoría inicial (por directorio, medida con `git diff --numstat` sobre la
base 777fbdfb2, ignorando fin de línea). La auditoría función por función se
va completando subsistema a subsistema; un subsistema no se marca terminado
hasta compararlo realmente.

| Subsistema | Delta (líneas, sin EOL) | Estado |
|---|---|---|
| Project64-core | +637 −175 | clasificado archivo a archivo en `PROJECT64_PARITY_FILES.md` (sin `?`) |
| Project64-video | +767 −355 (más ruido CRLF→LF, clase D) | clasificado en `PROJECT64_PARITY_FILES.md` |
| Common | +325 −23 (más ruido CRLF→LF, clase D) | clasificado en `PROJECT64_PARITY_FILES.md` |
| Project64-rsp / rsp-core | +342 −62 | clasificado en `PROJECT64_PARITY_FILES.md` |
| Project64-audio / input | +438 −32 | clasificado en `PROJECT64_PARITY_FILES.md` |
| Project64-gui (Qt6) | +1074 (GUI nueva; upstream usa WTL en `Source/Project64/UserInterface`) | **L** GUI nueva (Qt en lugar de WTL); páginas de ajustes, menús, navegador, depurador y scripts portados (ver secciones siguientes); las diferencias están en `PROJECT64_PARITY_FILES.md` |
| Project64-cli | +321 (sin equivalente upstream) | frontend Linux auxiliar |
| Recompilador x64 | ver «Recompilador x64» | **L** sin `BreakPoint` de stubs alcanzables; verificado con `jit-fuzz-test` y ROM reales |

## Divergencias conocidas registradas

1. **Fin de línea (D):** archivos de `Project64-video` y `Common` (p. ej.
   `rdp.cpp`, `path.cpp`, `ucode06.cpp`) cambiaron CRLF→LF, lo que oculta el
   delta real. Se revierte para minimizar el diff.
2. **GUI (C):** la GUI Qt6 se escribió desde cero, no como port de
   `Source/Project64/UserInterface`. Faltan (confirmado por prueba manual)
   Settings gráficos/completos y otras funciones. No hay paridad todavía.
3. **Plugins (C/D):** la carga es por `dlopen` con nombres por defecto
   fijos; falta descubrimiento, selección y persistencia como en upstream.
4. **Recompilador x64 (C):** upstream avanzó 13 commits (502 líneas) sobre
   la base; Vortex64 añadió operaciones propias en paralelo. Se fusiona
   tomando la versión upstream cuando ambos implementan lo mismo.

## Estado medido tras la fusión con upstream

- Delta `Source/` contra upstream/develop: +4477 / −690 líneas en 156 archivos
  (129 modificados, 27 nuevos). Ningún archivo de upstream ha sido borrado;
  `Source/Project64/` (GUI WTL) sigue en el árbol sin compilarse en Linux.
- Recompilador x64: la fusión adoptó la implementación upstream de `J()` y las
  ~500 líneas nuevas de upstream. Siguen siendo código propio de Vortex64
  (clase C: upstream mantiene `BreakPoint` stub): `SPECIAL_DIVU`,
  `SPECIAL_DMULT`, `JumpToSection`, `JumpToUnknown`.
- Renombrado `glXxxARB` → `pj64_glXxxARB` en `Project64-video`
  (`OGLcombiner.cpp`, `glitchmain.h`, …): clase **B**. Razón Linux: colisión de
  símbolos con libGL que provocaba segfault (commit 762a91265). Pendiente
  evaluar una alternativa de menor delta (macros) que mantenga las líneas
  upstream intactas.
- GUI: upstream `Source/Project64/UserInterface` (+ `Settings/`) ≈ 11 400
  líneas y 14 páginas de configuración (Options, Directories, Plugin,
  GameBrowser, KeyboardShortcuts, AdvancedOptions, Defaults, DiskDrive y las
  páginas por juego General/Plugin/Recompiler/Status/DiskDrive). La GUI Qt
  actual tiene ~860 líneas y solo un diálogo de plugins simplificado, sin
  ninguna de las páginas anteriores: **C**, sin paridad.

## Recompilador x64 y ABI System V (auditoría Codex, ítems 2 y 4 verificados a mano)

El JIT x64 de upstream está escrito para el ABI Microsoft x64 y Vortex64 no
añade ningún puente SysV. Clase **C/B** (portar solo la capa de ABI):

1. Prólogo/epílogo de bloque (`x64RecompilerOps.cpp` ~3370/3673) no
   preserva rbx, rbp, r12–r15, que el register cache sí asigna
   (`x64RegInfo.cpp:14`).
2. `CX64Ops::CallThis` pone `this` en rcx (`x64ops.cpp:625`); SysV exige rdi.
   Todos los sitios de llamada colocan argumentos en rcx/rdx/r8/r9.
3. Llamadas virtuales a `MemoryHandler` marshalan Win64.
4. `CX64RegInfo::BeforeCallDirect` (`x64RegInfo.cpp:259`) usa la lista de
   registros volátiles Win64 (omite rdi/rsi, que son asignables).
5. Alineación de pila en las llamadas con número impar de push (sin
   confirmar aún).
6. Trampolín de breakpoint de depuración con argumentos Win64.

Estado tras el commit `jit: adapt x64 recompiler to the System V AMD64 ABI`
(clase **B**, `#if defined(_WIN32)` conserva el comportamiento Microsoft):

1. Hecho: `EnterCodeBlock`/`ExitCodeBlock` guardan y restauran rbx, rbp,
   r12–r15 en Linux.
2. Hecho: alias `X64Abi::Arg0..Arg3` (`x64ops.h`) sustituyen rcx/rdx/r8/r9 en
   todas las llamadas generadas. No se tocaron los usos con semántica
   implícita (`cl` en desplazamientos, `edx` en `mul`/`cdq`).
3. Hecho: las llamadas virtuales a `MemoryHandler` usan los alias.
4. Hecho por exclusión: rdi/rsi ya no entran en el register cache en Linux,
   así que la lista de volátiles de `BeforeCallDirect` es correcta.
5. Hecho: `BeforeCallDirect`/`AfterCallDirect` mantienen la pila alineada a
   16 con un número impar de push (corrección común a ambos ABI).
6. Hecho: el trampolín de breakpoint alinea la pila y usa los alias.

Validado (ver «Recompilador x64» más abajo): el CLI y la GUI Qt ya no fuerzan el intérprete
(`Setting_ForceInterpreterCPU` solo se aplica en Windows); SM64 y otras ROM locales corren con el recompilador y
`jit-fuzz-test` compara el JIT con el intérprete. El segfault 139 que aquí se describía quedó diagnosticado y corregido
(ver «Segfault 139 de run-gui-tests.sh»).

Implementaciones propias de Vortex64 que upstream deja como `BreakPoint`
(`SPECIAL_DIVU`, `SPECIAL_DMULT`, `JumpToSection`, `JumpToUnknown`):
se conservan; falta auditar su corrección y añadir pruebas.

## JIT: causa raíz del segfault y estado

- Causa raíz del segfault (rip=0xffffffff80000000): `MemoryHandler` usa
  `__interface` (MSVC: métodos virtuales puros), pero en Linux `__interface`
  se define como `struct`, así que la interfaz no tenía vtable y el código
  generado saltaba por la ranura 0/1 de un objeto sin vtable. Corregido en
  `MemoryHandler.h` (clase **B**, virtual explícito y en el mismo orden).
- Segunda revisión (Codex, verificada a mano): `add rsp,8` de
  `AfterCallDirect` pisaba ZF entre `test al,al` y el salto condicional.
  Corregido con `lea`.
- El recompilador x64 de upstream está incompleto: 104 de 187 manejadores de
  opcode son `BreakPoint` stubs (todo el FPU, LB/LH/LWL/LWR/..., DADD*, MULT,
  DIV, TLBR/TLBWR/TLBP, ...). Super Mario 64 con `--recompiler` se detiene en
  `COP0_CO_TLBP` (`x64RecompilerOps.cpp:3060`). **Clase C, upstream WIP**; no
  es un problema del port Linux. Por eso el intérprete sigue siendo el núcleo
  por defecto del CLI (`--recompiler` lo cambia) y no se declara paridad de
  ejecución del JIT.

## GUI: estado real (auditoría, corrige afirmaciones anteriores)

Los manejadores anteriores `OnSettingsAudio`, `OnSettingsInput`,
`OnSettingsCheats`, `OnSettingsEnhancements`, `OnRomInfo`, `OnHotkeys`,
`OnRomBrowser` y `OnSettingsPlugins` de `MainWindow.cpp` solo mostraban un
`QMessageBox` con texto: **no eran implementaciones** y los documentos
`LINUX_PORT_AUDIT.md` / `OPENCODE_HANDOFF.md` que los daban por completos
eran incorrectos. Estado real tras esta sesión:

| Página / función upstream | Estado |
|---|---|
| Settings › Options (General) | **L** portada (`QtGeneralOptionsPage`, probada) |
| Settings › Advanced | **L** portada |
| Settings › Defaults | **L** portada (mantiene el emparejamiento Known/Unknown invertido de upstream) |
| Settings › Directories | **L** portada, probada |
| Settings › Disk drive | **L** portada |
| Settings › ROM selection (GameBrowser) | **L** portada, probada (campos/orden del ROM browser) |
| Settings › Plugin (Video/Audio/Input/RSP) | **L** portada; descubrimiento por escaneo de `.so` (`CPluginList`), probado |
| Settings › Game (General, Recompiler, Plugin, Disk drive) | **L** portadas; Game_CpuType probado |
| Settings › Game Status (RDB editor) | **L** portada (`QtGameStatusPage`, solo con `Setting_RdbEditor` como upstream); upstream edita `Config/Project64.rdb` junto al ejecutable. **L** en Linux: si el fichero instalado no es escribible (instalación de solo lectura, AppImage, Flatpak, `/usr`) se copia una vez a `$XDG_CONFIG_HOME/project64/Config/` (permisos de escritura del usuario: `copy_file` conservaba el 0444) y esa copia se lee y se edita; el original no se toca (`CSettingTypeRomDatabase::DatabasePath`, también `Video.rdb`/`Audio.rdb`, la lista de ROM y la identificación alternativa). Una copia de usuario ya creada tiene prioridad sobre el RDB instalado (decisión: no se fusionan actualizaciones del RDB instalado con ediciones del usuario). Prueba: `settings-test rdb` (instalación de solo lectura, copia, edición, reapertura, original intacto, instalación escribible in situ) en `run-offscreen-tests.sh` |
| Settings › Keyboard shortcuts | **L** portada (`QtShortCutsPage` sobre el `CShortCuts` de upstream compilado sin cambios; `Project64.sc3` compatible con Windows), probada |
| ROM Browser | **L** portado sobre `CRomList` del core (columnas, orden multi-clave, persistencia), probado con directorio real |
| ROM Info | **L** portado (`RomInformation`), probado con una ROM real |
| Recent ROMs / Recent dirs | **L** usan `File_RecentGameFile*` / `Directory_RecentGameDir*` como upstream |
| Options › Graphics settings (`DllConfig` de Project64-video) | **L** diálogo Qt (`ConfigQt.cpp`): Basic, Emulation, Debug, Texture enhancement; prueba de ida y vuelta con `CPlugins::ConfigPlugin` |
| Options › Audio settings (`DllConfig` de Project64-audio) | **L** diálogo Qt (`ConfigQt.cpp`), probado |
| Options › Input settings (`DllConfig` de Project64-input) | **L** diálogo Qt (`Project64-input/ConfigQt.cpp`): 4 jugadores, Setup/Reset/Options/Shortcuts. Asignación libre: cualquier tecla física (identidad = scancode físico, X keycode→tabla de SDL; incluye Escape, Enter, Espacio, Tab, flechas, modificadores izq./der., teclado numérico, Super, signos, teclas internacionales), botones del ratón (izq., central, der., Atrás, Adelante y extras) y rueda (↑ ↓ ← →) como botones momentáneos; captura con filtro de eventos de aplicación (nada llega a los widgets mientras se espera), clic derecho en el campo → Asignar/Borrar, aviso al mover un duplicado; joystick/hotplug/zona muerta intactos; probado: `input-capture-test write/read [B]` (diálogo, dos juegos de 18 entradas, persistencia, borrado), `input-e2e-test [B]` (XTEST→SDL→`GetKeys`: 17 + 18 entradas llegan al estado N64: Escape, Enter, Espacio, Tab, flechas, Shift/Ctrl/Alt izq. y der., Super izq. y der., keypad, letras, números, ratón 1/2/3/Atrás/Adelante/6.º botón, rueda ↑↓←→) e `input-gamepad-test` (mando virtual uinput: conexión en caliente, asignación en el diálogo, `GetKeys`, desconexión y nueva conexión) |
| Options › RSP settings | **N/A** el plugin RSP no exporta `DllConfig` en upstream (código comentado) |
| Cheats / Enhancements | **L** portados (`Dialogs/CheatDialog.cpp`, `EnhancementDialog.cpp`) sobre `g_Enhancements`; árbol tri-estado probado |
| Menú principal y atajos (`MainMenu.cpp`) | **L** `QtMainMenu` porta `FillOutMenu`/`ProcessMessage` (File, System, Options, Debugger parcial, Help, guardado por ranuras, idiomas, MRU) y usa `MenuShortCuts.cpp` de upstream; probado (`mainmenu-test`) |
| About | **L** portado |
| Depurador: ventanas (Comandos, Registros, Memoria, Volcado y Búsqueda de memoria, Símbolos, Pila, Traza de pila, TLB, Excepciones, Log DMA, Log de comandos) | **L** portadas sobre el back end de upstream (`Breakpoints`, `Symbols`, `DebugMMU`, `MemoryScanner`, `Assembler`, `CPULog`, `DMALog`, compilados sin cambios); `debugger-test` (ROM real, Xvfb) |
| Depurador: ventana Scripts y diálogo Autorun scripts | **L** portadas sobre el `CScriptSystem` real de upstream (ver «Scripts del depurador»); `script-test` |
| Debugger › Logging › Log options | **L** `LogOptionsDialog` (Registers / Pif Ram / General); `mainmenu-test` |
| ROM Browser: menú contextual (Play, Play with disk, Refresh, Choose ROM directory, ROM info, Edit game settings / cheats / enhancement del ROM seleccionado) | **L** `QtRomBrowser::FillContextMenu` + `MainWindow::EditRomSettings` (identidad del ROM como en `WM_COMMAND ID_POPUPMENU_*`); `rombrowser-test` (entradas y señales) |
| Welcome | **divergencia documentada**: no hay asistente; el primer arranque abre la ventana normal con valores por defecto (español, GLideN64, biblioteca vacía) — `ApplyFirstRunDefaults`, `first-run-test` |
| Ventana de soporte / ProjectSupport (códigos de supporter validados en línea con WinInet y el ID de máquina de Windows) | **E** no aplicable en Linux; Help › Support Project64 abre la web |
| Discord Rich Presence (`DiscordRPC.cpp`, `discord-rpc`; `Setting_EnableDiscordRPC`, `CMainGui::DiscordRPCChanged`, actualización al cargar el juego y con `pj64.close()`) | **L** `DiscordRPC.cpp` de upstream compilado (solo `Timestamp()` cambia fuera de Windows) sobre la biblioteca vendida con su capa de conexión Linux (`connection_unix.cpp`: socket `discord-ipc-N` en el directorio de ejecución, también Flatpak y Snap); `mainwindow-test` con un servidor que hace de cliente de Discord (saludo con el id de aplicación, `SET_ACTIVITY` con título, inicio e icono, «Not in-game», apagado y reconexión). Sin cliente de Discord real a mano: la comprobación con Discord en pantalla queda para prueba manual |

Las páginas y diálogos Qt viven en `Source/Project64-gui/Settings/`; comparten motor
(`QtSettingsPage`) con las reglas de los controles "Modified" de upstream y
`Source/Project64/Settings/UISettings.cpp` se compila sin cambios salvo
`DPIScale()`. `Source/Project64/Plugins/PluginList.cpp` solo cambia
`*.dll`/`LoadLibrary` por `*.so`/`dlopen`.

## GLideN64 como plugin de vídeo (`Source/GLideN64-linux`)

| Función | Estado |
|---|---|
| Plugin `libGLideN64.so` (API Zilmar 1.5) construido desde el submódulo sin modificarlo; el CMake de upstream rechaza UNIX sin Mupen64Plus | **L** `Source/GLideN64-linux/CMakeLists.txt` + 8 archivos de plataforma (contexto SDL2/GLX, `DisplayWindow`, rutas XDG, captura PNG, memoria, modos de pantalla Qt, fuente OSD) |
| Render sobre el widget del frontend, tamaño del área de vídeo = resolución en ventana de GLideN64 (`SetRenderSize` ≙ `SetWindowPos`), seguimiento del tamaño real vía `glXQueryDrawable`, captura de pantalla | **L** `gliden64-test` (ROM real, Xvfb): SM64 dibuja (19162 colores), 800x600 pedido por el ini, capturas del tamaño correcto, pausa/guardar/restaurar/reset, diálogo de ajustes (GLideNUI) y cierre con la ROM en marcha |
| Ajustes en `$XDG_CONFIG_HOME/project64/GLideN64/GLideN64.ini` (por defecto el de la instalación), caché y datos en XDG | **L** |
| Pantalla completa | **L** la hace el frontend; el plugin sigue el tamaño del drawable (el cambio de modo de pantalla de Windows no se porta) |
| Vídeo en hilo (`threadedVideo=1`) | **L** `GLIDEN64_TEST_THREADED=1 gliden64-test` en PASS (verificado a mano, no forma parte de `run-gui-tests.sh`) |
| CRC/matrices con SSE (`CRC_OPT`, `RSP_LoadMatrixX86`) | **C** no activados: se usan las versiones portables |
| Calidad visual comprobada con una sola ROM (SM64); otras ROM, shaders de FB emulation, HiRes: sin verificar | **pendiente** |

## Recompilador x64 (estado a 2026-09-30, trabajo en curso)

| Tema | Estado |
|---|---|
| Stubs de upstream (104 funciones con `BreakPoint()` puro: TLBP, COP1 completo, DADD..., LWL..., etc.) | **L** ejecutan la implementación del intérprete (`CompileInterpreterOp` → `R4300iOp::ExecuteFallbackOpcode`); las excepciones se detectan con `m_JumpToLocation` y terminan el bloque |
| Aritmética/lógica/SLT/MTLO/MTHI, cargas y stores 8/16/32, comparaciones de salto, JR/JALR/J/JAL, saltos probables, unión de secciones | **L** reescritos o portados de x86; núcleo de 32 bits nativo, núcleo de 64 bits vía intérprete salvo lo ya nativo |
| Verificación | `jit-fuzz-test` (programas aleatorios intérprete vs recompilador, con reducción automática): >50 000 programas sin diferencias (32/64 bits, sin caché, sin fastSP); SM64 corre 240 s con el JIT sin errores, con pausa, reset, save/load state, cambio de plugin, cierre y reapertura; debugger-test, script-test y mainwindow-test pasan con `PJ64_TEST_JIT=1` |
| Depurador con JIT | **L** parada/paso (`CRecompiler::DebuggerStep`), puntos de ejecución, CPU log y callbacks de scripts (el JIT pasa por el intérprete mientras se necesitan) |
| `RecompilerMain_Lookup_validate` | defecto de upstream corregido (volvía a traducir el PC antiguo) |
| `BlockLinking_Eager/Adaptive` (por defecto x64: None, no cambiado) | **L** arreglado: `Compile_BranchLikely` sobrescribía `m_Jump.TargetPC` con el destino del salto cuando el análisis ya había creado la sección del delay slot (PC+4), por lo que el delay slot de un salto probable tomado no se ejecutaba; ahora respeta el análisis como x86 y completa `m_Cont` si el análisis no creó sección de continuación (x86 aborta con BreakPoint). `CCodeBlock::AnalyzeInstruction` (código compartido con upstream) no conocía `SYNC` y abortaba el análisis: añadido. Regresión: programas dirigidos de `jit-fuzz-test` (se comprueba que fallan con el código anterior) y la campaña de la suite con `link=0/1/2` |
| Salto probable con condición constante (aviso de Codex) | **L** cubierto por los dirigidos: cada salto × valores límite × registros conocidos por el compilador (constantes por `LUI/ORI`) o no × delay slot × colocación (también última palabra de página) × destino (salta una instrucción / el propio delay slot). Sin diferencias en None/Eager/Adaptive con núcleo 32 y 64 |
| `RecompilerMain_VirtualTable_validate` | **L** implementado: es alcanzable (Ajustes → Recompilador → búsqueda de funciones «virtual lookup» con `SMM-FUNC`, activo por defecto; el RDB usa `FuncFind=2` en Conker's Bad Fur Day). Upstream lo deja en `BreakPoint`. Comprobado con SM64 (`LIFECYCLE_FUNCFIND=2 LIFECYCLE_SMMFUNC=1`), pausa/estado/reset incluidos |
| `RecompilerMain_ChangeMemory` (`FuncFind_ChangeMemory`=3) | **C** se deja como upstream (`BreakPoint`): no es alcanzable (la UI de upstream lo tiene comentado — `SettingsPage-Game-Recompiler.cpp:65` — y la nuestra no lo ofrece; ninguna entrada de `Project64.rdb` usa `FuncFind=3`; sólo un ini editado a mano) y upstream no tiene implementación que portar |
| Tamaño de RDRAM al cargar un estado | **L** defecto de upstream corregido: una ROM que no está en el RDB arranca con `rdramSize` = valor «conocido» (4 MB) pero con la memoria «desconocida» (8 MB); al cargar un estado `Game_RDRamSize` pasaba a 8 MB y la tabla de saltos del recompilador (4 MB) quedaba pequeña (lectura fuera de la tabla, SIGSEGV). `CN64System::SetRdramSizeOfState` rehace el mapa de funciones si el tamaño cambia |
| Fuzz | `jit-fuzz-test` ejecuta primero ~106 000 programas dirigidos (núcleo 64; ~47 000 con núcleo 32) y luego los aleatorios; opciones `link=N`, `core32`, `core64`, `directed`, `nodirected`. El intérprete de upstream no implementa `BLTZALL`, `LLD`, `SCD` (`UnknownOpcode`): no se comparan |

## Pruebas automáticas

Se construyen con `ninja <destino>` (todos `EXCLUDE_FROM_ALL`) y se ejecutan con datos aislados
(XDG, directorio de ejecución y bus de sesión propios; nada del usuario):

| Prueba | Qué comprueba | Cómo |
|---|---|---|
| `lifecycle-test`, `debugger-test`, `script-test`, `mainwindow-test`, `gliden64-test`, `common-test` | ciclo de ventana/ROM/plugins, ventanas del depurador, motor de scripts, ventana principal (barra de estado, posición/tamaño, auto sleep, pantalla completa, soltar archivos, menú System, Discord con un servidor que hace de cliente, ROM en `.7z` con `MAINWINDOW_TEST_7Z`), capa de hilos | `Scripts/linux/run-gui-tests.sh <build> <rom>` (Xvfb, ROM real) |
| `mainmenu-test`, `rombrowser-test`, `settings-test write/read`, `plugin-config-test video\|audio\|input write/read` (vídeo: prep + los 48 controles de todas las páginas cambiados y releídos), `input-capture-test write/read`, `input-e2e-test` (en `run-gui-tests.sh`), ronda completa del diálogo de GLideN64 (`GLIDEN64_TEST_CONFIGRT=1`, 67 controles, perfil del juego), `plugins-test`, `common-test` | menú, navegador de ROMs (menú contextual, vigilancia del directorio, teclado), páginas de Settings, diálogos de plugins, carga de plugins | `Scripts/linux/run-offscreen-tests.sh <build> <rom>` (Qt offscreen; la ROM debe ser la que da el CRC 635A2BFF) |

Última ejecución completa: todas en PASS (`run-gui-tests.sh` con la ROM de uso diario y
`run-offscreen-tests.sh` con `ROMS-PARA-PRUEBAS/test.z64`).

## Scripts del depurador (`Project64-debugger-scripts`)

Motor JavaScript (duktape) de upstream: `ScriptSystem`, `ScriptInstance`, `ScriptWorker`,
`ScriptAPI/*` y la capa de sockets/procesos/temporizadores compilados **sin reestructurar**; las
dependencias de `<windows.h>` se cubren con `Source/Project64-gui/Debugger/ScriptShim/` (solo lo
ve ese target) y las divergencias de los archivos de upstream son `#ifdef _WIN32` literal /
`#else` mínimo o correcciones de defectos de upstream (ver `PROJECT64_PARITY_FILES.md`).

Integración (todo real, sin stubs): `CDebuggerUI` crea/destruye el `CScriptSystem` y llama a los
hooks de upstream (estado de emulación, CPU exec/read/write, PIF, tarea RSP, DMA PI, ratón del
área de vídeo, autorun al arrancar); ventana **Scripts** (lista con estados `-`/`*`/`>` y colores,
Run/Stop, consola con copiar/borrar, entrada con historial, menú contextual Run/Stop/Edit/Autorun,
carpeta vigilada) y diálogo **Autorun scripts** (persistido en `Debugger_AutorunScripts`, formato
`a|b|c` de upstream). Los scripts viven en `$XDG_DATA_HOME/project64/Scripts` (los de la
instalación se copian una vez); `alert()` es modal; `exec("start x")` → `xdg-open`.

Pruebas (Xvfb, ROM real, scripts temporales en los XDG aislados): `script-test` — ventana desde
el menú; ejecutar/parar/repetir; consola, errores JS (excepción, sintaxis, archivo inexistente);
entrada de consola, `console.listen`, historial; temporizadores (`setInterval/Timeout/clear*`);
`script.timeout`; `fs`/`exec`/`pj64.*Directory`; sockets (`Server`/`Socket`, puerto liberado al
parar, servidor inactivo sin gastar CPU, conexión colgada que se puede parar, host inexistente);
`alert()` (bloquea al script; desde un hook del ratón; al cerrar la emulación); autorun; con ROM:
estados de emulación (`events.onstatechange`: LOADED_ROM/STARTED/PAUSED/RESUMED/RESETTING/RESET/
DEBUG_PAUSED/DEBUG_RESUMED/STOPPED), `mem`/`cpu`/`pj64.romInfo`, `events.onexec/onread/onwrite`
sobre un rango, ratón, ciclos de ROM con scripts activos, `pj64.pause/resume/reset/close/open`,
ciclo de vida de la ventana (consola sin duplicados), hilos y descriptores devueltos, cierre con
ROM + callbacks + temporizadores + servidor + socket + hijo de `exec` + descendiente en segundo
plano + `alert` abierto. `common-test`: `SyncEvent(false)` se rearma tras liberar una espera y
`pthread_cancel` (`abi::__forced_unwind`) no es absorbido por `catch (...)` (con mutación
comprobada: sin el `rethrow` el proceso aborta). `debugger-test`: Dump/Search con el juego
detenido no dejan el juego en pausa tras Go.

Defectos encontrados y corregidos (los de upstream se marcan «upstream»):
`va_list` reutilizado en `ConsoleLog/ConsolePrint` (crash en x86-64 Linux); `clearInterval/
clearTimeout` liberaban el objeto mientras el worker encolaba `remove` sobre él (upstream, use
after free); `pj64.open()` enviaba una cadena de duktape que la ventana libera (upstream);
`m_StopsIssued` sin candado común (upstream); `freeaddrinfo` de un puntero sin inicializar
(upstream); `select()` de Linux sobrescribe el `timeval` que los workers reutilizan (un servidor
inactivo giraba al 100 %: 1,50 s de CPU en 1,5 s → 0,01 s); servidores sin `SO_REUSEADDR`;
sockets heredados por los hijos de `exec` y `SIGPIPE`; `alert()` con la emulación cerrándose
(interbloqueo con el candado de callbacks); `libpng 1.4.7` del árbol sustituía al `libpng` de Qt
(símbolos ocultos); `connect()` sin poder interrumpirse al parar; descendientes de `exec("x &")`
sobrevivían al cierre.

Límites conocidos (no divergencias nuevas): `Socket.end()` sin argumentos lanza (upstream lo
declara opcional); `exec()` lee stdout hasta EOF antes de stderr (upstream); `getaddrinfo` es
bloqueante y no interrumpible; `select` de los workers admite descriptores < `FD_SETSIZE` (por
encima falla con error en lugar de corromper memoria); la caché de rangos de callbacks de CPU
se lee sin candado (upstream, «por velocidad»). Pendiente de comprobación **manual** (no
automatizable aquí): ratón real sobre el vídeo incrustado de SDL (la prueba usa `QMouseEvent`
sintéticos), «Edit» y el botón «...» (abren con `xdg-open`/`QDesktopServices`) y `alert()` con un
gestor de ventanas real.

## Ventana principal (`CMainGui` → `MainWindow`)

El auditado de `Source/Project64/UserInterface/MainWindow.cpp`/`Notification.cpp` contra la
ventana Qt encontró comportamientos de upstream que faltaban (ajustes que existían en Options
pero sin efecto). Ahora portados y probados con ROM real (`mainwindow-test`, Xvfb):

| Upstream | Qt |
|---|---|
| Barra de estado de dos paneles (`SetStatusText`, segundo de 135 px), regla de tiempo de `DisplayMessage` (`m_NextMsg`), `DisplayMessage2`, texto del juego en pantalla completa por `DrawStatus` del plugin | **L** `QtNotification::DisplayMessage/2`, `MainWindow::ShowStatusMessage/2` (se eliminaron los textos propios «No ROM loaded»/«Running:») |
| Callbacks de ajustes: `GameRunning_CPU_Running` (AlwaysOnTop, ocultar el navegador, `Setting_AutoFullscreen` con `<estado>.AutoFullScreen` del RDB, `BringToTop`; al terminar: modo ventana, navegador, sin AlwaysOnTop, paneles vacíos), `GameRunning_CPU_Paused`, `GameRunning_LoadingInProgress` (una carga fallida devuelve el navegador), `Game_File` (ROM reciente + título «`GoodName` - `Setting_ApplicationName`»), `UserInterface_ShowStatusBar` (solo sin juego), `RomBrowser_Enabled` | **L** `MainWindow::RegisterSettingCallbacks` (sustituye el sondeo de 250 ms `CheckEmulationEnded`) |
| Posición y tamaño: `UserInterface_MainWindowTop/Left`, `RomBrowser_Top/Left/Width/Height/Maximized`, guardado 1 s después de mover (`Timer_SetWindowPos`), ventana fija de 640×480 con juego y redimensionable con el navegador, centrado por defecto | **L** `EnterBrowserMode/EnterGameMode`, `moveEvent/resizeEvent` |
| `Setting_AutoSleep` (`WM_SETFOCUS/KILLFOCUS/ACTIVATEAPP` → `SysEvent_PauseCPU_AppLostFocus/AppLostActive`, reanudación solo del mismo tipo de pausa, `WM_KillFocus` del plugin de control), `UserInterface_ExitFullscreenOnLoseFocus` | **L** `WindowFocusChanged`, `ApplicationActiveChanged` (activación de Qt) |
| `SC_SCREENSAVE/SC_MONITORPOWER` con `Setting_DisableScrSaver` mientras corre y no está en pausa | **L** equivalente Linux: `ScreenSaverInhibitor` pide al escritorio por D-Bus (`org.freedesktop.ScreenSaver`, `org.gnome.SessionManager`) no entrar en reposo; comprobado contra el servicio real de la sesión |
| `CNotificationImp::WindowMode()` (salir de pantalla completa antes de errores/avisos/preguntas) | **L** `MainWindow::LeaveFullScreen` |
| `WM_DROPFILES` | **L** `MainWindow::DropFiles` (ROM o imagen de disco) |
| Menú contextual del navegador de ROMs | **L** (ver tabla anterior) |
| `CRomBrowser::WatchThreadStart/Stop` + `RomDirNeedsRefresh`/`GetRomFileNames` (el directorio de ROMs se vigila mientras se ve el navegador, y se comprueba al empezar; la lista se rehace si los nombres ya no dan el MD5 de la caché) y `RomBrowserListChanged` (`RomList_GameDirRecursive`, `RomList_ShowFileExtensions`) | **L** `QtRomBrowser::WatchStart/WatchStop/RomDirNeedsRefresh` (`QFileSystemWatcher`, comprobación en un hilo aparte, reintento mientras `CRomList` ignora la petición); `rombrowser-test` (alta y baja de ROMs, parado, reanudado). Se arregló además un interbloqueo al destruir el navegador con un refresco en curso (el hilo del refresco espera al hilo GUI con `BlockingQueuedConnection`) |
| ROMs dentro de archivos `.7z` (`C7zip`: listado en el navegador con su caché y carga de `archivo.7z?fichero`), que upstream activa solo en `_WIN32` | **L** `PJ64_HAVE_7ZIP` (Windows y Linux; el decodificador y su capa portable ya compilaban); comprobado: el navegador lista el ROM del `.7z` y `mainwindow-test` (con `MAINWINDOW_TEST_7Z=<archivo>?<fichero>`) lo ejecuta |
| `CNotificationImp::FatalError` (`ExitThread` tras el mensaje) | **L** el mensaje se muestra y el hilo llamante termina (`pthread_exit`; el envoltorio de `CThread` lo marca como parado); `mainwindow-test` |
| `WM_HIDE_CUROSR` (lo envía el plugin de vídeo), iconos de bandeja/ventana, `SetWindowPos` topmost por Win32 (aquí `_NET_WM_STATE_ABOVE`) | **E/B** el plugin SDL gestiona su cursor; ya existe `MakeWindowOnTop` |

Pendiente de comprobación **manual** (Xvfb no tiene gestor de ventanas): activación/foco reales
con el escritorio (p. ej. que abrir un menú no cuente como pérdida de foco), pantalla completa
real, «always on top» y que el escritorio no apague la pantalla durante el juego.

## Matriz de archivos (resuelta)

`PROJECT64_PARITY_FILES.md` lista los 272 archivos de `Source/` que difieren de
upstream con clase y razón; se regenera con `Scripts/linux/parity_files.sh`
a partir de `Scripts/linux/parity_classes.tsv`. Ninguno queda sin clasificar.
Cada regla se escribió leyendo el diff real. Medida actual contra
`upstream/develop`: 272 archivos, +29558/−765; sin la GUI Qt, el CLI y los
`CMakeLists.txt` (código nuevo sin equivalente upstream) son 162 archivos,
+4654/−765: `#include` de `\` a `/`, includes estándar que GCC exige, ramas
`#ifdef _WIN32`/Linux y los archivos del motor de scripts (ver «Scripts del depurador»).

Divergencias accidentales (clase D) eliminadas en esta pasada:

- `Project64-video/TexCache.cpp`: `printf("[TR_DUMP]...")` de la investigación
  de texturas; el archivo vuelve a ser idéntico a upstream.
- `Project64-core/Plugins/Plugin.cpp`: se había quitado
  `m_Audio->Close(m_MainWindow)` de `DestroyAudioPlugin`; restaurado (idéntico
  a upstream). `CAudioPlugin::Close` ahora detiene antes el hilo de audio.
- `Project64-core/Plugins/AudioPlugin.cpp`: el hilo de audio llamaba
  `AiUpdate(false)+Sleep(1)` también en Windows; ahora Windows conserva
  `AiUpdate(true)` y solo Linux sondea (el driver SDL no tiene evento de espera).
- `Project64-rsp-core/Recompiler/RspCodeBlock.cpp`: `std::find` → `.find()`
  innecesario y ruido de fin de línea; ahora solo añade `#include <algorithm>`.
- Basura de fin de archivo (`\r` final) en `ucode00/05/06/09.cpp`, `rdp.cpp`,
  `Main.cpp`, `OGLgeometry.cpp`, `OGLglitchmain.cpp`.

Pendiente de decidir (clase D conservada): `ScreenResolution.cpp` añade
"1980p" (1920×1080) en mitad de la tabla, lo que desplaza los índices de las
dos resoluciones siguientes; se mantiene porque el usuario la tiene guardada.

## Auditoría de los opcodes propios del JIT

`SPECIAL_DIVU` y `SPECIAL_DMULT` existen porque upstream los deja como
`BreakPoint`; se conservan y se corrigieron (commit `jit: fix SPECIAL_DIVU...`):

- `DIVU`: no protegía eax/edx (un registro MIPS cacheado en ellos se
  corrompía; la causa está en `CX64RegInfo::Map_TempReg`, ver más abajo), un divisor 0 provocaba `#DE` en el host (el intérprete
  devuelve LO=−1, HI=sext(RS)) y LO/HI no se extendían con signo
  (`(int32_t)(rs/rt)`). Ahora replica `R4300iOp::SPECIAL_DIVU`.
- `DMULT`: mismo problema de rax/rdx; ahora usa el patrón de `DMULTU`.
- `JumpToSection`/`JumpToUnknown` solo emiten un salto a una etiqueta y son
  correctas; upstream las deja vacías porque su recompilador x64 no genera
  secciones enlazadas aún.

Segunda revisión (Codex, solo lectura, verificada a mano contra el código): no
hay ninguna violación de System V AMD64 pendiente (argumentos, `this`, pila,
registros preservados, retorno, vtable de `MemoryHandler`, flags). Sí encontró
un fallo del register cache que no es de ABI: `Map_TempReg` con un registro
explícito (`SLLV/SRLV/SRAV`, `MULTU`, `DMULTU`, `DDIVU`, ...) no volcaba un
registro MIPS ya cacheado en ese registro físico. Corregido en un único punto
(`x64RegInfo.cpp`, `UnMap_X64reg` antes de tomar el registro; clase **D**
respecto a upstream, que comparte el defecto). Los `sub/add rsp,32` que
quedan en Linux son espacio sobrante inocuo, no shadow space necesario.

Hallazgo: el recompilador x64 de upstream no puede ejecutar todavía un juego
(SM64 se detiene en `COP0_CO_TLBP`, un `BreakPoint`). Vortex64 tiene 99
manejadores stub frente a 102 de upstream. Comparar intérprete y JIT
sobre una ROM real no es posible hasta que existan los ~100 opcodes
restantes; eso es trabajo nuevo, no un port, y no se ha emprendido.

## Registro de decisiones por divergencia

Formato: upstream · diferencia · razón Linux · prueba. El detalle por archivo
está en `PROJECT64_PARITY_FILES.md`.


## Hallazgos de esta sesión (verificados con la GUI real bajo Xvfb)

- **Corrupción de texturas 2D (causa raíz):** `loadBlock` (`rdp.cpp`) hace
  `v16 += dxt; if (v16 < 0)` sobre un `int` y depende de que el desbordamiento
  se envuelva. MSVC lo hace; GCC -O3 lo trata como indefinido y rompía el
  intercambio de filas impares de toda textura cargada con LoadBlock (HUD,
  "PRESS START", fondos). Clase **B**: `-fwrapv` en `Project64-video`
  (`CMakeLists.txt`). Comprobado con volcados de textura y capturas.
- **Datos que faltaban en el despliegue:** `Lang/`, `Config/Cheats`,
  `Config/Enhancements` y `Project64.rdx` no se copiaban ni se instalaban
  (menú de idiomas, cheats de serie y enhancements salían vacíos). Corregido
  en el `CMakeLists.txt` raíz.
- **Rutas escribibles:** `Project64.sc3`, las cachés de la lista de ROMs y
  `Cheats-User`/`Enhancements-User` van a XDG.
- **Entrada:** el teclado llega al juego (Return pasa de la pantalla de título de
  SM64); verificado con `xdotool` bajo Xvfb.
- **Pendiente de prueba manual:** pantalla completa, joysticks reales, audio
  audible y el cierre natural de la GUI con ROM.

### Segfault 139 de run-gui-tests.sh (resuelto)

Causa (gdb, `PifRamHandler::DMA_READ`, hilo de emulación, no el cierre): `Debugger_Enabled` queda guardado en
la configuración compartida por debugger-test; gliden64-test (sin `CDebuggerUI`) entonces tenía
`haveDebugger` verdadero y `g_Debugger` nulo, y `DMA_READ` llamaba `g_Debugger->PIFReadStarted()`.
Corregido con la comprobación `g_Debugger != nullptr` (como el resto de sitios del núcleo) en `PifRamHandler`
y `R4300iOp::UnknownOpcode`. Además `exit codes:` ya muestra el código de cada test (el de lifecycle era el
acumulado). Los tests están `EXCLUDE_FROM_ALL`: hay que compilarlos explícitamente.

### Auditoría de Settings (2026-10-01)

- Los `SettingID` que enlazan las páginas de upstream (`Source/Project64/UserInterface/Settings/*.cpp`) y las del port Qt coinciden
  (83 frente a 82; solo falta `Cfg_GoodName`, que upstream usa para el título del diálogo). Sin controles ficticios.
- Barrido `settings-test sweep-write` / `sweep-read` (`run-offscreen-tests.sh`): para cada control enlazado de cada página (12 páginas:
  opciones, avanzadas, valores por defecto, plugins globales y de juego, directorios, disco, general/recompilador/disco/estado del juego,
  navegador) se cambia desde la UI, se aplica, se comprueba que el `SettingID` tiene el valor del control, y tras reiniciar el proceso
  que la UI y el ajuste lo conservan: 64 controles. No se barren (6) los deshabilitados o con un solo elemento, ni los 4 que piden
  confirmación con un diálogo (`HleGfx`/`HleAudio`, global y de juego), que se prueban a mano.
- El barrido encontró un defecto de upstream (también en `SettingsType-RDB.cpp` de upstream): `CSettingTypeRDB::Save` escribía `Yes`/`No` y `Load`
  lee un número (`%u`), de modo que con el editor de RDB activo «Audio Signal», «FPU Reg Cache»... guardados como activados se leían
  como desactivados. Ahora se guarda `1`/`0`, como el resto del RDB (**L**, defecto corregido).
- Efecto real: los `SettingID` sin consumidor directo en el núcleo son los de valores por defecto (`Default_*`, leídos por la cadena de
  `CSettingTypeRDB(..., Default_*)`) y los `Directory_*Selected/UseSelected` (leídos por `CSettingTypeSelectedDirectory`), igual que upstream.

### Empaquetado / instalación de solo lectura

- `cmake --install` instala `bin/project64{,-gui}`, `lib64/project64/` (plugins, `libGLideN64.so`, `*-qtconfig.so`, `Config/`, `Lang/`,
  `Scripts/`, `GLideN64*.ini`), `.desktop`, icono y MIME (verificado: 18 MB).
- `Scripts/linux/test-readonly-install.sh <build> <rom>`: instala en un prefijo temporal, lo deja de solo lectura (`chmod -R a-w`), arranca
  `project64-gui` con la ROM y XDG propios, y comprueba que corre, que el prefijo no cambia (hashes) y que la configuración va a
  `XDG_CONFIG_HOME`. **PASS**. Los ajustes, el editor RDB (copia de usuario) y los scripts del usuario ya usan XDG.
- AppImage: `Vortex64.AppDir/` (sin versionar) es un artefacto antiguo sin GLideN64 ni `Config`/`Lang`; no hay `appimagetool`/`linuxdeploy`
  en el entorno (habría que descargarlos), así que **no se genera un AppImage**. Pendiente para quien lo empaquete: copiar el
  resultado de `cmake --install --prefix AppDir/usr`, el plugin `xcb` de Qt y SDL2; el programa localiza plugins y datos relativos
  al ejecutable y no necesita que el prefijo sea escribible.

### ROM probadas con el recompilador y con GLideN64 (2026-10-01)

Solo se dispone localmente de cuatro ROM distintas (md5 de los 12 primeros): `20b854b23920` SM64 (U), `55901681e4bc` SM64 (E),
`85d61f5525af` SM64 (J) y `6a0faa943d2e` (hack de 96 MB «ULTRA 64 MARIO BROS», no está en el RDB: RDRAM «desconocida»).
Todas son de la misma familia (SM64); no hay otro juego local ni se descargó ninguno.

| Prueba | U | E | J | 96 MB |
|---|---|---|---|---|
| `lifecycle-test` con JIT, 2 ciclos: 120 s en marcha sin diálogos, pausa/reanudar, estado guardar/cargar, reset, cambio de plugin de audio, fin y reapertura, cierre de ventana | PASS | PASS | PASS | PASS |
| `gliden64-test` (captura, 800×600, pausa, estado, reset, ajustes, cambio de plugin de vídeo y vuelta, cierre) | PASS | PASS | PASS | PASS |

Además `gliden64-test` con vídeo en hilo (`GLIDEN64_TEST_THREADED=1`) y con JIT (`GLIDEN64_TEST_JIT=1`) con la ROM de la suite: PASS.
Sin verificar (requiere ROM/hardware que no hay): HUD/texrect/profundidad con otros juegos, pantalla completa real, HiRes textures,
framebuffer emulation en juegos que lo exigen.

## Auditoría de calidad (2026-10-01, tras el tag `vortex64-pre-manual-test-5ba685845`)

### Pantalla completa y escalado

Diagnóstico con el KWin/XWayland real del usuario (dos monitores: 1920×1080 y 1080×1920 girado) capturando la pantalla con
`spectacle` mientras `gliden64-test` alternaba ventana ↔ pantalla completa:

- **GLideN64**: correcto (la resolución interna sigue al widget: 1920×1080 nítido, 4:3 con bandas).
- **Project64-video (Glide64)**, el plugin que el usuario tiene seleccionado: **roto**. El wrapper dibujaba un bloque de
  `g_width×g_height` (640×480) en la esquina y dejaba el resto negro (y duplicado). Corregido en `OGLglitchmain.cpp`:
  si el drawable (leído con `glXQueryDrawable`, SDL no ve los cambios de un widget ajeno) es de otro tamaño, el `swap` copia el
  bloque a una textura y lo dibuja escalado con su aspect ratio (`PJ64_PresentScaled`, estado de OpenGL guardado y restaurado);
  si el tamaño se mantiene 20 fotogramas, el plugin se reabre a ese tamaño (`g_auto_request_*` → `ChangeSize`/`to_fullscreen`, el
  mismo camino que el cambio a pantalla completa de upstream), de modo que en pantalla completa se renderiza a la resolución de la
  pantalla (nítido) y al volver a ventana vuelve a la resolución de los ajustes. La lista «Full screen resolution» del diálogo del plugin
  estaba vacía (el índice apuntaba a una tabla vacía): ahora tiene la entrada «Native: the size of the screen» (sin elección posible:
  el fullscreen es el de la GUI).
- La ventana de juego volvía a 640×480 al salir de pantalla completa aunque el plugin hubiera pedido otro tamaño: `MainWindow::m_RenderSize`.
- Cuando el hilo de emulación era cancelado (cambio de plugin, cierre) dentro de `RefreshScreen()`, `SystemTiming.cpp` tragaba el
  `__forced_unwind` con `catch(...)` y el proceso abortaba (`FATAL: exception not rethrown`, ~25 % de las ejecuciones en el escritorio
  real): ahora se relanza (también en `OGLglitchmain.cpp`).
- Pruebas: `gliden64-test` con `GLIDEN64_TEST_FULLSCREEN=<n>` (tamaños de pantalla/ventana/widget/captura en cada estado) y
  `GLIDEN64_TEST_PLUGIN=<lib>`; en Xvfb con `Scripts/linux/minimal-wm.c` (WM mínimo que respeta `_NET_WM_STATE_FULLSCREEN`).

### Pausa/reanudación (carrera)

`CN64System::Pause()` ejecutaba `m_hPauseEvent.Reset()` al empezar: una reanudación que llegaba entre la decisión de pausar y la
entrada en `Pause()` (o mientras el evento de pausa seguía en cola) se perdía y el juego quedaba pausado para siempre (reproducido
abriendo y cerrando Settings con rapidez: ~5 de cada 6 ejecuciones de la sesión de prueba). El evento se reinicia ahora al decidir la
pausa (`ResetPauseEvent`) y los `ResumeCPU_*` cancelan la pausa pendiente (`CSystemEvents::CancelEvent`).

### Valor por defecto del núcleo

`Setting_ForceInterpreterCPU` es `false` por defecto en Linux (**B/D**: upstream lo deja en `true` en x64 porque su recompilador x64
tiene `BreakPoint` stubs; el de Vortex64 está completo y se compara con el intérprete en `jit-fuzz-test` y con ROM reales).

### Sesión de usuario automatizada

`GLIDEN64_TEST_SESSION=<n>`: n veces (abrir ROM → pausa/reanudar → pantalla completa → ventana → diálogo de Settings (cambia una
opción y pulsa OK) → cambio de plugin de vídeo y vuelta → captura → guardar/cargar estado en un slot distinto → reset suave y duro →
cerrar ROM → reabrir), con ambos plugins; en la suite (`run-gui-tests.sh`).

### Ajustes

- Las páginas Advanced/Defaults aparecen al desmarcar «Hide advanced settings» (como upstream `OnBasicMode`), no solo tras Apply. Un
  usuario con Basic Mode activo (el valor por defecto de upstream) no las ve.
- Matriz Settings upstream↔Qt por `.rc` + cadenas de idioma (`settings-test dump`): sin controles ausentes salvo `IDD_Settings_ShellInt`
  (asociación de extensiones de Windows; en Linux es el tipo MIME de `project64.desktop`, **E**).
- El menú `ID_PLUGIN_MENU` (menú de depuración del RSP, un `HMENU` de Win32 del plugin RSP) no existe en Qt (**E**/**C**: depurador RSP de Windows).

### ASan/UBSan (2026-10-01)

`build-asan` (RelWithDebInfo, `-fsanitize=address,undefined`): lifecycle (intérprete y JIT), gliden64-test (sesión, ambos plugins),
debugger-test, script-test y mainwindow-test terminan con exit 0 y **0 errores de ASan**; jit-fuzz-test no muestra errores pero sale por
el límite de tiempo (ASan es ~10× más lento). Hallazgos corregidos (todos también en upstream): `unique_ptr<char>` con `new char[]` en
`CIniFileBase` (alloc-dealloc-mismatch), `delete` de `new[]` en `CTraceLog`/`TraceSetMaxModule` y en `CRDP::free` (vtx1/vtx2/cache de
Project64-video), llamada a un miembro sobre `g_System` nulo al cargar una ROM, `&vector[0]` de un vector vacío en `JSSocketWorker`. Faux
positivo de ASan en libxkbcommon-x11 (`strndup`): `intercept_strndup=0`. UBSan: solo desplazamientos de negativos, enteros fuera del
rango de `SettingID` (el valor lleva banderas), cargas sin alinear en x86 y un `memcpy(NULL,0)` de asmjit: sin consecuencias, no se tocan.

### Segunda tanda de la auditoría (2026-10-01)

Hallazgos propios (pruebas con KWin real, ASan, barrido de `#ifdef _WIN32`) y de Codex (verificados uno a uno antes de tocar nada):

| Hallazgo | Estado |
|---|---|
| Texturas HiRes de Glide64 (`TxHiResCache::loadHiResTextures` era código solo-Windows: con `ghq_hirs` activo no se cargaba ningún pack en Linux) | **L** el cargador se ejecuta en Linux (`CPath::FindFirst` ya tenía versión Linux); la suite lee un pack sintético (formato Rice) y comprueba que carga 2 texturas (`PJ64_VIDEO_REPORT`). El volcado de texturas (`ghq_hirs_dump`) no está conectado en el código de Glide64 de upstream (`ext_ghq_dmptx` no se llama): sin cambios |
| Nombre de ROM de Glide64 vacío tras reabrir el renderizador (capturas `Glide64__01.png`, pack de texturas buscado en `Textures//`) | **L** `CRDP::free()` ya no lo borra |
| `png_destroy_read_struct` en las rutas de error de escritura de PNG; `unique_ptr<uint8_t>` con `new[]` | **L** |
| Cursor visible en pantalla completa; ordenación del navegador por columna tras reordenar; Welcome aplicaba sus opciones al cerrarlo; cheats/enhancements abiertos al terminar el juego | **L** corregido |
| `Setting_CheckEmuRunning` sin efecto en Linux | **L** otra copia de `project64-gui` se termina (tras preguntar) como en Windows; fichero de pid en `XDG_RUNTIME_DIR`, solo mata si `/proc/<pid>/exe` es el mismo ejecutable |
| Volumen del plugin de audio sin efecto; reinicio del buffer circular con el callback ya en marcha; mandos conectados en caliente; zona muerta 100 % | **L** corregido |
| Cambiar el directorio de plugins no recargaba plugins con los mismos nombres | **L** corregido (también en upstream) |
| `CThread::m_threadID` sin sincronizar | **L** atómico en Linux |
| Plugins ausentes o que no son plugins | **L** error y recuperación comprobados (`LIFECYCLE_BADPLUGIN`) |
| Wayland nativo (`QT_QPA_PLATFORM=wayland`) | **C** el vídeo de las dos plugins se incrusta con `winId()` de X11; con Wayland nativo el plugin abre su propia ventana y el pantalla completa de la GUI no la alcanza. Por defecto la GUI fuerza `xcb` (XWayland), que es lo probado; HiDPI (`QT_SCALE_FACTOR=2`) comprobado con ambos plugins |
| Rendimiento | navegador en reposo 0,1 % CPU; Glide64 con juego ≈32 % de un núcleo y GLideN64 (rasterizador software de Xvfb) varios núcleos: sin bucles activos evidentes |
| Código de upstream con ramas Linux vacías (`#ifdef _WIN32` sin `#else` de más de 6 líneas en núcleo y plugins) | revisado: HiRes (arreglado), configuración Win32 de los plugins (sustituida por Qt), depurador del RSP y diálogos Win32 del RSP (**E**), `DllMain` (no aplica) |

### Tercera tanda: estrés y estados raros (2026-10-01)

Nuevas pruebas en `run-gui-tests.sh`: arranque y fin inmediatos repetidos (`LIFECYCLE_STRESS`, intérprete y JIT), «Start Emulation» tras terminar
(`LIFECYCLE_RESTART`), plugins ausentes o falsos (`LIFECYCLE_BADPLUGIN`), sesión de usuario y modo caos con operaciones aleatorias rápidas
(`GLIDEN64_TEST_SESSION`, `GLIDEN64_TEST_CHAOS`, semillas por `GLIDEN64_TEST_SEED`), pantalla completa ×3, capturas de ambos plugins y un pack
HiRes sintético. Defectos que salieron (todos reproducidos antes de corregirse):

| Defecto | Corrección |
|---|---|
| Un fin de emulación pedido antes de que el hilo de emulación llegue a `ExecuteCPU` se perdía (el hilo ponía `m_EndEmulation=false` al empezar, y `Recompiler::Run` otra vez) y la emulación seguía hasta el tiempo límite de `CloseCpu`; al borrar luego el sistema el hilo y el callback de audio usaban memoria liberada (SIGSEGV/ASan use-after-free) | `m_CloseRequested` (persistente hasta un nuevo `StartEmulation`), `m_thread` fijado antes de arrancar el hilo, sin reinicio en `Recompiler::Run` |
| El hilo de emulación esperaba para siempre en `AI_LenChanged` a que un dispositivo de audio que no estaba en marcha vaciara el buffer (sin comprobar el fin de emulación) | espera con tope de 500 ms y solo si el dispositivo suena |
| Hotplug de mandos en Linux sin efecto, y tras un cambio de dispositivos los botones mapeados apuntaban a nodos borrados | eventos SDL → `TakeHotPlug` → `RemapDevices` |
| Una textura de presentación del plugin de vídeo sobrevivía a la destrucción del contexto GL | se reinicia con el contexto |
| Instancia única: carrera entre dos copias lanzadas a la vez | `flock` sobre el fichero de pid |

Sanitizers: ver «ASan/UBSan». TSan no se ha usado (el núcleo tiene condiciones de carrera benignas deliberadas y Qt/SDL generan ruido que impide separar hallazgos reales): sustituido por los estrés anteriores y la revisión de Codex.


## Cierre del port base (2026-10-02)

Auditorías detalladas: `PROJECT64_VIDEO_OPTIONS.md` (Glide64 control por control, GLideN64, Settings 64/64 enlaces).
Hallazgos corregidos en esta fase (propios y de la auditoría final de Codex, verificados a mano contra el código):

| Hallazgo | Estado |
|---|---|
| Un mando enchufado con el diálogo de Input abierto y sin juego en marcha no aparecía (la reasignación de dispositivos solo ocurría en `GetKeys`) | **L** corregido (`ScanDevices` procesa el hot plug); lo encontró el nuevo test de mando virtual |
| La lista de dispositivos de Input se reconstruía sin `m_DeviceCS` (hot plug frente a sondeo/escaneo) y `StartScanDevices` sondeaba sin `m_CS` (Codex) | **L** corregido |
| Glide64 «Autodetect VRAM / tamaño de VRAM» guardaba el valor pero no hacía nada en Linux (`TMU_SIZE` solo en `_WIN32`) (Codex) | **L** corregido |
| Glide64 «Vertical sync» sin efecto (upstream tampoco lo aplicaba en el wrapper) | **L** `SDL_GL_SetSwapInterval`; en GLideN64 se aplica en vivo |
| Cambios de la hoja de Glide64 con el juego abierto no se aplicaban hasta la siguiente ROM (Codex) | **L** el hilo de emulación recrea el renderer tras el diálogo, como `DllConfig` de upstream; probado con la hoja abierta 3 veces |
| Tooltips de 10 controles de la página de texturas sin enlazar | **L** corregido |
| GLideN64 «Threaded video» no reconfigura una sesión en marcha (Codex) | **D justificada**: upstream tampoco; se aplica al arrancar el plugin |
| GLideN64 tamaño en ventana ignorado en una ventana SDL propia ya abierta (Codex) | **E**: solo existe la ventana SDL propia sin frontend Qt (consola); el frontend siempre incrusta el widget |
| Sincronización del diálogo de Glide64 con `g_ProcessDListCS` (Codex) | **D justificada**: el diálogo solo escribe valores escalares; las operaciones que tocan el contexto GL ya van al hilo de emulación |
| Presets «Best performance/Best texture quality» de Glide64 | falso positivo: upstream no tiene manejadores |
| El diálogo de GLideN64 sin las opciones de pantalla completa/borderless/monitor/refresco | **E** el frontend es dueño de la ventana (igual que el plugin de mupen64plus) |

### Sanitizers (cierre del port, `build-asan`, RelWithDebInfo con `-fsanitize=address,undefined`)

`run-offscreen-tests.sh` y `run-gui-tests.sh` (lifecycle con intérprete y JIT, depurador, scripts, ventana principal,
GLideN64, ambos plugins en pantalla completa, Input end to end, audio): sin errores de ASan (memoria) en ninguna. UBSan
solo informa de aritmética de enteros del emulador y de un cast de enumeración, sin efecto en GCC/x86-64:

| Aviso de UBSan | Dónde | Veredicto |
|---|---|---|
| desplazamiento a la izquierda de un valor negativo, desbordamiento de `int` con signo (`2147483647 + 1`, `0 - INT_MIN`) | `InterpreterOps.cpp`, `R4300iInstruction.cpp`, `x64RecompilerOps.cpp` | **D** semántica de complemento a dos del R4300i (la misma que el JIT), código de upstream; definida en GCC; las comparaciones intérprete/JIT (`jit-fuzz-test`, 0 diferencias) la ejercitan |
| carga de un `SettingID` fuera del rango declarado de la enumeración (valores de ajustes de plugins, 0x200000+) | `SettingsType-RDBUser.cpp`, `Settings.cpp` | **D** upstream usa la enumeración como número de ajuste abierto |
| carga de `uint64_t` no alineada | `CodeBlock.cpp` | **D** x86-64 la admite; código de upstream |
| `memcpy` con puntero nulo y longitud 0 | `3rdParty/asmjit` | **E** biblioteca externa, inocuo |

El fuzz de ASan en el modo más lento (`link=2 core32`) tarda ~8 min con el equipo libre y 0 diferencias; dentro de la suite
completa, ejecutada junto a otros trabajos, puede pasar del timeout de 900 s del script (código 137 por `timeout -s KILL`,
no un cuelgue): conviene ejecutar la suite de ASan sin otra carga.
