cmake_minimum_required(VERSION 3.25)

project(GeoReader VERSION 1.0.1 LANGUAGES CXX)

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
if(UNIX AND NOT APPLE)
    # Qt's IBus platform input context depends on Qt DBus.  Qt 6.8 does not
    # deploy non-default input context plugins automatically, so make the
    # module available for the explicit Linux package installation below.
    find_package(Qt6 6.8 REQUIRED COMPONENTS DBus)
endif()
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
    # Mapnik 4.x exports its GeoJSON and WKT implementations as separate
    # targets. Static vcpkg builds do not pull these archives in through
    # mapnik::mapnik, so consumers must link every exported implementation
    # target explicitly. Keep the auxiliary targets conditional for packaged
    # Mapnik builds that still expose only the core target.
    set(GEOREADER_MAPNIK_LINK_TARGETS mapnik::mapnik)
    foreach(_mapnik_target IN ITEMS mapnik::json mapnik::wkt)
        if(TARGET "${_mapnik_target}")
            list(APPEND GEOREADER_MAPNIK_LINK_TARGETS "${_mapnik_target}")
        endif()
    endforeach()
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
    set(GEOREADER_MAPNIK_LINK_TARGETS "${MAPNIK_LIBRARY}")
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

if(NOT TARGET mapnik::mapnik
   AND (NOT MAPNIK_INCLUDE_DIR OR NOT MAPNIK_LIBRARY))
    message(FATAL_ERROR
        "Mapnik headers or library were not found. "
        "Set MAPNIK_INCLUDE_DIR and MAPNIK_LIBRARY.")
endif()
if(NOT ICU_INCLUDE_DIR OR NOT ICU_UC_LIBRARY OR NOT ICU_I18N_LIBRARY)
    message(FATAL_ERROR "ICU uc and i18n development libraries were not found.")
endif()

set(_georeader_mapnik_input_search_dirs)
if(MAPNIK_INPUT_PLUGIN_DIR)
    list(APPEND _georeader_mapnik_input_search_dirs
        "${MAPNIK_INPUT_PLUGIN_DIR}")
endif()
foreach(_dependency_root IN LISTS _georeader_dependency_roots)
    list(APPEND _georeader_mapnik_input_search_dirs
        "${_dependency_root}/lib/mapnik/input"
        "${_dependency_root}/bin/mapnik/input"
        "${_dependency_root}/bin/plugins/input"
        "${_dependency_root}/tools/mapnik/plugins/input"
        "${_dependency_root}/plugins/mapnik/input"
        "${_dependency_root}/mapnik/input")
endforeach()
list(APPEND _georeader_mapnik_input_search_dirs
    "/opt/homebrew/opt/mapnik/lib/mapnik/input"
    "/usr/local/lib/mapnik/input"
    "/usr/lib64/mapnik/input"
    "/usr/lib/x86_64-linux-gnu/mapnik/input"
    "/usr/lib/mapnik/input")
list(REMOVE_DUPLICATES _georeader_mapnik_input_search_dirs)

function(_georeader_resolve_mapnik_input_plugins output_files output_dir)
    foreach(_candidate_dir IN LISTS _georeader_mapnik_input_search_dirs)
        set(_candidate_files)
        set(_candidate_complete TRUE)
        foreach(_plugin_name IN ITEMS geojson.input raster.input shape.input)
            if(EXISTS "${_candidate_dir}/${_plugin_name}")
                list(APPEND _candidate_files
                    "${_candidate_dir}/${_plugin_name}")
            else()
                set(_candidate_complete FALSE)
            endif()
        endforeach()

        if(EXISTS "${_candidate_dir}/gdal+ogr.input")
            list(APPEND _candidate_files
                "${_candidate_dir}/gdal+ogr.input")
        elseif(EXISTS "${_candidate_dir}/gdal.input"
               AND EXISTS "${_candidate_dir}/ogr.input")
            list(APPEND _candidate_files
                "${_candidate_dir}/gdal.input"
                "${_candidate_dir}/ogr.input")
        else()
            set(_candidate_complete FALSE)
        endif()

        if(_candidate_complete)
            set("${output_files}" "${_candidate_files}" PARENT_SCOPE)
            set("${output_dir}" "${_candidate_dir}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set("${output_files}" "" PARENT_SCOPE)
    set("${output_dir}" "" PARENT_SCOPE)
endfunction()

_georeader_resolve_mapnik_input_plugins(
    GEOREADER_MAPNIK_INPUT_PLUGIN_FILES
    GEOREADER_MAPNIK_RESOLVED_INPUT_DIR)
if(NOT GEOREADER_MAPNIK_INPUT_PLUGIN_FILES)
    list(JOIN _georeader_mapnik_input_search_dirs "\n  " _search_dirs)
    message(FATAL_ERROR
        "No single Mapnik input directory contains all required plugins: "
        "GeoJSON, raster, shape, and GDAL/OGR.\n"
        "Searched directories:\n  ${_search_dirs}")
endif()
set(MAPNIK_INPUT_PLUGIN_DIR "${GEOREADER_MAPNIK_RESOLVED_INPUT_DIR}")
message(STATUS
    "GeoReader Mapnik input plugins: ${GEOREADER_MAPNIK_RESOLVED_INPUT_DIR}")
foreach(_plugin_file IN LISTS GEOREADER_MAPNIK_INPUT_PLUGIN_FILES)
    message(STATUS "  ${_plugin_file}")
endforeach()

qt_standard_project_setup(REQUIRES 6.8)

# Qt 默认会按 QML URI 在构建根目录创建 GeoReader/。Linux 可执行文件
# 同样名为 GeoReader，因此把所有 QML 工具产物集中到独立的 qml/ 目录。
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml")

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

if(APPLE)
    set(GEOREADER_MAPNIK_RUNTIME_INPUT_DIR "../PlugIns/mapnik/input")
elseif(WIN32)
    set(GEOREADER_MAPNIK_RUNTIME_INPUT_DIR "mapnik/input")
else()
    set(GEOREADER_MAPNIK_RUNTIME_INPUT_DIR
        "../${CMAKE_INSTALL_LIBDIR}/georeader/mapnik/input")
endif()

target_include_directories(GeoReader SYSTEM PRIVATE "${ICU_INCLUDE_DIR}")
if(MAPNIK_INCLUDE_DIR)
    target_include_directories(GeoReader SYSTEM PRIVATE "${MAPNIK_INCLUDE_DIR}")
endif()
target_compile_definitions(GeoReader PRIVATE
    GEOREADER_MAPNIK_INPUT_DIR="${MAPNIK_INPUT_PLUGIN_DIR}"
    GEOREADER_MAPNIK_RUNTIME_INPUT_DIR="${GEOREADER_MAPNIK_RUNTIME_INPUT_DIR}"
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
    ${GEOREADER_MAPNIK_LINK_TARGETS}
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

install(FILES ${GEOREADER_MAPNIK_INPUT_PLUGIN_FILES}
    DESTINATION "${GEOREADER_MAPNIK_PLUGIN_INSTALL_DIR}")

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
    set(_georeader_qt_deploy_options)
    if(UNIX AND Qt6_VERSION VERSION_GREATER_EQUAL 6.10)
        # Desktop integration plugins may be installed beside Qt's own
        # plugins without being part of the Qt SDK. Arch's Fcitx5, qt6ct and
        # Kvantum plugins have no writable RPATH entry, so Qt's deploy script
        # cannot relocate them while generating DEB/RPM packages. Keep Qt's
        # Compose and IBus input plugins and all required QPA/Wayland plugins,
        # while leaving optional desktop themes and styles to the target
        # distribution's system packages.
        list(APPEND _georeader_qt_deploy_options
            INCLUDE_PLUGINS
                qoffscreen
                qwayland
                qwayland-egl
                qwayland-generic
            EXCLUDE_PLUGINS libfcitx5platforminputcontextplugin
            EXCLUDE_PLUGIN_TYPES platformthemes styles)
    elseif(UNIX)
        # Plugin selection arguments were added to the deployment generator
        # in Qt 6.10. Qt 6.8 still exposes imported targets for the plugins we
        # require, so install those explicitly and let the regular Qt deploy
        # script collect the remaining QML modules, plugins and libraries.
        set(_georeader_legacy_linux_platform_plugins
            Qt6::QOffscreenIntegrationPlugin
            Qt6::QWaylandIntegrationPlugin)
        set(_georeader_legacy_linux_input_plugins
            Qt6::QComposePlatformInputContextPlugin
            Qt6::QIbusPlatformInputContextPlugin)
        foreach(_qt_plugin_target IN LISTS
                _georeader_legacy_linux_platform_plugins
                _georeader_legacy_linux_input_plugins)
            if(NOT TARGET "${_qt_plugin_target}")
                message(FATAL_ERROR
                    "Required Qt Linux plugin target is unavailable: "
                    "${_qt_plugin_target}")
            endif()
        endforeach()
        install(IMPORTED_RUNTIME_ARTIFACTS
            ${_georeader_legacy_linux_platform_plugins}
            LIBRARY DESTINATION
                "${CMAKE_INSTALL_LIBDIR}/qt6/plugins/platforms")
        install(IMPORTED_RUNTIME_ARTIFACTS
            ${_georeader_legacy_linux_input_plugins}
            LIBRARY DESTINATION
                "${CMAKE_INSTALL_LIBDIR}/qt6/plugins/platforminputcontexts")
        # The input plugins are intentionally outside Qt 6.8's default plugin
        # set, so its dependency scanner does not discover Qt DBus through
        # them. Install the module explicitly with the plugins.
        install(IMPORTED_RUNTIME_ARTIFACTS Qt6::DBus
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif()
    qt_generate_deploy_qml_app_script(
        TARGET GeoReader
        OUTPUT_SCRIPT GEOREADER_QT_DEPLOY_SCRIPT
        NO_UNSUPPORTED_PLATFORM_ERROR
        ${_georeader_qt_deploy_options}
    )
    if(UNIX AND Qt6_VERSION VERSION_LESS 6.9)
        # Qt 6.8's generic Linux deploy helper installs into DESTDIR but
        # rewrites plug-in RPATHs through the un-staged install prefix.  Use a
        # narrow wrapper until the upstream Qt 6.9 path handling is available.
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunQt68Deploy.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/RunQt68Deploy.cmake"
            @ONLY)
        install(SCRIPT
            "${CMAKE_CURRENT_BINARY_DIR}/RunQt68Deploy.cmake")
    else()
        install(SCRIPT "${GEOREADER_QT_DEPLOY_SCRIPT}")
    endif()
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
    # Keep system packages rooted at /usr and expose CPack's staging directory
    # to the Qt 6.8 compatibility wrapper above.
    set(CPACK_PACKAGE_RELOCATABLE OFF)
    set(CPACK_SET_DESTDIR ON)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "GeoReader maintainers")
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    string(CONCAT CPACK_DEBIAN_PACKAGE_DEPENDS
        "libdbus-1-3, libwayland-client0, libwayland-cursor0, "
        "libwayland-egl1, libxkbcommon0")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    set(CPACK_RPM_PACKAGE_LICENSE "PolyForm-Noncommercial-1.0.0")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Engineering")
    set(CPACK_RPM_PACKAGE_REQUIRES
        "dbus-libs, wayland-libs, libxkbcommon")
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
endif()

include(CPack)
