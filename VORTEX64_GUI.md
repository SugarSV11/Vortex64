# Vortex64: el frontend

Vortex64 es la interfaz propia del port a Linux. El **núcleo** es Project64 (CPU, JIT, intérprete, memoria, RCP,
plugins, RDB, ajustes, partidas, estados, trucos, mejoras, depurador, scripts, 64DD); lo que cambia es todo lo que el
usuario ve. Este documento describe la arquitectura, qué ventanas antiguas se sustituyeron y cuáles se conservan.

La referencia visual es el mockup de 12 pantallas (Biblioteca, Recientes, Favoritos, 64DD, Emulación, Ajustes
General / Directorios / Gráficos / Input / Plugins, Debugger, Acerca de). Las capturas de comparación salen de
`Scripts/linux/vortex-shots.sh` (ver «Cómo probarlo»).

## Arquitectura

```
Source/Project64-gui/
  main.cpp                 QApplication, tema, icono, MainWindow
  MainWindow.*             ventana única: pila [Shell | vídeo]; menús y barra de estado SOLO con un juego en marcha
  Vortex/
    Theme.*                el sistema visual (tokens + paleta + una hoja de estilos)
    Icons.*                iconos de línea (QPainter), banderas, logo del tornado
    Strings.h  Store.*     textos es/en; estado del frontend en JSON ($XDG_CONFIG_HOME/vortex64/vortex.json)
    LibraryModel.*         modelo + proxies (Juegos / Favoritos / Recientes / Discos) sobre la lista del núcleo
    Covers.*               portadas locales, caché, carga asíncrona, placeholder
    GameBrowser.*          cuadrícula de tarjetas, lista, compacta (un solo modelo y selección)
    DetailPanel.*          franja inferior con el juego seleccionado y «Iniciar»
    Sidebar.*  Shell.*     navegación, cabecera (búsqueda, vistas), páginas, menú contextual
    ShotScript.*           herramienta de QA (VORTEX_SHOTS) que automatiza capturas
  Settings/                SettingsDialog (categorías) + las páginas portadas de Project64 + VortexPages
  Debugger/DebuggerShell   «Vortex64 - Debugger»: las vistas del depurador como pestañas
  Dialogs/                 Acerca de, Bienvenida, Trucos, Mejoras, Opciones de log
  RomBrowser.*             QtRomBrowser: el escáner (CRomList) — ya no se muestra
Source/Project64-input/ConfigQt.cpp   ventana de Input (Control 1-4, Pak, Asignación / Ajustes / Prueba)
```

### Sistema visual (`Vortex/Theme`)

Un único lugar define el aspecto: tokens (`Background`, `Sidebar`, `Surface`, `SurfaceElevated`, `Hover`,
`Selected`, `Border`, `TextPrimary/Secondary/Disabled`, `Accent`, `Danger`; `CornerRadius`, `Spacing`,
`ControlHeight`, `SidebarWidth`, `HeaderHeight`), la paleta de Qt y **una** hoja de estilos generada con ellos.
Los widgets piden un rol (`SetRole(w, "title")`, propiedad `vx`) en vez de llevar estilos propios. Como es global,
también viste los diálogos de los plugins (GLideN64, Glide64, audio, input), que corren en el mismo proceso.

### La biblioteca

* El escaneo, la caché binaria (`RomList_RomListCache`) y la vigilancia del directorio siguen siendo de **CRomList**
  (el mismo parser, RDB y caché que el ROM Browser clásico). `QtRomBrowser` se conserva como escáner oculto y avisa
  con señales (`ListReset`, `RomCountChanged`, `ListLoaded`). Al abrir Vortex64 la lista sale de la caché de inmediato.
* Varias carpetas: `CRomList::SetExtraGameDirectories` (cambio mínimo en el núcleo) además de `RomList_GameDir`.
* Persistente (`Store`): favoritos, historial (última vez, tiempo jugado, lanzamientos), carpetas adicionales,
  modo de vista por página, orden, filtro, último juego seleccionado, tamaño de ventana, idioma, geometría del depurador.
* El tiempo de juego cuenta solo con la CPU en marcha y sin pausa.
* Portadas: **no se descarga nada**. Se buscan `<rom>.png|jpg|webp` junto a la ROM, `cover.*` y
  `$XDG_CONFIG_HOME/vortex64/covers/<CRC1>-<CRC2>.png | <nombre>.png`. Sin imagen, un placeholder fijo del juego.

## Ventanas del frontend

| Antes (Project64) | Ahora (Vortex64) | Estado |
|---|---|---|
| ROM Browser (tabla) | Biblioteca / Recientes / Favoritos / 64DD (`Shell`) | sustituida; la tabla ya no es accesible |
| Menús File / System / Options / Debugger / Help | Archivo / Emulación / Ver / Configuración / Herramientas / Ayuda, solo con un juego en marcha | sustituidos (mismos comandos, IDs y atajos) |
| Settings (árbol de 12 páginas) | `SettingsDialog`: General, Directorios, Gráficos, Audio, Input, RSP, Plugins, Atajos, 64DD, Avanzado | sustituida; las páginas son las mismas (mismos `SettingID`) |
| Página «ROM selection» (columnas del ROM Browser) | — | retirada de la ventana: no hay tabla que configurar (la clase se conserva por sus tests) |
| Menú Language | Ajustes → General → Idioma | movido |
| Choose ROM Directory / Refresh | Ajustes → Directorios; botón de reescaneo de la cabecera | movidos |
| Debugger (ocho ventanas sueltas) | `DebuggerShell` con una pestaña por vista | sustituida; las vistas son las mismas |
| About | Acerca de de Vortex64 (Créditos, Componentes) | sustituida |
| Welcome | (sin asistente: el primer arranque abre la ventana principal con español y GLideN64) | eliminada |
| Barra de estado de 2 paneles | Estado, mensaje, velocidad, CPU, vídeo, pausa, pantalla completa | sustituida |
| «Support Project64» | — | retirada (valida contra un servicio web de Project64 que no aplica) |
| Configuración de input | Ventana de Input de Vortex64 | sustituida |

### Ventanas que se conservan, y por qué

* **GLideN64 / Glide64 / audio**: son diálogos de sus plugins. Se abren desde Ajustes → Gráficos / Audio / Plugins
  («Configurar…»), heredan el tema global y no se reescriben.
* **Trucos, Mejoras, Información de ROM, Estado del juego, opciones de log**: ventanas de Vortex64 con el sistema
  visual común (la lógica y los datos son los del núcleo).
* **Selectores de archivo y directorio, avisos**: diálogos nativos de Qt/del sistema.

## Idiomas

Vortex64 tiene exactamente dos idiomas, **Español** y **English**, y los dos cubren toda la interfaz propia.

* **Una sola llamada** cambia el idioma de toda la aplicación: `Vortex::ApplyLanguage("es" | "en" | "auto")`
  (`Vortex/Language.h`). La guarda (`language` del almacén `vortex.json`; sin preferencia = el del sistema: español para un
  sistema en español, English para cualquier otro), cambia los textos, el archivo de idioma del núcleo (los textos de las
  páginas de Ajustes, atajos y avisos del núcleo siguen al de Vortex64), los textos estándar de Qt y avisa con
  `LanguageNotifier::Changed()`.
* **Se aplica al instante**, sin cerrar el juego ni reiniciar la emulación: `MainWindow::OnLanguageChanged` reconstruye la
  biblioteca (con su página, filtro, modo y selección, que vuelven del almacén), los menús, la barra de estado y las ventanas
  abiertas del depurador; Ajustes se vuelve a abrir en la misma página si el idioma se cambió con «Aplicar».
* **Dónde están los textos**: `Tr("Español", "English")` junto al sitio donde se usa (`Vortex/Strings.h`) y, para lo que está
  escrito en inglés (el depurador, los diálogos de trucos y de registro), `Tr("English")` con el español en una tabla,
  `Vortex/Catalog.inc`. Un texto sin entrada se ve en inglés y queda anotado (`MissingTranslations()`).
* **No se traducen**: nombres de ROMs, de plugins, GLideN64, RSP, JIT, nombres técnicos y las ventanas de plugins externos.
* **Prueba**: `language-test` (Español ⇄ English, persistencia con un proceso nuevo, selector de dos idiomas, reapertura de
  Ajustes, y una auditoría de los textos visibles de todas las ventanas: ninguno del otro idioma ni sin traducir) y
  `layout-test` (filas de la barra de Ajustes y franja inferior de la biblioteca, en los dos idiomas, a varios tamaños y,
  con `QT_SCALE_FACTOR`, a varias escalas).

## Cómo probarlo

```
toolbox run -c fedora-toolbox-42 bash -c 'cd <proyecto> && cmake -S . -B build-final -G Ninja && cmake --build build-final'
build-final/bin/vortex64                      # abre la Biblioteca
Scripts/linux/vortex-shots.sh build-final out # capturas de las pantallas con ROMs de prueba (sin juegos reales)
Scripts/linux/run-offscreen-tests.sh build-final ROMS-PARA-PRUEBAS/test.z64   # incluye vortex-test (navegación)
Scripts/linux/run-gui-tests.sh build-final ROMS-PARA-PRUEBAS/test.z64         # núcleo, JIT, plugins, depurador, scripts
```
