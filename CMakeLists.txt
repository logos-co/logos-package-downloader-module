cmake_minimum_required(VERSION 3.14)
project(LogosPackageDownloaderModulePlugin LANGUAGES CXX)

# Include the Logos Module CMake helper
if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

# Universal module — generated_code/ is picked up automatically by LogosModule.cmake
logos_module(
    NAME package_downloader
    SOURCES
        src/package_downloader_impl.h
        src/package_downloader_impl.cpp
    EXTERNAL_LIBS
        package_downloader_lib
)
