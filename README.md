# Motor Albert

Motor 3D en C++20 con OpenGL 4.6 (Core Profile) y SDL3, desarrollado para la asignatura
de Motores. Esta versión (**v2.0**) corresponde a la **Entrega 2 — Editor del motor**:
escena jerárquica de GameObjects con componentes, cámara como componente con frustum
culling y picking acelerados por un octree, gestor de recursos con formato propio en
`Library/`, serialización de la escena y Play / Pause / Stop.

- **Repositorio:** https://github.com/Fredi223/MotorAlbert
- **Releases:** https://github.com/Fredi223/MotorAlbert/releases

## Integrantes

| Nombre | GitHub |
|---|---|
| Albert Frederic Bailen Ruesca | [Fredi223](https://github.com/Fredi223) |

## Cómo usar el motor

Al arrancar, el motor importa la carpeta `Assets/` (que está junto al ejecutable) a
`Library/` y carga automáticamente la escena **Street environment**. La consola muestra
qué se importa y qué se carga.

### Cámara del editor

Los controles de cámara solo responden con el cursor sobre la ventana **Viewport**.

| Control | Acción |
|---|---|
| Clic derecho (mantener) + mover ratón | Mirar alrededor (free look) |
| Clic derecho (mantener) + `W` `A` `S` `D` | Moverse adelante / izquierda / atrás / derecha |
| Clic derecho (mantener) + `Q` / `E` | Bajar / subir |
| `Shift` (mientras se mueve) | Duplica la velocidad de movimiento |
| Rueda del ratón | Zoom hacia el objeto seleccionado (o hacia el origen) |
| `Alt` + clic izquierdo (mantener) + mover ratón | Orbitar alrededor del objeto seleccionado (o del origen) |
| `F` | Encuadrar el objeto seleccionado (con toda su rama de hijos) |

La velocidad, la sensibilidad, el zoom y el FOV / near / far de la cámara del editor se
ajustan en **Configuration > Input / Camera**.

### Atajos

| Control | Acción |
|---|---|
| Clic izquierdo en el Viewport | Seleccionar el objeto bajo el cursor (test contra sus triángulos); clic en vacío = deseleccionar |
| `H` | Ocultar / mostrar el objeto seleccionado (y sus hijos) |
| `N` | Activar / desactivar las normales de toda la escena |
| `Ctrl` + `Z` / `Ctrl` + `Y` (o `Ctrl` + `Shift` + `Z`) | Deshacer / rehacer |
| `Ctrl` + `S` | Guardar la escena |
| `Ctrl` + `P` | Play / Stop |
| `Ctrl` + `Shift` + `P` | Pausar / continuar |
| `Supr` (con la Hierarchy enfocada) | Borrar el objeto seleccionado y sus hijos |
| `Ctrl` + `D` (con la Hierarchy enfocada) | Duplicar el objeto seleccionado y sus hijos |
| `Esc` | Salir del programa |

### Jerarquía (Hierarchy)

- Árbol de GameObjects con padres e hijos. Un FBX se carga con su jerarquía de nodos.
- **Arrastrar** un objeto sobre otro lo hace su hijo (conservando su posición en el
  mundo); soltarlo en el hueco vacío lo deja en el primer nivel.
- **Clic derecho** sobre un objeto: *Rename*, *Create Empty Child*, *Duplicate*,
  *Unparent* y *Delete*. Clic derecho en el hueco vacío: *Create Empty*. Doble clic =
  renombrar.
- Al seleccionar un objeto con el ratón en el Viewport, la Hierarchy despliega sus padres.

### Inspector

- **Transform** (local, relativo al padre): posición, rotación y escala, y *Reset Transform*.
- Cada componente tiene una casilla para activarlo / desactivarlo, *Remove Component*
  (botón o clic derecho en la cabecera) y se añaden con **Add Component**:
  - **Mesh**: desplegable con todas las mallas importadas (o arrastrar una malla desde los
    paneles Assets o Resources), UID y referencias, vértices / índices y opciones de normales.
  - **Material**: desplegable con todas las texturas importadas (o arrastrar una), tamaño,
    referencias y vista previa, y el **color** del material (multiplica la textura). Sin
    textura se dibuja el color liso; sin textura y en blanco, la textura de cuadros. Así salen
    el asfalto y las aceras de Street, que en el FBX solo tienen color, no textura.
  - **Camera**: FOV, near, far y *Culling camera* (el frustum culling se hace con esta cámara
    en lugar de la del editor, para poder ver desde fuera lo que descarta).
  - **Rotator**: grados por segundo en cada eje; el objeto solo gira durante el Play.

### Escenas, Play y recursos

- **File > New Scene / Open Scene... / Save Scene / Save Scene As...**: las escenas se
  guardan en JSON (`.scene`) en `Assets/Scenes/`. También se abren con doble clic desde el
  panel Assets.
- **Play / Pause / Step / Stop** (centro de la barra de menú): Play guarda la escena en
  memoria y empieza a correr el tiempo de juego; Stop la restaura tal como estaba. La barra
  se pone azul en Play y naranja en pausa. Durante el Play no hay deshacer y no se puede
  guardar ni abrir escenas. El campo `x1.00` es la escala del tiempo de juego.
- **Drag & drop** de un archivo (`.fbx`, `.png`, `.jpg`, `.tga`, `.bmp`, `.dds`) desde
  cualquier carpeta a la ventana: se copia a la carpeta abierta en el panel Assets (o a la
  raíz de `Assets/` si el panel está cerrado) y se importa a `Library/`. Un FBX se añade a la
  escena; una textura se aplica al objeto seleccionado.
- **GameObject > Create Empty / Create Empty Child / 3D Object (Cube, Sphere, Cylinder,
  Cone, Plane) / Camera**. La cámara nueva se crea donde está la del editor.

### Opciones de importado (Import Settings)

Al hacer clic en una textura o un FBX del panel Assets, el Inspector muestra sus datos y sus
**Import Settings**. Los cambios no se aplican hasta pulsar **Apply** (*Revert* los descarta)
y se guardan en el `.meta` del asset, así que si se borra `Library/` se regenera igual.

- **Texturas**: tamaño, formato y vista previa grande.
  - *Filter*: Linear + mipmaps, Linear o Nearest.
  - *Wrap S* / *Wrap T*: Repeat, Clamp to edge o Mirrored repeat.
  - *Flip X* / *Flip Y*: voltean la imagen y la reimportan. En los DDS comprimidos (DXT)
    solo se puede voltear en Y.
  - Apply actualiza la textura al momento en todos los objetos que la usan.
- **Modelos (FBX)**: número de mallas (con sus UIDs) y de nodos, y las cámaras y luces que
  trae el archivo.
  - *Global scale*: multiplica la escala de unidades del archivo.
  - *Axis*: Y arriba (sin giro), Z arriba (-90° en X) o *Custom* (una rotación en grados).
  - *Ignore cameras* / *Ignore lights*: sin marcar, cada cámara del FBX se importa como un
    GameObject con componente Camera (con su FOV, near y far) y cada luz como un GameObject
    vacío con su nombre (el motor no tiene luces). Prueba: *Street environment_V01.FBX* trae
    una cámara (`Camera001`) y dos luces.
  - Apply reimporta el modelo, pero lo que ya está en la escena no cambia: solo las
    instancias nuevas (botón *Instantiate* o doble clic) salen con las opciones nuevas.

### Ventanas del editor

Todas se pueden acoplar (docking) y mostrar u ocultar desde el menú **View**.

- **Viewport**: la escena. Muestra la cámara que hace el culling, cuántos objetos se
  dibujan y descartan, y cuántos candidatos y nodos visita el octree.
- **Assets**: explorador de la carpeta `Assets/`. A la izquierda, el árbol de carpetas; a la
  derecha, el contenido de la carpeta actual en una rejilla, con un icono por tipo de archivo
  y la **miniatura** de cada textura. Arriba, la ruta (cada parte lleva a su carpeta) y el
  botón para subir.
  - **Clic**: selecciona el asset y muestra sus datos e *Import Settings* en el Inspector.
  - **Doble clic**: una carpeta se abre, un FBX se añade a la escena, una textura se aplica
    al objeto seleccionado y un `.scene` se abre.
  - **Arrastrar**: las texturas y las mallas al Inspector; los FBX y las texturas también al
    Viewport.
  - **Clic derecho** en un asset: sus acciones, *Show meshes* (en un FBX: ver sus mallas
    para arrastrarlas al campo Mesh), *Show in Explorer* y *Delete* (borra el archivo, su
    `.meta` y sus archivos de `Library/`; en una carpeta, todo lo que contiene, después de
    decir cuántos archivos son). En el hueco: *New Folder*, *Refresh* y *Show in Explorer*.
  - **Refresh** importa lo que se haya copiado a `Assets/` desde Windows y olvida lo que se
    haya borrado.
- **Resources**: todos los recursos (mallas, texturas, modelos) con su UID, su archivo de
  `Library/` y su **número de referencias**; los cargados en memoria salen en verde.
- **Configuration**: información del sistema y de la GPU, ventana y VSync, frustum culling,
  cámara del editor, texturas cargadas (y su VRAM aproximada), memoria y rendimiento.
- **Console**: log de todo lo que hace el motor (también sale en la terminal).
- **View > Debug Draw**: AABBs (verde), octree (azul), rayo del picking (rojo) y frustums de
  las cámaras (amarillo; la culling camera en naranja).

## Funcionalidades extra

Además de lo que pide la entrega:

- **Gestor de recursos ampliado** (los requisitos de la entrega aplazada, hechos aunque se
  entrega a tiempo):
  - **Explorador de assets** con árbol de carpetas, rejilla con iconos y miniaturas,
    *New Folder*, borrar carpetas enteras e importar en la carpeta abierta.
  - **Opciones de importado** de texturas (filtro, wrap, flip) y de modelos (escala, ejes,
    cámaras y luces), guardadas en el `.meta` (ver *Opciones de importado*).
  - **Cámaras de los FBX** importadas como cámaras de juego.
- **Color difuso del material**: los materiales del FBX que solo tienen color (sin textura)
  se ven con su color, y se puede editar en el Inspector.
- **Deshacer / rehacer** de cualquier cambio en la escena (transform, componentes,
  jerarquía, crear, borrar, duplicar, renombrar...), por snapshots, hasta 30 pasos.
- **Picking por triángulos**: el octree y los AABB descartan, y el resultado final es el
  triángulo más cercano que toca el rayo (Möller–Trumbore), no la caja.
- **Debug draw** del octree, de los AABB, del rayo del picking y de los frustums.
- **Step** y **escala de tiempo** en la simulación, y un tiempo de juego separado del real.
- **Componente Rotator** para ver la simulación en marcha.
- **Cargador DDS propio** (DXT1/3/5 y sin comprimir): en `Library/` las texturas DDS se
  guardan comprimidas, con sus mipmaps.
- **Normales** por vértice y por cara, globales o forzadas por objeto.
- **Proyección de caja automática** para las mallas que llegan sin coordenadas UV.

## Comentarios para el profesor

- **Formato propio (`Library/`)**: `Meshes/<uid>.mesh` (cabecera + vértices + índices),
  `Textures/<uid>.tex` (RGBA8, o DXT con mipmaps si viene de un DDS, y el filtro y el wrap
  de sus opciones de importado) y `Models/<uid>.model` (el árbol de nodos del FBX con el
  transform, los UIDs de malla y textura, el color del material y, si es una cámara, su FOV,
  near y far). Cada formato tiene versión: un archivo de una versión anterior se reimporta
  solo, con los mismos UIDs.
  Assimp y stb_image solo se usan al importar; el resto del tiempo todo se carga de
  `Library/`. Las primitivas también son recursos (con UIDs fijos 1–5).
- **`.meta`**: cada asset tiene un `.meta` (JSON) con su UID, su fecha, sus opciones de
  importado (`"import"`) y, en los FBX, los UIDs de sus mallas. Al arrancar se importa lo que no tiene `.meta`, lo que ha cambiado y lo
  que falta en `Library/`. Si se borra `Library/`, se regenera con los mismos UIDs, así que
  las escenas guardadas siguen funcionando.
- **Conteo de referencias**: los componentes guardan un `shared_ptr` al recurso y el gestor
  un `weak_ptr`; las referencias son el `use_count`. Un recurso se carga de `Library/` con su
  primera referencia y se descarga de la GPU con la última (la consola lo indica). El panel
  Resources lo muestra; las referencias incluyen los pasos del historial de deshacer.
- **Escena**: cada GameObject es dueño de sus hijos (`unique_ptr`) y tiene un UID de 64
  bits. En los archivos solo se guardan UIDs, nunca punteros. El transform es local y la
  matriz de mundo se recalcula cada frame. La serialización de la escena es la misma que
  usa Play / Stop para guardar el estado inicial en memoria.
- **Octree**: se reconstruye cuando cambia la escena, con los límites de la propia escena.
  El frustum culling descarta subárboles enteros, y el picking solo prueba los objetos de
  los nodos que cruza el rayo.
- **Separación motor / editor**: el código del motor no depende de ImGui. *Motor*: `Scene`,
  `GameObject`, `Component`, `ResourceManager`, `LibraryFormat`, `SceneSerializer`,
  `Simulation`, `Renderer`, `Octree`, `Geometry`, `Camera`, `Shaders`, `TextureLoader`,
  `DdsLoader`, `ModuleModelLoader`, `Primitives`, `FrameTiming`. *Editor*: la clase `Editor`,
  repartida en un archivo por panel (`Panel*.cpp`), `EditorCamera.cpp`, `EditorSceneFile.cpp`,
  y `UndoHistory`.
- El código del motor compila sin avisos con `/W4`.
- **Street environment** se instancia al arrancar desde su `.model` de `Library/`. La Baker
  House necesita un giro de +90° en X: lo lleva su `.meta` como opción de importado (*Axis:
  Custom*).
- Al compilar, `cmake/CopyAssets.cmake` copia `Assets/` junto al `.exe`, pero solo lo que ha
  cambiado y sin pisar los `.meta` que ya hay allí (así no se pierden las opciones aplicadas
  desde el editor ni se reimporta todo en cada build).

## Licencia

El motor se distribuye bajo la licencia MIT (ver [`LICENCE.md`](LICENCE.md)). Las
licencias de las librerías de terceros están en la carpeta [`Licences/`](Licences).

---

## Compilar desde el código fuente

### Requisitos

- **Visual Studio 2022** (con el workload "Desarrollo para escritorio con C++").
- **CMake** 3.21 o superior (incluido con Visual Studio 2022, o instalable aparte).
- **vcpkg**, en modo manifest — las dependencias (SDL3, GLM, Assimp, ImGui,
  nlohmann-json) se resuelven automáticamente a partir de [`vcpkg.json`](vcpkg.json), no
  hace falta instalarlas a mano. La versión de las librerías la fija el baseline de
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
   las DLLs necesarias (SDL3, assimp, etc.) y CMake copia la carpeta `Assets/`,
   así que se puede lanzar directamente desde ahí. `Library/` se crea al lado al arrancar.

6. Para la build Release (la que se entrega) se usa el preset `release`; el
   resultado queda en `build/Release/`:

   ```cmd
   cmake --build --preset=release
   ```

### Estructura del repositorio

```
src/            Código fuente del motor y del editor
src/vendor/     Dependencias que no vienen de vcpkg (GLAD, generado para GL 4.6 Core)
Assets/         Modelos, texturas, escenas y sus .meta; se copian junto al .exe
Licences/       Licencias de las librerías de terceros
docs/           Material de la asignatura
cmake/          Script que copia Assets/ junto al .exe al compilar
CMakeLists.txt, CMakePresets.json, vcpkg.json, vcpkg-configuration.json   Buildsystem
```
