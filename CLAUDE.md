# CLAUDE.md

## Contexto del proyecto

Motor gráfico desarrollado para una asignatura de la universidad. Se reutiliza y repara
código del año pasado (proyecto "MotorAlbert") en vez de empezar de cero: el core
(render, editor ImGui con docking, octree, picking por raycast, culling) ya funciona y
es trabajo no trivial. Lo que falta es "fontanería": build system, gestión de
dependencias, y limpieza de código muerto/rutas hardcodeadas.

## Requisitos de la asignatura

El profesor exige un toolchain específico, documentado en `docs/project-tools.pdf`.
No usar alternativas aunque sean más cómodas (ej. proyecto de Visual Studio suelto,
vcpkg en modo clásico, etc.) — el objetivo es cumplir exactamente lo pedido:

- **Git**: control de versiones con historial real de commits (no subidas manuales).
- **CMake**: buildsystem generator, con `CMakeLists.txt`.
- **vcpkg en modo manifest**: `vcpkg.json` + `vcpkg-configuration.json`, sin instalar
  dependencias manualmente ni depender de una carpeta de vcpkg fuera del repo.

Antes de proponer cualquier alternativa de tooling, consulta `docs/project-tools.pdf`.

## Dependencias del motor

SDL, OpenGL, GLM, Assimp, ImGui (con bindings de SDL3 + OpenGL3).

## Estado del proyecto

- Build system completo y funcional: Git + CMake + vcpkg (modo manifest) + GLAD
  (OpenGL 4.6 Core), verificado de punta a punta.
- Estructura de carpetas limpia: `src/` (con GLAD en `src/vendor/glad/`), `assets/`,
  `docs/`. `MotorAlbert.V2/` (build antiguo) eliminada.
- Cámara: libre (WASD + QE + Shift sprint, con deltaTime) y orbital (Alt+click, zoom
  con rueda, F para encuadrar) implementadas y verificadas.
- Visualizador de normales conectado y calibrado (offset proporcional, color verde),
  con toggle global y por objeto. Desactivado por defecto.
- Cascada de bugs de escena resuelta y verificada (con ASan y pruebas de referencia
  independientes): use-after-free en Delete/Duplicate del Hierarchy, AABB no
  refrescado al editar Transform desde el Inspector, `ExtractFrustum` con planos mal
  extraídos (columnas en vez de filas), y rayo de picking con mezcla de coordenadas
  absolutas/relativas y doble inversión de Y. Picking, culling y AABB son ahora
  consistentes entre sí.
- Pendiente: las teclas H/N no comprueban si hay un campo de texto activo en ImGui
  (pueden dispararse por accidente al renombrar un objeto); el wireframe de debug del
  octree sigue con `glBegin/glEnd` comentado, sin reescribir a VAO/VBO; el octree
  inicial (escena vacía) se construye con límites degenerados que la primera
  reconstrucción sustituye, y `RebuildOctree` con la escena vacía usa un fallback de
  límites fijos (±1000) en vez de algo más elegante.
- Primera entrega (cargar geometría, cámara libre y orbital, materiales/texturas,
  editor básico) funcionalmente completa. El frustum culling y el mouse picking, como
  mecanismos de bajo nivel, ya están corregidos y verificados (ver cascada de bugs
  arriba), pero como requisitos formales de la segunda entrega aún no se ha
  comprobado si cumplen todo lo que pide el enunciado. Resto de la segunda entrega
  (manejo de recursos, play/stop, serialización) y la tercera (sistema de alto nivel
  a elección) sin revisar.

## Convenciones de Git

- **No añadir la línea `Co-authored-by: Claude` (ni ninguna referencia a Claude/Anthropic)
  en los mensajes de commit.** El repositorio debe reflejar únicamente al usuario como
  autor y colaborador.
- Mensajes de commit descriptivos, en español, explicando el "qué" y el "por qué" del
  cambio, no solo el "qué".
- No hacer `push --force` a `main` sin avisar explícitamente antes y esperar
  confirmación — es una rama con trabajo real, no un scratch.
- Assets pesados (modelos 3D duplicados en varios formatos, texturas grandes) se
  ignoran vía `.gitignore` salvo que se pida explícitamente versionarlos. Assets
  pequeños usados como pruebas reales (ej. drag & drop) sí se versionan.

## Preferencias de trabajo

- Ir paso a paso en cambios estructurales grandes (build system, reorganización de
  carpetas), no todo de golpe — permite verificar que compila/funciona tras cada paso.
- Antes de una acción destructiva o irreversible (force push, borrar archivos,
  sobrescribir remoto), explicar el riesgo y pedir confirmación explícita.
- El usuario es estudiante de desarrollo de videojuegos — priorizar explicaciones que
  ayuden a entender el toolchain (CMake, vcpkg, Git), no solo resolver el problema sin
  contexto.
