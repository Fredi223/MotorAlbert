# Motor Albert

Motor gráfico 3D en C++20 con OpenGL 4.6 (Core Profile) y SDL3, desarrollado para la
asignatura de Motores. Esta versión corresponde a la **Entrega 1 — Visualizador de
geometría**: carga de modelos FBX con sus texturas, drag & drop de modelos y texturas,
cámara al estilo de Unity y un editor hecho con ImGui (docking).

- **Repositorio:** https://github.com/Fredi223/MotorAlbert
- **Releases:** https://github.com/Fredi223/MotorAlbert/releases

## Integrantes

| Nombre | GitHub |
|---|---|
| Albert Frederic Bailen Ruesca | [Fredi223](https://github.com/Fredi223) |

## Cómo usar el motor

Al arrancar se carga automáticamente la **Baker House** desde la carpeta `Resources/`,
que tiene que estar junto al ejecutable. En esa carpeta también están los dos modelos
propios (`Calavera_Oro.fbx` y `Lingotes.fbx`), que se pueden cargar arrastrándolos a la
ventana.

### Cámara

Los controles de cámara solo responden con el cursor sobre la ventana **Viewport**.

| Control | Acción |
|---|---|
| Clic derecho (mantener) + mover ratón | Mirar alrededor (free look) |
| Clic derecho (mantener) + `W` `A` `S` `D` | Moverse adelante / izquierda / atrás / derecha (estilo FPS) |
| Clic derecho (mantener) + `Q` / `E` | Bajar / subir |
| `Shift` (mientras se mueve) | Duplica la velocidad de movimiento |
| Rueda del ratón | Zoom hacia el objeto seleccionado (o hacia el origen si no hay selección) |
| `Alt` + clic izquierdo (mantener) + mover ratón | Orbitar alrededor del objeto seleccionado (o del origen) |
| `F` | Encuadrar el objeto seleccionado |

La velocidad de movimiento, la sensibilidad del ratón y el paso de zoom se pueden
ajustar en **Configuration > Input / Camera**.

### Selección y atajos

| Control | Acción |
|---|---|
| Clic izquierdo en el Viewport | Seleccionar el objeto bajo el cursor (clic en vacío = deseleccionar) |
| Clic en la Hierarchy | Seleccionar un objeto |
| `H` | Ocultar / mostrar el objeto seleccionado |
| `N` | Activar / desactivar la visualización global de normales |
| `Ctrl` + `Z` | Deshacer |
| `Ctrl` + `Y` (o `Ctrl` + `Shift` + `Z`) | Rehacer |
| `Supr` (con la Hierarchy enfocada) | Borrar el objeto seleccionado |
| `Ctrl` + `D` (con la Hierarchy enfocada) | Duplicar el objeto seleccionado |
| `Esc` | Salir del programa |

### Drag & drop

Se puede arrastrar a la ventana del motor cualquier archivo desde cualquier carpeta
del disco:

- **`.fbx`**: se carga el modelo y se crea un GameObject por cada malla. La textura
  difusa se busca por nombre de archivo en la misma carpeta que el FBX.
- **`.png`, `.jpg`, `.tga`, `.dds`**: la textura se aplica al GameObject seleccionado
  (si no hay ninguno, al primero de la Hierarchy).

### Ventanas del editor

Todas se pueden acoplar (docking) y mostrar u ocultar desde el menú **View**.

- **Viewport**: renderiza la escena. Muestra si el frustum culling está activo y
  cuántos objetos se han dibujado en el frame.
- **Hierarchy**: lista de todos los GameObjects. Clic para seleccionar; doble clic
  o clic derecho > *Rename* para renombrar; clic derecho > *Duplicate* / *Delete*;
  arrastrar un objeto sobre otro para reordenar la lista. Los objetos ocultos salen
  en gris.
- **Inspector**: componentes del GameObject seleccionado.
  - *Transform*: posición, rotación y escala **editables**, y botón *Reset Transform*.
  - *Mesh*: número de vértices e índices, y opciones de normales: *Normals Mode*
    (usar el ajuste global, forzar siempre o nunca), normales por vértice (verde)
    y normales por cara (magenta) solo para este objeto.
  - *Texture*: ruta y tamaño de la textura, vista previa, *Remove Texture*,
    *Open Folder* (abre la carpeta de la textura) y *Assign CheckerTexture*
    (textura de cuadros blancos y negros).
- **Console**: log de la carga de geometría (Assimp), de las texturas (stb_image y
  el cargador DDS), de la inicialización de las librerías, del flujo de la
  aplicación y de los errores. Todo lo que sale aquí también se escribe en la
  terminal.
- **Configuration**:
  - *System / Graphics Info*: versiones de SDL3, OpenGL, GLSL y Assimp; CPU, RAM
    del sistema y GPU.
  - *Window / Viewport*: tamaño de la ventana, VSync y frustum culling.
  - *Input / Camera*: velocidad, sensibilidad, zoom y estado de la cámara.
  - *Textures*: filtro y modo de wrap (se aplican también a las texturas ya
    cargadas), número de texturas y memoria que ocupan.
  - *Memory*: memoria del proceso (working set y privada) y tamaño de la escena.
  - *Performance*: FPS medios, gráfico del tiempo de frame.
- **About**: nombre y versión del motor, autor, librerías usadas y licencia.

### Menú principal

- **File > Exit**: salir del programa.
- **Edit > Undo / Redo**: deshacer y rehacer.
- **View**: mostrar u ocultar Console, Inspector, Hierarchy, Configuration y About,
  y activar las normales por vértice (*Normals*) y por cara (*Face Normals*) de toda
  la escena.
- **GameObject > 3D Object**: crear primitivas: *Cube*, *Sphere*, *Cylinder*,
  *Cone* y *Plane*.
- **Help**: *Documentation* (documentación en GitHub), *Report Bug* (issues),
  *Download Latest* (releases) y *About*.

## Funcionalidades extra

Además de lo que pide la entrega:

- **Deshacer / rehacer** (`Ctrl+Z` / `Ctrl+Y`, también en el menú Edit) de cualquier
  cambio en la escena: transform, texturas, crear, borrar, duplicar, renombrar o
  reordenar objetos. Funciona por snapshots de la escena, con un máximo de 30 pasos.
- **Transform editable** desde el Inspector (el bonus de la guía), con reset.
- **Mouse picking**: seleccionar objetos haciendo clic en el Viewport (raycast contra
  las cajas AABB, con un margen de unos píxeles para que sea cómodo acertar).
- **Frustum culling** de los objetos que quedan fuera de la cámara, activable desde
  Configuration, y **octree** de la escena que usa el picking para descartar objetos.
- **Hierarchy ampliada**: renombrar, duplicar, borrar, reordenar arrastrando y
  ocultar objetos (`H`).
- **Cargador DDS propio** (DXT1, DXT3, DXT5 y RGB/RGBA sin comprimir), sin depender
  de DevIL.
- **Normales por objeto**: además del interruptor global, cada GameObject puede forzar
  sus normales por vértice y por cara desde el Inspector.
- **Movimiento vertical** de la cámara libre con `Q` / `E`.
- **Ajustes de texturas en caliente** (filtro y wrap) y de la cámara desde
  Configuration.
- **Proyección de caja automática** para las mallas que llegan sin coordenadas UV,
  así se les puede aplicar una textura igualmente.
- Las texturas se comparten entre objetos y solo se liberan cuando ya no las usa
  ningún objeto (ni ningún paso del historial de deshacer).

## Comentarios para el profesor

- **Separación motor / editor.** El código del motor no depende de ImGui:
  - *Motor*: `Scene` (lista de GameObjects + octree), `Renderer` (framebuffer del
    viewport, culling, dibujo de mallas y normales), `Camera` (estado y movimientos
    de la cámara), `Geometry` (raycast, frustum, AABB), `Shaders`, `TextureLoader`,
    `DdsLoader`, `ModuleModelLoader` (Assimp), `Primitives`, `Octree`, `FrameTiming`.
  - *Editor*: la clase `Editor` (`Editor.h`), repartida en `Editor.cpp` (menú,
    docking, eventos y drag & drop), `EditorCamera.cpp` (lee el ratón y el teclado
    y mueve la `Camera`) y un archivo por panel (`PanelHierarchy.cpp`,
    `PanelInspector.cpp`, `PanelConfiguration.cpp`, `PanelViewport.cpp`), además de
    `UndoHistory`.
  - `main.cpp` solo crea las piezas, las conecta y lleva el bucle principal.
- **GameObjects y componentes.** Cada malla cargada es un GameObject con tres
  componentes: Transform, Mesh y Texture. Un FBX con varias mallas crea varios
  GameObjects independientes, **sin relación padre / hijo** (la entrega no pide
  jerarquía).
- **Texturas sin DevIL.** En lugar de DevIL se usa `stb_image` para PNG/JPG/TGA y un
  cargador DDS escrito para el motor. Por eso Configuration muestra las versiones de
  SDL3, OpenGL y Assimp, pero no la de DevIL.
- **Baker House.** El FBX trae un nodo con una rotación de -90° en X; el cargador la
  compensa para que la casa aparezca de pie. El motor trabaja con un sistema de mano
  derecha e Y hacia arriba.
- **`Resources/`** se busca junto al ejecutable, así que el motor funciona aunque se
  lance desde otra carpeta.
- Los controles de cámara y el zoom solo actúan con el cursor sobre el Viewport, para
  no interferir con los demás paneles.

## Licencia

El motor se distribuye bajo la licencia MIT (ver [`LICENCE.md`](LICENCE.md)). Las
licencias de las librerías de terceros están en la carpeta [`Licences/`](Licences).

---

## Compilar desde el código fuente

### Requisitos

- **Visual Studio 2022** (con el workload "Desarrollo para escritorio con C++").
- **CMake** 3.21 o superior (incluido con Visual Studio 2022, o instalable aparte).
- **vcpkg**, en modo manifest — las dependencias (SDL3, GLM, Assimp, ImGui) se
  resuelven automáticamente a partir de [`vcpkg.json`](vcpkg.json), no hace falta
  instalarlas a mano. La versión de las librerías la fija el baseline de
  [`vcpkg-configuration.json`](vcpkg-configuration.json).

### Pasos

1. Comprobar si ya tienes vcpkg instalado:

   ```cmd
   where vcpkg
   ```

   Si no aparece nada, clónalo y haz bootstrap (por ejemplo en `C:\vcpkg`):

   ```cmd
   git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   ```

2. Define `VCPKG_ROOT` apuntando a esa carpeta (una sola vez; abre una terminal
   nueva después para que el cambio surta efecto):

   ```cmd
   setx VCPKG_ROOT "C:\vcpkg"
   ```

3. Configura el proyecto (descarga y compila las dependencias declaradas en
   `vcpkg.json` la primera vez, puede tardar varios minutos):

   ```cmd
   cmake --preset=default
   ```

4. Compila:

   ```cmd
   cmake --build --preset=default
   ```

5. El ejecutable queda en `build/Debug/MotorAlbert.exe`. vcpkg copia junto a él
   las DLLs necesarias (SDL3, assimp, etc.) y CMake copia la carpeta `Resources/`,
   así que se puede lanzar directamente desde ahí.

### Estructura del repositorio

```
src/            Código fuente del motor y del editor
src/vendor/     Dependencias que no vienen de vcpkg (GLAD, generado para GL 4.6 Core)
Resources/      Modelos y texturas que usa el motor; se copian junto al .exe
Licences/       Licencias de las librerías de terceros
docs/           Material de la asignatura
CMakeLists.txt, CMakePresets.json, vcpkg.json, vcpkg-configuration.json   Buildsystem
```
