add_definitions(
#    -ggdb3 
    -std=gnu99
    )

add_library(object-model-client SHARED
    object_client.c)

# make the header public
pods_install_headers(object_client.h DESTINATION object_model)

# make the library public
pods_install_libraries(object-model-client)

# uncomment these lines to link against another library via pkg-config
set(REQUIRED_PACKAGES lcm 
    bot2-core 
    bot2-param-client 
    lcmtypes_object_model)

pods_use_pkg_config_packages(object-model-client ${REQUIRED_PACKAGES})

# create a pkg-config file for the library, to make it easier for other
# software to use.
pods_install_pkg_config_file(object-model-client
    CFLAGS
    LIBS -lobject-model-client
    REQUIRES ${REQUIRED_PACKAGES}
    VERSION 0.0.1)

add_executable(er-test-object-client test_object_client.c)

pods_use_pkg_config_packages(er-test-object-client object-model-client)

pods_install_executables(er-test-object-client)
