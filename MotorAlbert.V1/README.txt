Aviso!!!!
El motor tiene fallos que seran arreglados para próximas entregas!

CONTROLES:
 - Click en el hierarchy al objeto para seleccionarlo
 - Doble click en el inspector para modificar los valores
 - Arrastrar los valores del inspector para modificarlos


Funcionalidades implementadas en el proyecto:

1. Inicialización de SDL3 y OpenGL
   - Inicializa SDL3 correctamente (SDL_Init)
   - Crea una ventana con SDL3
   - Configura un contexto OpenGL (SDL_GL_CreateContext)
   - Habilita VSync (SDL_GL_SetSwapInterval(1))
   - Manejo básico de errores en inicialización

2. Gestión de ventana
   - Creación de ventana con tamaño configurable
   - Soporte para modo ventana y posiblemente pantalla completa

3. Carga de recursos
   - Carpeta Assets/Resources para FBX, PNG, DDS y otros ficheros multimedia
   - Integración inicial para cargar ficheros de recursos

4. Compilación multiplataforma con CMake y vcpkg
   - Configuración de CMake para Windows (Visual Studio 17 2022)
   - Uso de vcpkg para dependencias externas (SDL3, Glad, etc.)

5. Soporte para librerías externas
   - SDL3 (ventana, input, eventos)
   - OpenGL con Glad

6. Estructura de proyecto modular
   - Separación de código fuente (.cpp / .h)
   - Carpeta build para compilaciones
   - Preparación para release y debug

7. Logs y manejo de errores
   - Mensajes en consola para verificar inicialización de SDL y OpenGL
   - Mensajes de error si SDL u OpenGL falla