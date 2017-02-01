cmake_minimum_required(VERSION 2.6.0)

# pull in the pods macros. See cmake/pods.cmake for documentation
set(POD_NAME object_model)
include(cmake/pods.cmake)


# automatically build LCM types.  This also defines a number of CMake
# variables, see cmake/lcmtypes.cmake for details
include(cmake/lcmtypes.cmake)
lcmtypes_build()

include_directories(${LCMTYPES_INCLUDE_DIRS})

# create an executable, and make it public
add_subdirectory(src/object_server)
add_subdirectory(src/object_client)
add_subdirectory(src/object_renderer)
