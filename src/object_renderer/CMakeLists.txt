find_package(PkgConfig REQUIRED)
pkg_check_modules(BOT2_VIS bot2-vis)
if(NOT BOT2_VIS_FOUND)
    message("\n\n")
    message("bot2-vis not found.  Not building cam thumb renderer")
    message("\n")
    return()
endif()

add_definitions(-std=gnu99)
add_library(object-model-renderer SHARED
    renderer_generic_object.c)

target_link_libraries(object-model-renderer ${OPENGL_LIBRARIES})

set(REQUIRED_LIBS bot2-vis bot2-param-client bot2-frames path-util lcmtypes_object_model)

pods_use_pkg_config_packages(object-model-renderer ${REQUIRED_LIBS})

pods_install_headers(renderer_generic_object.h DESTINATION object_model)
pods_install_libraries(object-model-renderer)
pods_install_pkg_config_file(object-model-renderer
    CFLAGS
    LIBS -lobject-model-renderer
    REQUIRES ${REQUIRED_LIBS}
    VERSION 0.0.1)