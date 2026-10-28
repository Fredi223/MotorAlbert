cmake_minimum_required(VERSION 3.21)

project(MotorAlbert LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependencias ---
# Resueltas via vcpkg en modo manifest (vcpkg.json), a traves del
# toolchain file configurado en CMakePresets.json.
find_package(SDL3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

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
    src/vendor/glad/src/glad.c
    src/vendor/glad/include/glad/glad.h
    src/vendor/glad/include/KHR/khrplatform.h
)

target_include_directories(MotorAlbert PRIVATE
    src
    src/vendor/glad/include
)

target_link_libraries(MotorAlbert PRIVATE
    SDL3::SDL3
    OpenGL::GL
    glm::glm
    assimp::assimp
    imgui::imgui
)

# Resources/ se copia junto al ejecutable en cada compilacion: el motor la busca ahi
# (SDL_GetBasePath), tanto en build/ como en la carpeta de entrega.
add_custom_command(TARGET MotorAlbert POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Resources $<TARGET_FILE_DIR:MotorAlbert>/Resources
)
