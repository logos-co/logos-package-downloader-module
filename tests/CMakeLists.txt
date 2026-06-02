cmake_minimum_required(VERSION 3.14)
project(PackageDownloaderModuleTests LANGUAGES CXX)

include(LogosTest)

# Unit tests with a link-time-mocked PackageDownloaderLib. The real
# library (network + disk) is never linked; mocks/mock_package_downloader_lib.cpp
# supplies the lgpd::PackageDownloaderLib / RepositoryRegistry symbols, and
# tests/stubs/package_downloader_lib.h provides the declarations the impl
# compiles against.
logos_test(
    NAME package_downloader_module_tests
    MODULE_SOURCES
        ../src/package_downloader_impl.cpp
    TEST_SOURCES
        main.cpp
        test_package_downloader.cpp
    MOCK_C_SOURCES
        mocks/mock_package_downloader_lib.cpp
    EXTRA_INCLUDES
        stubs
)
