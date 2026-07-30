cmake_minimum_required(VERSION 3.25)

project(GeoReader VERSION 0.1.0 LANGUAGES CXX)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

option(GEOREADER_BUNDLE_VCPKG_RUNTIME
    "Install vcpkg runtime libraries and data files into packages" OFF)
option(GEOREADER_BUILD_TESTS
    "Build lightweight GeoReader model tests" OFF)
set(GEOREADER_VCPKG_RUNTIME_ROOT "" CACHE PATH
    "Triplet directory containing vcpkg bin, lib and share folders")

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Qml
    Quick
    QuickControls2
    Widgets
    Concurrent
    LinguistTools
)
find_package(GDAL 3.8 CONFIG REQUIRED)

set(_georeader_dependency_roots)
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND _georeader_dependency_roots
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
endif()
if(GEOREADER_VCPKG_RUNTIME_ROOT)
    list(APPEND _georeader_dependency_roots
        "${GEOREADER_VCPKG_RUNTIME_ROOT}")
endif()

find_path(ICU_INCLUDE_DIR unicode/unistr.h
    HINTS
        ${_georeader_dependency_roots}
        /opt/homebrew/opt/icu4c@78
        /opt/homebrew/opt/icu4c
        /usr/local
        /usr
    PATH_SUFFIXES include
)
find_library(ICU_UC_LIBRARY NAMES icuuc
    HINTS
        ${_georeader_dependency_roots}
        /opt/homebrew/opt/icu4c@78
        /opt/homebrew/opt/icu4c
    PATH_SUFFIXES lib
)
find_library(ICU_I18N_LIBRARY NAMES icui18n icuin
    HINTS
        ${_georeader_dependency_roots}
        /opt/homebrew/opt/icu4c@78
        /opt/homebrew/opt/icu4c
    PATH_SUFFIXES lib
)

find_package(mapnik CONFIG QUIET)
if(TARGET mapnik::mapnik)
    set(GEOREADER_MAPNIK_LINK_TARGET mapnik::mapnik)
    if(COMMAND mapnik_find_plugin_dir)
        mapnik_find_plugin_dir(MAPNIK_INPUT_PLUGIN_DIR)
    endif()
else()
    find_path(MAPNIK_INCLUDE_DIR mapnik/map.hpp
        HINTS
            ${_georeader_dependency_roots}
            /opt/homebrew/opt/mapnik/include
            /usr/local/include
            /usr/include
        PATH_SUFFIXES include
    )
    find_library(MAPNIK_LIBRARY NAMES mapnik
        HINTS
            ${_georeader_dependency_roots}
            /opt/homebrew/opt/mapnik/lib
            /usr/local/lib
            /usr/lib64
            /usr/lib
        PATH_SUFFIXES lib
    )
    set(GEOREADER_MAPNIK_LINK_TARGET "${MAPNIK_LIBRARY}")
endif()

if(NOT MAPNIK_INPUT_PLUGIN_DIR)
    find_path(MAPNIK_INPUT_PLUGIN_DIR
        NAMES gdal+ogr.input gdal.input ogr.input shape.input
        HINTS
            ${_georeader_dependency_roots}
            /opt/homebrew/opt/mapnik/lib/mapnik/input
            /usr/local/lib/mapnik/input
            /usr/lib64/mapnik/input
            /usr/lib/x86_64-linux-gnu/mapnik/input
            /usr/lib/mapnik/input
        PATH_SUFFIXES
            lib/mapnik/input
            bin/mapnik/input
            bin/plugins/input
            tools/mapnik/plugins/input
            plugins/mapnik/input
            mapnik/input
    )
endif()

if((NOT TARGET mapnik::mapnik
    AND (NOT MAPNIK_INCLUDE_DIR OR NOT MAPNIK_LIBRARY))
   OR NOT MAPNIK_INPUT_PLUGIN_DIR)
    message(FATAL_ERROR
        "Mapnik headers, library, or input plugins were not found. "
        "Set MAPNIK_INCLUDE_DIR, MAPNIK_LIBRARY and MAPNIK_INPUT_PLUGIN_DIR.")
endif()
if(NOT ICU_INCLUDE_DIR OR NOT ICU_UC_LIBRARY OR NOT ICU_I18N_LIBRARY)
    message(FATAL_ERROR "ICU uc and i18n development libraries were not found.")
endif()

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(GeoReader
    src/main.cpp
    src/AppController.cpp
    src/AppController.h
    src/AttributeTableModel.cpp
    src/AttributeTableModel.h
    src/LayerModel.cpp
    src/LayerModel.h
    src/MapCanvas.cpp
    src/MapCanvas.h
    src/RasterRenderer.cpp
    src/RasterRenderer.h
)

if(GEOREADER_BUILD_TESTS)
    enable_testing()
    qt_add_executable(GeoReaderModelTests
        tests/ModelTests.cpp
        src/AttributeTableModel.cpp
        src/AttributeTableModel.h
        src/LayerModel.cpp
        src/LayerModel.h
    )
    target_include_directories(GeoReaderModelTests PRIVATE src)
    target_compile_definitions(GeoReaderModelTests PRIVATE
        GEOREADER_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    target_link_libraries(GeoReaderModelTests PRIVATE
        Qt6::Core
        Qt6::Gui
        GDAL::GDAL
    )
    add_test(NAME GeoReaderModelTests COMMAND GeoReaderModelTests)
endif()

qt_add_qml_module(GeoReader
    URI GeoReader
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/AppIcon.qml
        qml/components/EmptyState.qml
        qml/components/FloatingPanel.qml
        qml/components/ToolRailButton.qml
)

qt_add_translations(GeoReader
    TS_FILES translations/georeader_en.ts
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete
)

qt_add_resources(GeoReader "georeader_icons"
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES
        resources/icons/georeader.svg
)
set_property(TARGET GeoReader APPEND PROPERTY
    QT_EXCLUDE_SOURCES_FROM_TRANSLATION
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/georeader.svg")

if(APPLE)
    set_source_files_properties(resources/icons/georeader.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(GeoReader PRIVATE resources/icons/georeader.icns)
elseif(WIN32)
    target_sources(GeoReader PRIVATE resources/icons/georeader.rc)
endif()

target_include_directories(GeoReader SYSTEM PRIVATE "${ICU_INCLUDE_DIR}")
if(MAPNIK_INCLUDE_DIR)
    target_include_directories(GeoReader SYSTEM PRIVATE "${MAPNIK_INCLUDE_DIR}")
endif()
target_compile_definitions(GeoReader PRIVATE
    GEOREADER_MAPNIK_INPUT_DIR="${MAPNIK_INPUT_PLUGIN_DIR}"
    GEOREADER_VERSION="${PROJECT_VERSION}"
    QT_NO_CAST_FROM_ASCII
)
target_link_libraries(GeoReader PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::Concurrent
    GDAL::GDAL
    "${ICU_UC_LIBRARY}"
    "${ICU_I18N_LIBRARY}"
    ${GEOREADER_MAPNIK_LINK_TARGET}
)

if(APPLE)
    set_target_properties(GeoReader PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "GeoReader"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.georeader.desktop"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "georeader.icns"
    )
elseif(UNIX)
    set_target_properties(GeoReader PROPERTIES
        INSTALL_RPATH
            "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR};\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/georeader"
    )
endif()

install(TARGETS GeoReader
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

if(APPLE)
    set(GEOREADER_MAPNIK_PLUGIN_INSTALL_DIR
        "GeoReader.app/Contents/PlugIns/mapnik/input")
elseif(WIN32)
    set(GEOREADER_MAPNIK_PLUGIN_INSTALL_DIR
        "${CMAKE_INSTALL_BINDIR}/mapnik/input")
else()
    set(GEOREADER_MAPNIK_PLUGIN_INSTALL_DIR
        "${CMAKE_INSTALL_LIBDIR}/georeader/mapnik/input")
endif()

install(DIRECTORY "${MAPNIK_INPUT_PLUGIN_DIR}/"
    DESTINATION "${GEOREADER_MAPNIK_PLUGIN_INSTALL_DIR}"
    FILES_MATCHING
        PATTERN "gdal+ogr.input"
        PATTERN "gdal.input"
        PATTERN "ogr.input"
        PATTERN "geojson.input"
        PATTERN "shape.input"
        PATTERN "raster.input"
)

if(GEOREADER_BUNDLE_VCPKG_RUNTIME)
    if(NOT GEOREADER_VCPKG_RUNTIME_ROOT)
        message(FATAL_ERROR
            "GEOREADER_BUNDLE_VCPKG_RUNTIME requires "
            "GEOREADER_VCPKG_RUNTIME_ROOT to be set.")
    endif()

    if(WIN32)
        install(DIRECTORY "${GEOREADER_VCPKG_RUNTIME_ROOT}/bin/"
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            FILES_MATCHING PATTERN "*.dll")
    elseif(UNIX AND NOT APPLE)
        install(DIRECTORY "${GEOREADER_VCPKG_RUNTIME_ROOT}/lib/"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/georeader"
            FILES_MATCHING
                PATTERN "*.so"
                PATTERN "*.so.*")
    endif()

    foreach(_data_directory IN ITEMS gdal proj)
        if(EXISTS
           "${GEOREADER_VCPKG_RUNTIME_ROOT}/share/${_data_directory}")
            install(DIRECTORY
                "${GEOREADER_VCPKG_RUNTIME_ROOT}/share/${_data_directory}"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/georeader")
        endif()
    endforeach()
endif()

if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/com.georeader.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES resources/icons/georeader.svg
        DESTINATION
            "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
endif()

if(APPLE)
    install(FILES LICENSE THIRD_PARTY_NOTICES.md
        DESTINATION "GeoReader.app/Contents/Resources")
else()
    install(FILES LICENSE THIRD_PARTY_NOTICES.md
        DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/georeader")
endif()

if(NOT APPLE)
    qt_generate_deploy_qml_app_script(
        TARGET GeoReader
        OUTPUT_SCRIPT GEOREADER_QT_DEPLOY_SCRIPT
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT "${GEOREADER_QT_DEPLOY_SCRIPT}")
endif()

set(CPACK_PACKAGE_NAME "GeoReader")
set(CPACK_PACKAGE_VENDOR "GeoReader")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Modern Qt, Mapnik and GDAL spatial data viewer")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "GeoReader")
set(CPACK_PACKAGE_FILE_NAME
    "GeoReader-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_PACKAGE_EXECUTABLES "GeoReader" "GeoReader")
    set(CPACK_CREATE_DESKTOP_LINKS "GeoReader")
    set(CPACK_NSIS_DISPLAY_NAME "GeoReader")
    set(CPACK_NSIS_PACKAGE_NAME "GeoReader")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
else()
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "GeoReader maintainers")
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libwayland-client0, libwayland-cursor0, libwayland-egl1, libxkbcommon0")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    set(CPACK_RPM_PACKAGE_LICENSE "PolyForm-Noncommercial-1.0.0")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Engineering")
    set(CPACK_RPM_PACKAGE_REQUIRES "wayland-libs, libxkbcommon")
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
endif()

include(CPack)
