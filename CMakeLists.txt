cmake_minimum_required(VERSION 3.14)
project(PackageDownloaderModulePlugin LANGUAGES CXX)

# Include the Logos Module CMake helper
if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

# Define the module
logos_module(
    NAME package_downloader
    SOURCES
        src/package_downloader_interface.h
        src/package_downloader_plugin.h
        src/package_downloader_plugin.cpp
    EXTERNAL_LIBS
        package_downloader_lib
    INCLUDE_DIRS
        lib
)
