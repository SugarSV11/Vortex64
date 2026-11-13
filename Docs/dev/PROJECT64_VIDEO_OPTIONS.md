# Auditoría de opciones de vídeo y Settings (cierre del port)

Comparación control por control contra upstream (`project64-upstream-reference`). Fuente de cada tabla: lectura del
código de ambos lados; la parte de Settings está comprobada de forma mecánica (los `AddMod*` de upstream frente a los
de `Source/Vortex64-gui/Settings`).

## 1. Project64 Video (Glide64): `Source/Vortex64-video/ConfigQt.cpp` frente a `Config.cpp` de upstream

Los controles de upstream (`Vortex64-video.rc`, 4 páginas) están todos, con el mismo `SettingID`, valores, orden y
dependencias. Persistencia: `g_settings->WriteSettings()` (global, o la sección del juego cuando `g_romopen`, como upstream).
Pruebas: `plugin-config-test video prep|write|read` (48 controles de todas las páginas cambiados, OK, reabiertos) y
`gliden64-test` con `GLIDEN64_TEST_GLIDE64CFG=1` (la hoja abierta con el juego en marcha, tres veces).

| Página / control upstream | Setting (setter) | Tipo y valores | Defecto | Dependencia | Efecto real en Linux |
|---|---|---|---|---|---|
| Basic › Windowed Resolution (`IDC_CMB_WINDOW_RES`) | `SetScreenRes` (`resolution`) | lista de `GetScreenResolutionName` | `GetDefaultScreenRes()` | — | tamaño del área de vídeo (`g_width/g_height`, `SetRenderSize` del frontend) |
| Basic › Vertical sync | `SetVsync` (`vsync`) | bool | sí | — | **corregido en esta fase**: `SDL_GL_SetSwapInterval` (upstream solo lo pasaba al trace) |
| Basic › Full screen resolution | `SetFullScreenRes` | una entrada (la pantalla) | índice actual | desactivado con una sola entrada | la pantalla completa es del frontend (ver divergencias) |
| Basic › Anisotropic filtering | `SetWrpAnisotropic` | bool | no | — | `OGLglitchmain` (máximo del driver) |
| Basic › Autodetect VRAM + tamaño | `SetWrpVRAM` (`wrpVRAM`, 0 = auto) | 0..2³⁰ MB | 0 | caja numérica solo sin auto | **corregido**: `TMU_SIZE` (en Linux solo estaba en el código `_WIN32`) |
| Basic › Use frame buffer objects | `SetWrpFBO` | bool | no | — | `OGLglitchmain` |
| Basic › Show texture enhancement options | `SetTexenhOptions` | bool | no | la página aparece/desaparece al aplicar | **corregido** (página dinámica como `UpdateTextureSettings`) |
| Emulation › Filtering mode | `SetFiltering` | Automatic / Bilinear / Point | Automatic | — | `TexCache.cpp` |
| Emulation › Buffer swapping | `SetSwapMode` | Old / New / Hybrid | New | — | `Main.cpp`, `rdp.cpp`, `ucodeFB.cpp` |
| Emulation › LOD calculation | `SetLODmode` | off / fast / precise | off | — | `Combine.cpp`, `Util.cpp` |
| Emulation › Aspect ratio | `SetAspectmode` | 4:3 / 16:9 / Stretch / Original | 4:3 | — | `Main.cpp` (`ChangeSize`), `ScreenResolution.cpp` |
| Emulation › Fog | `SetFog` | bool | sí | — | `Util.cpp`, `OGLgeometry.cpp` |
| Emulation › Buffer clear every frame | `SetBuffClear` | bool | sí | — | `Main.cpp` |
| Emulation › Enable frame buffer emulation + 6 del grupo (hwfbe, get info, read every frame, render as texture, detect CPU write, software depth) | `UpdateFrameBufferBits` | bits `fb_*` | por juego (RDB) | — | `rdp.cpp`, `Main.cpp`, `TexBuffer.cpp`, `ucode06.cpp` |
| Debug › 11 niveles de trazas (Settings, Unknown, Glide64, Interface, Resolution, Glitch, RDP, TLUT, PNG, OGLWrapper, RDP Commands) | `Set_Logging_*` | Error…Verbose | los de `g_ModuleLogLevel` | solo con el depurador activado | `trace.cpp` |
| Texture enhancement › Filter, Enhancement, Texture cache, Compress cache, Apply compression, Ignore Backgrounds | `SetGhqFltr/Enht/CacheSize/EnhtGz/EnhtCmpr/EnhtNobg` | listas / MB / bool | None, None, 0, sí, no, no | — | `Main.cpp` (`ext_ghq_init`), `TexCache.cpp` |
| Texture enhancement › Hi-res: Format, Alternative CRC, Dump/edit, Alpha fully, Compress cache, Apply compression, Force 16bpp, Tile | `SetGhqHirs*` | lista / bool | None, sí, no, no, sí, no, no, no | Alternative CRC desactivado con Dump (como upstream) | carga de packs (arreglada en la fase anterior: `TxHiResCache`), `TexCache.cpp`; prueba con un pack sintético |
| Texture enhancement › Texture compression method, Save cache to disk | `SetGhqCmpr`, `SetGhqCacheSave` | S3TC/FXT1, bool | S3TC, sí | — | `Main.cpp` |
| Texture enhancement › Presets «Best performance» / «Best texture quality» (`IDC_BUTTON1/2`) | — | — | — | — | **no existen en la lógica de upstream**: `Config.cpp` no tiene manejadores para esos botones (solo están en el `.rc`). No se añade un control falso |
| Tooltips de todos los controles | `ConfigTooltips.inc` generado de upstream | | | | los 10 de la página de texturas que faltaban se enlazaron en esta fase |
| About | — | | | | `DllAbout` de upstream es solo Windows (`CAboutDlg`); About de la aplicación cubre la ayuda |

Aplicación con el juego abierto: upstream (`DllConfig`, Windows) suelta el renderer, `rdp.free()/init()` y vuelve a crearlo
(`CloseConfig` → `InitGfx`) bajo `g_ProcessDListCS`. En Linux el diálogo corre en el hilo de la interfaz, que no posee el
contexto GL: pide la recreación (`RequestConfigReinit`) y el hilo de emulación la hace en el siguiente `ProcessDList`.
Verificado con la hoja abierta con el juego en marcha, tres veces, con captura después de cada una.

## 2. GLideN64: `libGLideN64.so` (submódulo sin modificar), diálogo GLideNUI

El diálogo es el `ConfigDialog` del submódulo, compilado con `M64P_GLIDENUI` solo para ese archivo (como el plugin de
mupen64plus): los hosts que son dueños de la ventana no muestran «Full screen resolution / Borderless / monitor /
refresco». Los 96 controles con nombre del `.ui` están en la tabla; los marcados «oculto» son exactamente esos.
Pruebas (`gliden64-test`, `GLIDEN64_TEST_CONFIGRT=1`): cada control activo cambiado, **OK**, reabierto (67 controles
comparados, 0 distintos); **Close** deja lo guardado intacto; **Restore Defaults** (con su confirmación) cambia los
controles al valor por defecto; **Save** lo escribe y reabierto lo muestra. Efecto real: lo hace el propio código del
plugin (`config.*`); la parte de plataforma (ventana en ventana/pantalla completa, tamaño, `threadedVideo`, vsync) es de
`Source/GLideN64-linux`: `windowedWidth/Height` → `SetRenderSize` del frontend; **Vertical sync** se aplica en vivo
(`WindowsWGL::swapBuffers`, corregido en esta fase); `threadedVideo` se aplica al arrancar el plugin (`_start`),
como en upstream (cambiarlo exige parar y arrancar el plugin: al cambiar de plugin o reabrir la ROM).

Rareza de upstream (no se toca el submódulo): `Config::resetToDefaults()` no restablece los márgenes de overscan
(solo `enableOverscan`), y la caja del factor de resolución conserva su número hasta que se elige su botón.

| Control (objectName) | Tipo | Texto | Campo `config.*` | Linux |
|---|---|---|---|---|
| `borderlessRadioButton` | QRadioButton | Borderless window | `video.borderless` | oculto (M64P_GLIDENUI: ventana propia del frontend) |
| `borderlessComboBox` | QComboBox |  | indirecto (grupo de botones o valor derivado en `accept()`) | oculto (M64P_GLIDENUI: ventana propia del frontend) |
| `fullScreenRadioButton` | QRadioButton | Full screen resolution: | `video.borderless` | oculto (M64P_GLIDENUI: ventana propia del frontend) |
| `fullScreenResolutionComboBox` | QComboBox |  | `video.fullscreenHeight`, `video.fullscreenWidth` | oculto (M64P_GLIDENUI: ventana propia del frontend) |
| `fullScreenRefreshRateComboBox` | QComboBox |  | `video.fullscreenRefresh` | oculto (M64P_GLIDENUI: ventana propia del frontend) |
| `windowedResolutionComboBox` | QComboBox |  | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `aspectComboBox` | QComboBox |  | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `vSyncCheckBox` | QCheckBox | Enable VSync | `video.verticalSync` | visible |
| `threadedVideoCheckBox` | QCheckBox | Enable threaded video | `video.threadedVideo` | visible |
| `overscanCheckBox` | QCheckBox | Overscan | `frameBufferEmulation.enableOverscan` | visible |
| `overscanNtscLeftSpinBox` | QSpinBox |  | `frameBufferEmulation.overscanNTSC.left` | visible |
| `overscanNtscRightSpinBox` | QSpinBox |  | `frameBufferEmulation.overscanNTSC.right` | visible |
| `overscanNtscTopSpinBox` | QSpinBox |  | `frameBufferEmulation.overscanNTSC.top` | visible |
| `overscanNtscBottomSpinBox` | QSpinBox |  | `frameBufferEmulation.overscanNTSC.bottom` | visible |
| `overscanPalBottomSpinBox` | QSpinBox |  | `frameBufferEmulation.overscanPAL.bottom` | visible |
| `overscanPalRightSpinBox` | QSpinBox |  | `frameBufferEmulation.overscanPAL.right` | visible |
| `overscanPalLeftSpinBox` | QSpinBox |  | `frameBufferEmulation.overscanPAL.left` | visible |
| `overscanPalTopSpinBox` | QSpinBox |  | `frameBufferEmulation.overscanPAL.top` | visible |
| `noaaRadioButton` | QRadioButton | No anti-aliasing | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `fxaaRadioButton` | QRadioButton | Fast approximate anti-aliasing (FXAA) | `video.fxaa` | visible |
| `msaaRadioButton` | QRadioButton | Multisample anti-aliasing (MSAA): | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `aliasingSlider` | QSlider |  | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `anisotropicSlider` | QSlider |  | `texture.anisotropy` | visible |
| `blnrStandardRadioButton` | QRadioButton | Standard | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `blnr3PointRadioButton` | QRadioButton | N64-style 3 point | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `translationsComboBox` | QComboBox |  | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `customSettingsCheckBox` | QCheckBox | Use per-game settings | `generalEmulation.enableCustomSettings` | visible |
| `emulateLodCheckBox` | QCheckBox | Enable N64-style mip-mapping | `generalEmulation.enableLOD` | visible |
| `enableHWLightingCheckBox` | QCheckBox | Enable per-pixel lighting (better quality, HLE only) | `generalEmulation.enableHWLighting` | visible |
| `enableCoverageCheckBox` | QCheckBox | Enable pixel coverage calculation | `generalEmulation.enableCoverage` | visible |
| `enableInaccurateTextureCoordinatesCheckBox` | QCheckBox | Enable inaccurate texture coordinates | `generalEmulation.enableInaccurateTextureCoordinates` | visible |
| `enableShadersStorageCheckBox` | QCheckBox | Store compiled shaders for performance (recommended) | `generalEmulation.enableShadersStorage` | visible |
| `factor0xRadioButton` | QRadioButton | Same as output resolution | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `factor1xRadioButton` | QRadioButton | Original N64 resolution (most accurate) | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `factorXxRadioButton` | QRadioButton | Multiple of N64 resolution: | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `resolutionFactorSpinBox` | QSpinBox |  | `frameBufferEmulation.nativeResFactor` | visible |
| `gammaCorrectionCheckBox` | QCheckBox | Custom gamma correction level: | `gammaCorrection.force` | visible |
| `gammaLevelSpinBox` | QDoubleSpinBox |  | `gammaCorrection.force`, `gammaCorrection.level` | visible |
| `ditheringQuantizationCheckBox` | QCheckBox | Enable 5bit quantization | `generalEmulation.enableDitheringQuantization` | visible |
| `hiresNoiseDitheringCheckBox` | QCheckBox | High resolution noise dithering | `generalEmulation.enableHiresNoiseDithering` | visible |
| `ditheringPatternCheckBox` | QCheckBox | Dithering pattern on output image | `generalEmulation.enableDitheringPattern` | visible |
| `ditheringModeComboBox` | QComboBox |  | `generalEmulation.rdramImageDitheringMode` | visible |
| `nativeRes2DComboBox` | QComboBox |  | `graphics2D.enableNativeResTexrects` | visible |
| `halosRemovalCheckBox` | QCheckBox | Remove colored halos around transparent objects | `texture.enableHalosRemoval` | visible |
| `texCoordBoundsCheckBox` | QCheckBox | Enable native-res bounds for texture coordinates | `graphics2D.enableTexCoordBounds` | visible |
| `fixTexrectSmartRadioButton` | QRadioButton | For adjacent 2D elements | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `fixTexrectForceRadioButton` | QRadioButton | Always | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `fixTexrectDisableRadioButton` | QRadioButton | Never | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `bgModeOnePieceRadioButton` | QRadioButton | One piece | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `bgModeStrippedRadioButton` | QRadioButton | Stripped | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `frameBufferCheckBox` | QCheckBox | Emulate frame buffer | `frameBufferEmulation.enable` | visible |
| `copyAuxBuffersCheckBox` | QCheckBox | Copy auxiliary color buffers to N64 memory (sometimes slow, fewer game issues) | `frameBufferEmulation.copyAuxToRDRAM` | visible |
| `frameBufferSwapComboBox` | QComboBox |  | `frameBufferEmulation.bufferSwapMode` | visible |
| `fbInfoEnableCheckBox` | QCheckBox | Use emulator help to read/write frame buffers | `frameBufferEmulation.fbInfoDisabled` | visible |
| `readColorChunkCheckBox` | QCheckBox | Read color buffer in 4 KB chunks | `frameBufferEmulation.fbInfoDisabled`, `frameBufferEmulation.fbInfoReadColorChunk` | visible |
| `readDepthChunkCheckBox` | QCheckBox | Read depth buffer in 4 KB chunks | `frameBufferEmulation.fbInfoDisabled`, `frameBufferEmulation.fbInfoReadDepthChunk` | visible |
| `copyColorBufferComboBox` | QComboBox |  | `frameBufferEmulation.copyToRDRAM` | visible |
| `copyDepthBufferComboBox` | QComboBox |  | `frameBufferEmulation.copyDepthToRDRAM` | visible |
| `n64DepthCompareComboBox` | QComboBox |  | `frameBufferEmulation.N64DepthCompare` | visible |
| `forceDepthBufferClearCheckBox` | QCheckBox | Force depth buffer clear | `frameBufferEmulation.forceDepthBufferClear` | visible |
| `RenderFBCheckBox` | QCheckBox | Render N64 frame buffer to output | `frameBufferEmulation.copyFromRDRAM` | visible |
| `copyDepthToMainDepthBufferCheckBox` | QCheckBox | Copy depth to main depth buffer | `frameBufferEmulation.copyDepthToMainDepthBuffer` | visible |
| `filterComboBox` | QComboBox |  | `textureFilter.txFilterMode` | visible |
| `enhancementComboBox` | QComboBox |  | `textureFilter.txEnhancementMode` | visible |
| `deposterizeCheckBox` | QCheckBox | Decrease color banding (recommended for xBRZ enhancement) | `textureFilter.txDeposterize` | visible |
| `ignoreBackgroundsCheckBox` | QCheckBox | Disable for backgrounds | `textureFilter.txFilterIgnoreBG` | visible |
| `enhancedTexFileStorageCheckBox` | QCheckBox | Use file storage instead of memory cache | `textureFilter.txEnhancedTextureFileStorage` | visible |
| `texCachePathLineEdit` | QLineEdit |  | `textureFilter.txCachePath` | visible |
| `texPackPathLineEdit` | QLineEdit |  | `textureFilter.txPath` | visible |
| `texDumpPathLineEdit` | QLineEdit |  | `textureFilter.txDumpPath` | visible |
| `alphaChannelCheckBox` | QCheckBox | Use full transparencies (recommended for newer texture packs) | `textureFilter.txHiresFullAlphaChannel` | visible |
| `alternativeCRCCheckBox` | QCheckBox | Alternative CRC calculation (for old Rice Video packs) | `textureFilter.txHresAltCRC` | visible |
| `strongCRCCheckBox` | QCheckBox | Strong CRC calculation (when RiceCRC fails) | `textureFilter.txStrongCRC` | visible |
| `hiresTexFileStorageCheckBox` | QCheckBox | Use file storage instead of memory cache | `textureFilter.txHiresTextureFileStorage` | visible |
| `noTexFileStorageCheckBox` | QCheckBox | Use no file storage or cache (causes worse stutter, not recommended for playing games) | `textureFilter.txNoTextureFileStorage` | visible |
| `textureFilterCacheSpinBox` | QSpinBox |  | `textureFilter.txCacheSize` | visible |
| `textureFilterLimitSpinBox` | QSpinBox |  | `textureFilter.txHiresVramLimit` | visible |
| `saveTextureCacheCheckBox` | QCheckBox | Save enhanced texture cache to hard disk | `textureFilter.txSaveCache` | visible |
| `compressCacheCheckBox` | QCheckBox | Compress texture cache | `textureFilter.txCacheCompression` | visible |
| `force16bppCheckBox` | QCheckBox | Convert textures to 16 bpp | `textureFilter.txForce16bpp` | visible |
| `fpsCheckBox` | QCheckBox | Display FPS | `onScreenDisplay.fps` | visible |
| `visCheckBox` | QCheckBox | Display VI/s | `onScreenDisplay.vis` | visible |
| `percentCheckBox` | QCheckBox | Display percentage | `onScreenDisplay.percent` | visible |
| `internalResolutionCheckBox` | QCheckBox | Display internal resolution | `onScreenDisplay.internalResolution` | visible |
| `renderingResolutionCheckBox` | QCheckBox | Display rendering resolution | `onScreenDisplay.renderingResolution` | visible |
| `statisticsCheckBox` | QCheckBox | Display statistics | `onScreenDisplay.statistics` | visible |
| `fontLineEdit` | QLineEdit |  | `font.name` | visible |
| `fontSizeSpinBox` | QSpinBox |  | `font.size` | visible |
| `fontPreviewNotePlainTextEdit` | QPlainTextEdit |  | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `hotkeyListWidget` | QListWidget |  | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `dumpLowCheckBox` | QCheckBox | Dump low-level info | `debug.dumpMode` | visible |
| `dumpNormalCheckBox` | QCheckBox | Dump display list commands | `debug.dumpMode` | visible |
| `dumpDetailCheckBox` | QCheckBox | Dump detailed info (matrices, vertices, etc.) | `debug.dumpMode` | visible |
| `settingsDestGameRadioButton` | QRadioButton | This game | `generalEmulation.enableCustomSettings` | visible |
| `settingsDestProfileRadioButton` | QRadioButton | Settings profile: | indirecto (grupo de botones o valor derivado en `accept()`) | visible |
| `profilesComboBox` | QComboBox |  | indirecto (grupo de botones o valor derivado en `accept()`) | visible |

## 3. Settings: 64 enlaces control→SettingID

`AddMod*` de `Source/Vortex64-ui/UserInterface/Settings/SettingsPage-*.cpp` (65 enlaces) frente a los de
`Source/Vortex64-gui/Settings/*.cpp` (64): **idénticos**, salvo que upstream enlaza `Default_DiskSeekTiming` dos veces
(una caja y después un combo; el segundo sustituye al primero) y aquí solo el combo. Todos los `SettingID` que usan las
páginas de upstream aparecen en el código Linux. Cada control: UI → SettingID → lectura → modificación → persistencia →
reapertura la comprueba `settings-test sweep-write/sweep-read` (64 controles cambiados, aplicados y comprobados contra el
valor de la configuración; 6 no se cambian: desactivados, de un solo elemento o con confirmación). El efecto sobre el back
end de los ajustes que el núcleo lee está en `PROJECT64_PARITY.md`. «Hide advanced settings» (Basic mode) oculta y muestra
Advanced y Defaults al instante, sin Apply, repetido varias veces (`settings-test write`).
Página de upstream sin equivalente por ser de Windows: Shell Integration (`IDD_Settings_ShellInt`: asociaciones de archivo
del registro; en Linux lo cubren el `.desktop` y el tipo MIME instalados). `IDD_Settings_Accelerator` es la página
Keyboard shortcuts (portada) e `IDD_Settings_Config` el propio diálogo contenedor (`SettingsDialog`).

## 4. Audio, plugins (cierre automático)

`audio-test` (SDL, driver `disk`, sin tarjeta de sonido): la señal que entra por la interfaz AI del plugin sale con el
volumen de los ajustes (100 % y 40 %), el silencio con «Mute», tres ciclos RomOpen/RomClosed con datos en cola y la
parada final sin cuelgue ni cierre anormal. La calidad audible queda para la prueba manual.
