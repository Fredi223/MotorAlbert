cmake_minimum_required(VERSION 3.21)

project(MotorAlbert LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependencias ---
# Se resuelven via vcpkg (manifest mode), pendiente de configurar en el
# siguiente paso: vcpkg.json + CMAKE_TOOLCHAIN_FILE apuntando a
# vcpkg/scripts/buildsystems/vcpkg.cmake. Hasta entonces estos
# find_package fallan porque no hay ningun paquete instalado todavia.
find_package(SDL3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

# GLAD no esta cubierto aqui: no es un find_package normal (requiere
# generar el loader para el spec de OpenGL 4.6 Core que usa el motor)
# y ahora mismo ni siquiera esta vendorizado en el repo. Pendiente de
# decidir como se resuelve antes de poder compilar.

add_executable(MotorAlbert
    src/main.cpp
    src/Logger.cpp
    src/Logger.h
    src/ModuleModelLoader.cpp
    src/ModuleModelLoader.h
    src/Octree.cpp
    src/Octree.h
    src/GameObject.h
    src/Transform.h
    src/stb_image.h
)

target_include_directories(MotorAlbert PRIVATE src)

target_link_libraries(MotorAlbert PRIVATE
    SDL3::SDL3
    OpenGL::GL
    glm::glm
    assimp::assimp
    imgui::imgui
)
