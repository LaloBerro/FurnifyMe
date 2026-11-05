cmake_minimum_required(VERSION 3.20)
project(FurnifyMe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Milestone 1 sequencing (see CLAUDE.md): the headless geometry test must build,
# run and pass before any Qt code exists. Turn the app off to build tests alone
# on a machine that has OCCT but no Qt:  -DFURNIFYME_BUILD_APP=OFF
option(FURNIFYME_BUILD_APP "Build the Qt desktop application" ON)
# The HTML UI (migration Phases 0-2): pages in Qt WebEngine - the hub today,
# the editor's chrome later. Needs a Qt that ships WebEngine - the installer
# build in C:/Qt, see the windows-web preset - and Node for the page build.
# Off, the app is exactly the Qt Widgets app.
option(FURNIFYME_WEB_UI "Build the HTML UI (Qt WebEngine pages)" OFF)

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
  src/Joinery.cpp
  src/SceneModel.cpp
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

add_executable(headless_joinery tests/joinery.cpp)
target_link_libraries(headless_joinery PRIVATE furnify_geometry)
add_test(NAME headless_joinery COMMAND headless_joinery)

add_executable(headless_mitre tests/mitre.cpp)
target_link_libraries(headless_mitre PRIVATE furnify_geometry)
add_test(NAME headless_mitre COMMAND headless_mitre)

add_executable(headless_measured_box tests/measured_box.cpp)
target_link_libraries(headless_measured_box PRIVATE furnify_geometry)
add_test(NAME headless_measured_box COMMAND headless_measured_box)

add_executable(headless_scene_model tests/scene_model.cpp)
target_link_libraries(headless_scene_model PRIVATE furnify_geometry)
add_test(NAME headless_scene_model COMMAND headless_scene_model)

add_executable(headless_resize_axis tests/resize_axis.cpp)
target_link_libraries(headless_resize_axis PRIVATE furnify_geometry)
add_test(NAME headless_resize_axis COMMAND headless_resize_axis)

add_executable(headless_boolean_multi tests/boolean_multi.cpp)
target_link_libraries(headless_boolean_multi PRIVATE furnify_geometry)
add_test(NAME headless_boolean_multi COMMAND headless_boolean_multi)

add_executable(headless_item_order tests/item_order.cpp)
target_link_libraries(headless_item_order PRIVATE furnify_geometry)
add_test(NAME headless_item_order COMMAND headless_item_order)

# Hex lives only in Theme.cpp (the 2026-10 review's F19): reads src/ as text,
# links nothing - no OCCT, no Qt - so it runs wherever the others do.
add_executable(headless_colour_literals tests/colour_literals.cpp)
target_compile_definitions(headless_colour_literals PRIVATE
  FURNIFY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")
add_test(NAME headless_colour_literals COMMAND headless_colour_literals)

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

    # The editor session (migration Phase 1): the document, its checkpoints,
    # the outcome channel and the action registry - everything the HTML layer
    # will talk to. Core and Gui ONLY - and session_no_widgets below is what
    # actually enforces that, since linking alone does not.
    find_package(Qt6 REQUIRED COMPONENTS Core Gui)
    add_library(furnify_session STATIC
      src/session/EditorSession.cpp
      src/session/EditorSession.h
      src/session/Outcome.h
      src/session/ActionRegistry.cpp
      src/session/ActionRegistry.h
      src/session/ActionWatch.cpp
      src/session/ActionWatch.h
      # The library on disk - Core + Gui only, so the hub's model can sit
      # beside it (Phase 2).
      src/FurnitureStore.cpp
      src/FurnitureStore.h
      src/session/HubModel.cpp
      src/session/HubModel.h
      # The Versions drawer's model (Phase 3c).
      src/session/VersionsModel.cpp
      src/session/VersionsModel.h
      # The Joints drawer's content and model (Phase 3d).
      src/session/JointRows.cpp
      src/session/JointRows.h
      src/session/JointsModel.cpp
      src/session/JointsModel.h
      # The Items drawer's words, key table and model (Phase 3h).
      src/session/ItemMoveText.cpp
      src/session/ItemMoveText.h
      src/session/ItemsKeys.cpp
      src/session/ItemsKeys.h
      src/session/ItemsModel.cpp
      src/session/ItemsModel.h
      # The menu bar's model (Phase 3i).
      src/session/MenuModel.cpp
      src/session/MenuModel.h
    )
    target_include_directories(furnify_session PUBLIC src)
    target_link_libraries(furnify_session PUBLIC furnify_geometry Qt6::Core Qt6::Gui)

    # The no-Widgets rule above, enforced: a text scan of src/session (and the
    # project headers it reaches) against the QtWidgets include directory.
    # Linking Core + Gui alone does not enforce it - they export include/Qt6,
    # so <QtWidgets/QWidget> would still resolve.
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    get_target_property(qt_widgets_includes Qt6::Widgets INTERFACE_INCLUDE_DIRECTORIES)
    set(qt_widgets_include_dir "")
    foreach(dir IN LISTS qt_widgets_includes)
      if(dir MATCHES "/QtWidgets$")
        set(qt_widgets_include_dir "${dir}")
      endif()
    endforeach()
    add_executable(session_no_widgets tests/session_no_widgets.cpp)
    target_compile_definitions(session_no_widgets PRIVATE
      FURNIFY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src"
      FURNIFY_QTWIDGETS_INCLUDE_DIR="${qt_widgets_include_dir}")
    add_test(NAME session_no_widgets COMMAND session_no_widgets)

    add_executable(hub_model_tests tests/hub_model.cpp)
    target_link_libraries(hub_model_tests PRIVATE furnify_session)
    add_test(NAME hub_model COMMAND hub_model_tests)

    add_executable(versions_model_tests tests/versions_model.cpp)
    target_link_libraries(versions_model_tests PRIVATE furnify_session)
    add_test(NAME versions_model COMMAND versions_model_tests)

    add_executable(joints_model_tests tests/joints_model.cpp)
    target_link_libraries(joints_model_tests PRIVATE furnify_session)
    add_test(NAME joints_model COMMAND joints_model_tests)

    add_executable(items_model_tests tests/items_model.cpp)
    target_link_libraries(items_model_tests PRIVATE furnify_session)
    add_test(NAME items_model COMMAND items_model_tests)

    add_executable(menu_model_tests tests/menu_model.cpp)
    target_link_libraries(menu_model_tests PRIVATE furnify_session)
    add_test(NAME menu_model COMMAND menu_model_tests)

    add_executable(session_tests tests/session.cpp)
    target_link_libraries(session_tests PRIVATE furnify_session)
    add_test(NAME session COMMAND session_tests)
    # The tests run on Qt's offscreen platform (no window, no GPU), which has
    # to sit in platforms/ beside the exe like every other Qt plugin here.
    if(TARGET Qt6::QOffscreenIntegrationPlugin)
      add_custom_command(TARGET session_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:session_tests>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
                "$<TARGET_FILE_DIR:session_tests>/platforms/")
    endif()

    add_library(furnify_app STATIC
      resources.qrc
      src/MainWindow.cpp
      src/EditorSelectorHandoff.cpp
      src/OcctViewWidget.cpp
      src/GridRenderer.cpp
      src/ui/AppBar.cpp
      src/ui/AppearancePanel.cpp
      src/ui/SettingsEdits.cpp
      src/ui/Themes.cpp
      src/ui/Themes.h
      src/ui/ThemeShell.cpp
      src/ui/ThemeShell.h
      src/ui/ThemeDecor.cpp
      src/ui/ThemeDecor.h
      src/ui/HtmlCardSlot.cpp
      src/web/SettingsBridge.cpp
      src/web/SettingsBridge.h
      src/web/RenderBridge.cpp
      src/web/RenderBridge.h
      src/ui/ChipFace.cpp
      src/ui/ChipFace.h
      src/web/ChipsBridge.cpp
      src/web/ChipsBridge.h
      src/ui/AxisGizmo.cpp
      src/ui/CardSlide.cpp
      src/ui/DimensionRenderer.cpp
      src/ui/JointRenderer.cpp
      src/ui/SelectionSizesRenderer.cpp
      src/ui/ResizePinRenderer.cpp
      src/ui/BooleanBadgeRenderer.cpp
      src/ui/BooleanTool.cpp
      src/ui/Theme.cpp
      src/ui/IconSet.cpp
      src/ui/ToolChip.cpp
      src/ui/ToolCluster.cpp
      src/ui/ViewportOverlay.cpp
      src/ui/ItemsPanel.cpp
      src/ui/ItemsRow.cpp
      src/ui/ItemsFilterField.cpp
      src/ui/DropIndicator.cpp
      src/ui/DragGhost.cpp
      src/ui/PanelCloseButton.cpp
      src/ui/NameFurnitureCard.cpp
      src/ui/LoadingCard.cpp
      src/ui/MaterialCard.cpp
      src/ui/ShapeFlyout.cpp
      src/ui/ShortcutSheet.cpp
      src/ui/WalkthroughPanel.cpp
      src/ui/HintBalloon.cpp
      src/ui/Toast.cpp
      src/ui/ExtrudePreview.cpp
      src/ui/PullArrow.cpp
      src/ui/BevelArrow.cpp
      src/ui/MitreTool.cpp
      src/ui/ReMeasureTool.cpp
      src/ui/SlatsTool.cpp
      src/ui/TransformGizmo.cpp
      src/ui/InlineRename.cpp
      src/ui/WindowChrome.cpp
      src/ui/SelectorWindow.cpp
      src/ui/VersionsPanel.cpp
      src/ui/JointChip.cpp
      src/ui/JointsPanel.cpp
      src/ui/RenderSettingsPanel.cpp
      src/ui/RenderSettingsCopy.cpp
      src/ui/RenderFrameGuides.cpp
      src/ui/RenderStudio.cpp
      src/ui/SceneWindow.cpp
      src/ui/ScenePiecesPanel.cpp
      src/ui/AddPieceCard.cpp
      src/ui/UnsavedCloseCard.cpp
    )
    target_include_directories(furnify_app PUBLIC src/ui)
    target_link_libraries(furnify_app PUBLIC
      furnify_geometry
      furnify_session
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

    if(FURNIFYME_WEB_UI)
      find_package(Qt6 REQUIRED COMPONENTS WebEngineWidgets WebChannel Quick Test)
      target_sources(furnify_app PRIVATE
        src/web/WebHost.cpp
        src/web/ThemeBridge.cpp
        src/web/HubWindow.cpp
        src/web/EditorChrome.cpp
        src/web/ShellBridge.cpp
        src/web/RenderPage.cpp
        src/web/WebOverlay.cpp
      )
      target_link_libraries(furnify_app PUBLIC Qt6::WebEngineWidgets Qt6::WebChannel Qt6::Quick)
      target_compile_definitions(furnify_app PUBLIC FURNIFYME_WEB_UI)

      # The pages: one self-contained file each (web/build.mjs), rebuilt when
      # any web source changes. Node does the build; nothing here needs it at
      # run time.
      find_program(NPM_EXECUTABLE NAMES npm.cmd npm REQUIRED)
      set(web_dir "${CMAKE_CURRENT_SOURCE_DIR}/web")
      # A clean checkout has no node_modules: `npm ci` first, again whenever
      # the lock file changes (a stamp records the last install).
      set(web_installed "${CMAKE_CURRENT_BINARY_DIR}/web-npm-ci.stamp")
      add_custom_command(
        OUTPUT "${web_installed}"
        COMMAND "${NPM_EXECUTABLE}" ci --no-audit --no-fund
        COMMAND ${CMAKE_COMMAND} -E touch "${web_installed}"
        WORKING_DIRECTORY "${web_dir}"
        DEPENDS "${web_dir}/package.json" "${web_dir}/package-lock.json"
        COMMENT "Installing the web build's packages (npm ci)")
      file(GLOB_RECURSE web_sources CONFIGURE_DEPENDS "${web_dir}/src/*" "${web_dir}/*.html")
      set(web_pages_built "${web_dir}/dist/hub/index.html" "${web_dir}/dist/overlay/index.html"
          "${web_dir}/dist/editor/index.html")
      add_custom_command(
        OUTPUT ${web_pages_built}
        COMMAND "${NPM_EXECUTABLE}" run build
        WORKING_DIRECTORY "${web_dir}"
        DEPENDS ${web_sources} "${web_dir}/build.mjs" "${web_installed}"
        COMMENT "Building the web pages")
      # The pages reach the exe directory HERE, every build, after the old
      # copy is cleared - not in an exe's POST_BUILD, which runs only when that
      # exe relinks, so a .svelte-only edit would otherwise never arrive (and
      # a page deleted from dist would linger forever).
      add_custom_target(web_pages ALL
        DEPENDS ${web_pages_built}
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/web"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${web_dir}/dist"
                "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/web"
        COMMENT "Deploying the web pages beside the executables")

      # The Phase 0 gate harness and its isolation probe; and the hub's
      # harness. Console subsystem, like gui_smoke, because they print.
      add_executable(web_spike tests/web_spike.cpp)
      target_link_libraries(web_spike PRIVATE furnify_app Qt6::Test)
      add_executable(web_probe tests/web_probe.cpp)
      target_link_libraries(web_probe PRIVATE furnify_app)
      add_executable(hub_smoke tests/hub_smoke.cpp)
      target_link_libraries(hub_smoke PRIVATE furnify_app)
      add_executable(chrome_smoke tests/chrome_smoke.cpp)
      target_link_libraries(chrome_smoke PRIVATE furnify_app Qt6::Test)
      if(WIN32)
        target_link_libraries(chrome_smoke PRIVATE dbghelp)
      endif()
      # The mockups' scene per theme, for the side-by-side (themes part 2).
      add_executable(theme_shots tests/theme_shots.cpp)
      target_link_libraries(theme_shots PRIVATE furnify_app)
    endif()

    add_executable(furnifyme src/main.cpp)
    target_link_libraries(furnifyme PRIVATE furnify_app)
    # main()'s crash reporter symbolizes its frames through DbgHelp.
    if(WIN32)
      target_link_libraries(furnifyme PRIVATE dbghelp)
    endif()

    # The Settings drawer's model and bridge (migration Phase 3b), offscreen
    # beside session_tests - whose platforms/ copy it shares.
    add_executable(settings_tests tests/settings_tests.cpp)
    target_link_libraries(settings_tests PRIVATE furnify_app)
    add_test(NAME settings COMMAND settings_tests)

    # The render settings panel's edit path and its page bridge (migration
    # Phase 3e), offscreen beside settings_tests.
    add_executable(render_bridge_tests tests/render_bridge.cpp)
    target_link_libraries(render_bridge_tests PRIVATE furnify_app)
    add_test(NAME render_bridge COMMAND render_bridge_tests)

    # The gesture chips' face and bridge (migration Phase 4a), offscreen.
    add_executable(chip_face_tests tests/chip_face.cpp)
    target_link_libraries(chip_face_tests PRIVATE furnify_app)
    add_test(NAME chip_face COMMAND chip_face_tests)

    # The bundled material textures (Milestone 5): every image in
    # assets/materials ships beside the executable, where MainWindow's scan
    # finds them alongside whatever the user drops into their own library
    # folder. copy_directory alone would fail the build the day the folder
    # is emptied, so it is guarded on existence.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/materials")
      add_custom_command(TARGET furnifyme POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/materials"
                "$<TARGET_FILE_DIR:furnifyme>/materials")
    endif()
    if(WIN32)
      # A GUI subsystem binary, so launching the app opens no console window
      # (Milestone 5, "dont show the command window"). The entry point is
      # forced back to the ordinary CRT main() rather than pulling in a
      # WinMain shim, so src/main.cpp needs no Windows-only signature and
      # gui_smoke - which PRINTS its results and so keeps the console
      # subsystem - stays byte-for-byte the same source. qWarning() output
      # goes nowhere in this mode, which is what a shipped GUI app does.
      set_target_properties(furnifyme PROPERTIES WIN32_EXECUTABLE TRUE)
      target_link_options(furnifyme PRIVATE "/ENTRY:mainCRTStartup")
    endif()

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
    # The suite's own stack-overflow reporter resolves its frames with DbgHelp
    # (see traceStackOverflow() in tests/gui_smoke.cpp) - there is no debugger
    # installed on this machine, and a crash whose location cannot be read
    # costs far more than this one link.
    if(WIN32)
      target_link_libraries(gui_smoke PRIVATE dbghelp)
      # A BIGGER STACK, and it is this file's own shape that needs it. main()
      # here is one function of some thirty thousand lines: MSVC allocates a
      # frame for every local in every block of it AT ENTRY, so the frame is
      # enormous and grows with each block added. Windows gives a thread 1 MB
      # by default, and the first GL context creation - a deep call through
      # the display driver - is what finally ran off the end of it: a stack
      # overflow at window.show(), 34 frames deep, in a stack with no
      # recursion anywhere in it. Reserve, not commit, so it costs address
      # space and nothing else.
      target_link_options(gui_smoke PRIVATE /STACK:33554432)
      # AND A BIGGER OBJECT FILE, for the same reason one step along. A COFF
      # object holds at most 65,279 sections, and MSVC emits sections per
      # function and per COMDAT - which for this file means per block, per
      # lambda and per string literal folded out of one thirty-thousand-line
      # main(). The folder-duplicate block is simply the one that crossed it:
      # `fatal error C1128: number of sections exceeded object file format
      # limit`, which names its own fix and is not a symptom of anything else.
      # /bigobj widens the section count to 2^32 and costs nothing but a
      # slightly larger .obj; it changes no code that is generated.
      target_compile_options(gui_smoke PRIVATE /bigobj)
    endif()

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
    # The JPEG image plugin (Milestone 5, material textures): QImage reads
    # JPEG through a plugin exactly as the window reads the platform through
    # one, and without imageformats/qjpeg.dll beside the exe every .jpg
    # material silently loaded null and fell back to the procedural grain -
    # the user's own report ("the 3 woods seems the same"). PNG needs no
    # entry: the app icon proves it decodes without a deployed plugin here.
    if(TARGET Qt6::QJpegPlugin)
      list(APPEND QT_RUNTIME_PLUGINS Qt6::QJpegPlugin)
      # The plugin's OWN dependency: vcpkg's Qt links libjpeg-turbo as a
      # separate jpeg62.dll, which vcpkg's applocal step never copies (it
      # walks the exe's imports, and only the plugin - loaded at runtime -
      # imports it). Without it qjpeg.dll fails to LOAD, silently, and every
      # .jpg still reads as null - the exact symptom deploying the plugin
      # alone was supposed to fix. Located beside Qt6Core.dll, which is
      # where vcpkg puts every runtime DLL. (Not in the WebEngine spike: the
      # installer Qt's plugin carries its own JPEG decoder.)
      if(NOT FURNIFYME_WEB_UI)
      add_custom_command(TARGET furnifyme POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE_DIR:Qt6::Core>/jpeg62.dll"
                "$<TARGET_FILE_DIR:furnifyme>/")
      endif()
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

    if(FURNIFYME_WEB_UI AND WIN32)
      # DEPLOYMENT FOR THE HTML UI. vcpkg's applocal step is OFF in this preset:
      # it resolves DLLs by name against vcpkg's own bin/, which also holds
      # vcpkg's Qt, and would copy THAT Qt6Core.dll beside an exe built
      # against the installer Qt. So: windeployqt for Qt and WebEngine, then
      # every vcpkg runtime DLL that is not Qt (OCCT and what it needs). The
      # pages are web_pages' own job (above).
      get_target_property(qt_qmake Qt6::qmake IMPORTED_LOCATION)
      get_filename_component(qt_bin "${qt_qmake}" DIRECTORY)
      set(windeployqt "${qt_bin}/windeployqt.exe")
      file(GLOB vcpkg_runtime_dlls "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/*.dll")
      list(FILTER vcpkg_runtime_dlls EXCLUDE REGEX "/Qt6[^/]*\.dll$")
      foreach(web_target furnifyme web_spike hub_smoke chrome_smoke settings_tests render_bridge_tests gui_smoke)
        add_dependencies(${web_target} web_pages)
        add_custom_command(TARGET ${web_target} POST_BUILD
          COMMAND "${windeployqt}" --release --no-translations --no-compiler-runtime
                  --no-opengl-sw --no-system-d3d-compiler --verbose 0
                  "$<TARGET_FILE:${web_target}>"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different ${vcpkg_runtime_dlls}
                  "$<TARGET_FILE_DIR:${web_target}>/"
          COMMENT "Deploying Qt WebEngine and OCCT beside ${web_target}")
      endforeach()
    endif()
  else()
    message(STATUS "FurnifyMe: app sources not written yet - building geometry + tests only")
  endif()
endif()
