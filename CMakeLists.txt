cmake_minimum_required(VERSION 3.14)
project(apache2_mod_proxy_grpc CXX)

include(FetchContent)
find_package (Git)
find_package(PkgConfig REQUIRED)
FetchContent_Declare(
  gRPC
  GIT_REPOSITORY https://github.com/grpc/grpc
  GIT_TAG        v1.34.0
)
set(FETCHCONTENT_QUIET OFF)
set(gRPC_INSTALL OFF CACHE INTERNAL "Disable grpc install targets")
set(gRPC_BUILD_TESTS OFF CACHE INTERNAL "Disable building test cases")
set(gRPC_BUILD_CODEGEN OFF CACHE INTERNAL "Disable building codegen")
set(gRPC_BUILD_CSHARP_EXT OFF CACHE INTERNAL "Disable building codegen")
set(gRPC_BACKWARDS_COMPATIBILITY_MODE OFF CACHE INTERNAL "Disable backwards support")
set(gRPC_BUILD_GRPC_CPP_PLUGIN OFF CACHE INTERNAL "Disable building codegen")
set(gRPC_BUILD_GRPC_CSHARP_PLUGIN OFF CACHE INTERNAL "Disable building codegen")
set(gRPC_BUILD_GRPC_NODE_PLUGIN OFF CACHE INTERNAL "Disable building codegen")
set(gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN OFF CACHE INTERNAL "Disable building codegen")
set(gRPC_BUILD_GRPC_PHP_PLUGIN OFF CACHE INTERNAL "Disable building codegen")
set(gRPC_BUILD_GRPC_PYTHON_PLUGIN OFF CACHE INTERNAL "Disable building codegen")
set(gRPC_BUILD_GRPC_RUBY_PLUGIN OFF CACHE INTERNAL "Disable building codegen")
set(gRPC_BUILD_GRPC_PYTHON_PLUGIN OFF CACHE INTERNAL "Disable building codegen")

# Fix FetchContent not setting EXCLUDE_FROM_ALL
macro(add_subdirectory)
    _add_subdirectory(${ARGN} EXCLUDE_FROM_ALL)
endmacro()
FetchContent_MakeAvailable(gRPC)

if (GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty=-d --long
        OUTPUT_VARIABLE VERSION
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
else (GIT_FOUND)
      set(VERSION "v0.0.0-0-00000000-d")
endif (GIT_FOUND)

#parse the version information into pieces.
string(REGEX REPLACE "^v([0-9]+)\\..*" "\\1" VERSION_MAJOR "${VERSION}")
string(REGEX REPLACE "^v[0-9]+\\.([0-9]+).*" "\\1" VERSION_MINOR "${VERSION}")
string(REGEX REPLACE "^v[0-9]+\\.[0-9]+\\.([0-9]+).*" "\\1" VERSION_PATCH "${VERSION}")
string(REGEX REPLACE "^v[0-9]+\\.[0-9]+\\.[0-9]+-([0-9]+).*" "\\1" VERSION_COMMIT "${VERSION}")
string(REGEX REPLACE "^v[0-9]+\\.[0-9]+\\.[0-9]+-[0-9]+-(.*)" "\\1" VERSION_SHA1 "${VERSION}")
set(VERSION_SHORT "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

pkg_check_modules(APR1 REQUIRED IMPORTED_TARGET apr-1)
execute_process(
    COMMAND apxs -q INCLUDEDIR
    OUTPUT_VARIABLE APXS_INCLUDEDIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

add_library(mod_proxy_grpc SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/base64.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/grpc_proxy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/module.cpp
)
target_include_directories(mod_proxy_grpc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${APXS_INCLUDEDIR}
)
target_link_libraries(mod_proxy_grpc grpc++_unsecure PkgConfig::APR1)
target_link_libraries(mod_proxy_grpc -static-libstdc++)
set_target_properties(mod_proxy_grpc PROPERTIES PREFIX "")

install(
    TARGETS mod_proxy_grpc
    DESTINATION /usr/lib/apache2/modules/
)
install(
    FILES mod_proxy_grpc.load
    DESTINATION /etc/apache2/mods-available/proxy_grpc.load
)

add_custom_target(run-apache
    COMMAND apache2 -d . -f httpd.conf -e info -DFOREGROUND -X
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS mod_proxy_grpc
)
add_custom_target(run-apache-balancer
    COMMAND apache2 -d . -f httpd-balancer.conf -e info -DFOREGROUND -X
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS mod_proxy_grpc
)
add_custom_target(debug-apache
    COMMAND gdb --args apache2 -d . -f httpd.conf -e info -DFOREGROUND -X
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS mod_proxy_grpc
)

add_custom_target(ab-bench
    COMMAND ab -p sample_post.txt -T application/grpc-web-text -H 'X-Grpc-Web: 1' -H 'Accept: application/grpc-web-text' -c 100 -n 100000 http://127.0.0.1:8080/helloworld.Greeter/SayHello
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS mod_proxy_grpc
)

set(CPACK_DEB_COMPONENT_INSTALL OFF)
set(CPACK_PACKAGE_NAME "libapache2-mod-proxy-grpc")
set(CPACK_PACKAGE_VENDOR "Dominik Thalhammer")
set(CPACK_PACKAGE_CONTACT "dominik@thalhammer.it")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "grpc proxy module for Apache2")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Thalhammer/mod_proxy_grpc")
set(CPACK_PACKAGE_VERSION_MAJOR ${VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${VERSION_PATCH})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Dominik Thalhammer <dominik@thalhammer.it>")
set(CPACK_DEBIAN_PACKAGE_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
set(CPACK_DEBIAN_PACKAGE_RELEASE "${VERSION_COMMIT}")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "apache2-bin")

if (UNIX)
    set(CPACK_GENERATOR "DEB")
endif (UNIX)

include(CPack)