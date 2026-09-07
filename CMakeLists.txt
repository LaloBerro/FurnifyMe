cmake_minimum_required(VERSION 3.20)
project(FurnifyMe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Milestone 1 sequencing (see CLAUDE.md): the headless geometry test must build,
# run and pass before any Qt code exists. Turn the app off to build tests alone
# on a machine that has OCCT but no Qt:  -DFURNIFYME_BUILD_APP=OFF
option(FURNIFYME_BUILD_APP "Build the Qt desktop application" ON)

find_package(OpenCASCADE REQUIRED)

# OCCT 7.8 renamed the data-exchange toolkits. Handle both.
if(OpenCASCADE_VERSION VERSION_GREATER_EQUAL 7.8)
  set(OCCT_STEP_LIBS TKDESTEP TKDESTL)
else()
  set(OCCT_STEP_LIBS TKSTEP TKSTL)
endif()

# ---------------------------------------------------------------------------
# Geometry core: pure OCCT, zero Qt. This is what makes the headless test work.
# ---------------------------------------------------------------------------
add_library(furnify_geometry STATIC
  src/ModelingOps.cpp
  src/DocumentModel.cpp
  src/SketchController.cpp
  src/CameraController.cpp
  src/Measure.cpp
  src/UserProgress.cpp
  src/FurnifySerial.cpp
)
target_include_directories(furnify_geometry PUBLIC
  src ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(furnify_geometry PUBLIC
  TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo
  TKBRep TKTopAlgo TKPrim TKBO TKBool TKFillet TKShHealing TKMesh
  TKXSBase ${OCCT_STEP_LIBS}
  # Binary shape (de)serialization for .furnify's shapes.bin - BinTools_ShapeSet
  # lives in the "light" binary-persistence toolkit, which needs no OCAF/document
  # machinery. Modeling-adjacent, not visualization - see CLAUDE.md's stack note.
  TKBinL
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_executable(headless_geometry tests/headless_geometry.cpp)
target_link_libraries(headless_geometry PRIVATE furnify_geometry)
add_test(NAME headless_geometry COMMAND headless_geometry)

add_executable(headless_sketch_document tests/sketch_document.cpp)
target_link_libraries(headless_sketch_document PRIVATE furnify_geometry)
add_test(NAME headless_sketch_document COMMAND headless_sketch_document)

add_executable(headless_camera tests/camera_controller.cpp)
target_link_libraries(headless_camera PRIVATE furnify_geometry)
add_test(NAME headless_camera COMMAND headless_camera)

add_executable(headless_measure tests/measure.cpp)
target_link_libraries(headless_measure PRIVATE furnify_geometry)
add_test(NAME headless_measure COMMAND headless_measure)

add_executable(headless_progress tests/user_progress.cpp)
target_link_libraries(headless_progress PRIVATE furnify_geometry)
add_test(NAME headless_progress COMMAND headless_progress)

add_executable(headless_direct_modeling tests/direct_modeling.cpp)
target_link_libraries(headless_direct_modeling PRIVATE furnify_geometry)
add_test(NAME headless_direct_modeling COMMAND headless_direct_modeling)

add_executable(headless_furnify_serial tests/furnify_serial.cpp)
target_link_libraries(headless_furnify_serial PRIVATE furnify_geometry)
add_test(NAME headless_furnify_serial COMMAND headless_furnify_serial)

# ---------------------------------------------------------------------------
# Application (Qt + OCCT visualization)
# ---------------------------------------------------------------------------
if(FURNIFYME_BUILD_APP)
  # OpenGLWidgets since the QOpenGLWidget migration: the viewport is no longer a
  # native OS window OCCT owns, it is a QOpenGLWidget whose FBO OCCT renders into
  # (see OcctViewWidget's own header). Qt6::Widgets does not pull that module in.
  find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
  set(CMAKE_AUTOMOC ON)

  set(APP_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/OcctViewWidget.cpp
  )


  # Sources land here as Milestone 1 progresses; skip the target until they exist
  # so a fresh clone still configures and runs the headless test.
  set(APP_SOURCES_PRESENT TRUE)
  foreach(f IN LISTS APP_SOURCES)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
      set(APP_SOURCES_PRESENT FALSE)
    endif()
  endforeach()

  if(APP_SOURCES_PRESENT)
    # The UI lives in a library so the GUI smoke test can drive the real
    # MainWindow instead of a reimplementation of it. `furnifyme` is then just
    # main.cpp on top.
    # The bundled font is compiled into the binary rather than loaded from a
    # path, so the app stays self-contained wherever it is copied.
    set(CMAKE_AUTORCC ON)

    add_library(furnify_app STATIC
      resources.qrc
      src/MainWindow.cpp
      src/EditorSelectorHandoff.cpp
      src/OcctViewWidget.cpp
      src/GridRenderer.cpp
      src/ui/AppBar.cpp
      src/ui/AppearancePanel.cpp
      src/ui/AxisGizmo.cpp
      src/ui/DimensionRenderer.cpp
      src/ui/Theme.cpp
      src/ui/IconSet.cpp
      src/ui/ToolChip.cpp
      src/ui/ToolCluster.cpp
      src/ui/ViewportOverlay.cpp
      src/ui/ItemsPanel.cpp
      src/ui/ShortcutSheet.cpp
      src/ui/WalkthroughPanel.cpp
      src/ui/HintBalloon.cpp
      src/ui/Toast.cpp
      src/ui/ExtrudePreview.cpp
      src/ui/PullArrow.cpp
      src/ui/BevelArrow.cpp
      src/ui/TransformGizmo.cpp
      src/ui/InlineRename.cpp
      src/ui/SelectorWindow.cpp
      src/ui/VersionsPanel.cpp
      src/ui/RenderSettingsPanel.cpp
      src/FurnitureStore.cpp
    )
    target_include_directories(furnify_app PUBLIC src/ui)
    target_link_libraries(furnify_app PUBLIC
      furnify_geometry
      Qt6::Widgets
      Qt6::OpenGLWidgets
      TKService TKV3d TKOpenGl
    )
    if(WIN32)
      # wglGetCurrentDC, for the one question OpenGl_Window has to be able to
      # answer about the context Qt handed us - see currentGlNativeWindow() in
      # OcctViewWidget.cpp. Qt links opengl32 for its own use but does not
      # export it to consumers.
      target_link_libraries(furnify_app PUBLIC opengl32)
    endif()

    add_executable(furnifyme src/main.cpp)
    target_link_libraries(furnifyme PRIVATE furnify_app)

    # The executable's own icon. QApplication::setWindowIcon covers everything
    # that runs our code; Explorer, the taskbar's pinned entry and Alt+Tab read
    # the binary itself, so the .ico has to be a linked resource as well. The
    # path is substituted absolute because rc.exe resolves relative ones
    # against a working directory we do not control - see assets/app.rc.in.
    if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
      set(FURNIFYME_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
      configure_file(assets/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
      enable_language(RC)
      target_sources(furnifyme PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
      # The .rc names the .ico but does not #include it, so no dependency
      # scanner can see the edge: repainting the icon would leave the linked
      # resource stale until something else happened to force a recompile.
      # Stated explicitly instead.
      set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
                   APPEND PROPERTY OBJECT_DEPENDS "${FURNIFYME_ICON_FILE}")
    endif()

    # Repaints assets/icon.ico from IconSet::appIcon() - the same painting the
    # running window uses. Off by default: the icon is a committed asset, and
    # this exists so it stays reproducible rather than so it is rebuilt.
    # See tools/make_icon.cpp for how to run it.
    option(FURNIFYME_BUILD_ICON_TOOL "Build the tool that repaints assets/icon.ico" OFF)
    if(FURNIFYME_BUILD_ICON_TOOL)
      add_executable(make_icon tools/make_icon.cpp)
      target_link_libraries(make_icon PRIVATE furnify_app)
    endif()

    # Drives the real widgets with synthetic Qt events delivered straight to
    # them, so it never touches the OS cursor or steals focus - the machine
    # stays usable while it runs. Not registered with ctest: it needs a GPU and
    # a window server, unlike the headless tests.
    add_executable(gui_smoke tests/gui_smoke.cpp)
    target_link_libraries(gui_smoke PRIVATE furnify_app)

    # Qt refuses to start without a platform plugin, and it is looked up in a
    # `platforms/` directory next to the executable rather than alongside the
    # Qt DLLs. vcpkg's applocal deployment copies DLLs but not plugins, and the
    # `windeployqt` feature is deliberately not installed (see CLAUDE.md), so
    # copy the ones we need ourselves. Skip this and the app dies at startup
    # with "could not find the Qt platform plugin".
    set(QT_RUNTIME_PLUGINS Qt6::QWindowsIntegrationPlugin)
    if(TARGET Qt6::QModernWindowsStylePlugin)
      list(APPEND QT_RUNTIME_PLUGINS Qt6::QModernWindowsStylePlugin)
    endif()

    foreach(plugin IN LISTS QT_RUNTIME_PLUGINS)
      if(NOT TARGET ${plugin})
        continue()
      endif()
      # Qt records each plugin's subdirectory (platforms, styles, ...) on the target.
      get_target_property(plugin_type ${plugin} QT_PLUGIN_TYPE)
      if(NOT plugin_type)
        set(plugin_type platforms)
      endif()
      add_custom_command(TARGET furnifyme POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:furnifyme>/${plugin_type}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${plugin}>"
                "$<TARGET_FILE_DIR:furnifyme>/${plugin_type}/"
        COMMENT "Deploying Qt plugin ${plugin} -> ${plugin_type}/")
    endforeach()
  else()
    message(STATUS "FurnifyMe: app sources not written yet - building geometry + tests only")
  endif()
endif()
