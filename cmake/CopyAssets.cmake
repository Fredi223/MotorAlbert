# Copia Assets/ junto al ejecutable (lo ejecuta el target CopyResources con cmake -P).
#   cmake -DSRC=<Assets del repo> -DDST=<Assets junto al exe> -P CopyAssets.cmake
#
# No es un copy_directory porque ese copia todo en cada build:
#   - los assets saldrian con fecha nueva y el motor los reimportaria todos al arrancar;
#   - los .meta del repo pisarian a los de la build, y se perderian las Import Settings aplicadas
#     desde el editor.
# Asi que un asset se copia solo si ha cambiado, y un .meta solo si en la build todavia no hay uno
# (el de la build es el que el motor mantiene al dia).

file(GLOB_RECURSE files RELATIVE "${SRC}" "${SRC}/*")
set(copied 0)
foreach(rel IN LISTS files)
    set(from "${SRC}/${rel}")
    set(to "${DST}/${rel}")
    if(rel MATCHES "\\.meta$" AND EXISTS "${to}")
        continue()
    endif()
    get_filename_component(dir "${to}" DIRECTORY)
    file(MAKE_DIRECTORY "${dir}")
    file(COPY_FILE "${from}" "${to}" ONLY_IF_DIFFERENT RESULT err)
    if(err)
        message(WARNING "No se pudo copiar ${rel}: ${err}")
    endif()
endforeach()
