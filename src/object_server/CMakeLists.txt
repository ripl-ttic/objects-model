add_definitions(
#    -ggdb3 
    -std=gnu99
    )

add_executable(object-server object_server.c)

pods_use_pkg_config_packages(object-server 
    lcm 
    bot2-core 
    bot2-param-client
    lcmtypes_object_model)

pods_install_executables(object-server)