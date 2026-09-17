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

- Repo Git inicializado, un único commit limpio como base (historial anterior de
  GitHub con subidas manuales fue reemplazado intencionadamente).
- CMake y vcpkg pendientes de configurar (ver siguiente sección de tareas si existe
  un archivo de seguimiento aparte, o preguntar al usuario por el estado actual).
- Los includes usaban prefijo `src/` de forma inconsistente con archivos planos —
  si se mueve código, verificar que la estructura de carpetas y los includes sean
  coherentes entre sí.

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
