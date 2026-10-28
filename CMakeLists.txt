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
    src/Camera.cpp
    src/Camera.h
    src/Geometry.cpp
    src/Geometry.h
    src/Logger.cpp
    src/Logger.h
    src/ModuleModelLoader.cpp
    src/ModuleModelLoader.h
    src/Octree.cpp
    src/Octree.h
    src/Primitives.cpp
    src/Primitives.h
    src/Scene.cpp
    src/Scene.h
    src/Shaders.cpp
    src/Shaders.h
    src/SystemInfo.cpp
    src/SystemInfo.h
    src/TextureLoader.cpp
    src/TextureLoader.h
    src/UndoHistory.cpp
    src/UndoHistory.h
    src/DdsLoader.cpp
    src/DdsLoader.h
    src/Editor.cpp
    src/Editor.h
    src/EditorCamera.cpp
    src/PanelConfiguration.cpp
    src/PanelHierarchy.cpp
    src/PanelInspector.cpp
    src/PanelViewport.cpp
    src/FrameTiming.cpp
    src/FrameTiming.h
    src/Renderer.cpp
    src/Renderer.h
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

# Las extensiones gtx de GLM (yawPitchRoll en Transform.h) exigen esta macro antes de incluirlas;
# definirla aqui evita depender del orden de los #include.
target_compile_definitions(MotorAlbert PRIVATE GLM_ENABLE_EXPERIMENTAL)

target_link_libraries(MotorAlbert PRIVATE
    SDL3::SDL3
    OpenGL::GL
    glm::glm
    assimp::assimp
    imgui::imgui
)

if(WIN32)
    target_link_libraries(MotorAlbert PRIVATE psapi)   # GetProcessMemoryInfo
endif()

# Resources/ se copia junto al ejecutable: el motor la busca ahi (SDL_GetBasePath), tanto en
# build/ como en la carpeta de entrega. Es un target propio (y no un POST_BUILD del ejecutable)
# para que se copie tambien cuando solo cambian los recursos y el .exe no se vuelve a enlazar.
add_custom_target(CopyResources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/Resources $<TARGET_FILE_DIR:MotorAlbert>/Resources
    COMMENT "Copiando Resources junto al ejecutable"
)
add_dependencies(CopyResources MotorAlbert)
