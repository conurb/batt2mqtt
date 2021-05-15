cmake_minimum_required(VERSION 3.0)

project(batt2mqtt C)

find_package(PkgConfig REQUIRED)

find_library(COREFOUNDATION_LIBRARY CoreFoundation)
find_library(IOKIT_LIBRARY IOKit) 

pkg_check_modules(LIBMOSQUITTO REQUIRED IMPORTED_TARGET libmosquitto)

include_directories(${LIBMOSQUITTO_INCLUDE_DIRS})

add_executable(${PROJECT_NAME} src/batt2mqtt.c)

target_link_libraries(${PROJECT_NAME}
    pthread
    PkgConfig::LIBMOSQUITTO
    ${COREFOUNDATION_LIBRARY}
    ${IOKIT_LIBRARY}
)

install(TARGETS ${PROJECT_NAME} DESTINATION ~/bin)
install(FILES batt2mqtt.plist DESTINATION ~/Library/LaunchAgents)
