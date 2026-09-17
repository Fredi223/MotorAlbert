# Motor Albert

Motor gráfico 3D en C++ con OpenGL 4.6 (Core Profile) y SDL3, desarrollado para una
asignatura de desarrollo de videojuegos. Incluye carga de modelos FBX vía Assimp,
texturas PNG (stb_image), editor con ImGui (docking), cámara libre tipo editor,
selección de objetos por raycast y frustum culling acelerado con un octree.

## Requisitos

- **Visual Studio 2022** (con el workload "Desarrollo para escritorio con C++").
- **CMake** 3.21 o superior (incluido con Visual Studio 2022, o instalable aparte).
- **vcpkg**, en modo manifest — las dependencias (SDL3, GLM, Assimp, ImGui) se
  resuelven automáticamente a partir de [`vcpkg.json`](vcpkg.json), no hace falta
  instalarlas a mano.

## Compilar

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
   las DLLs necesarias (SDL3, assimp, etc.), así que se puede lanzar directamente
   desde ahí.

## Estructura del proyecto

```
src/            Código fuente del motor
src/vendor/     Dependencias que no vienen de vcpkg (GLAD, generado para GL 4.6 Core)
assets/         Assets de prueba versionados (drag & drop)
docs/           Material de la asignatura
CMakeLists.txt, vcpkg.json, CMakePresets.json   Buildsystem
```
