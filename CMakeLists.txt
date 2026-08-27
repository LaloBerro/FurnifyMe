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
)
target_include_directories(furnify_geometry PUBLIC
  src ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(furnify_geometry PUBLIC
  TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo
  TKBRep TKTopAlgo TKPrim TKBO TKBool TKShHealing TKMesh
  TKXSBase ${OCCT_STEP_LIBS}
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_executable(headless_geometry tests/headless_geometry.cpp)
target_link_libraries(headless_geometry PRIVATE furnify_geometry)
add_test(NAME headless_geometry COMMAND headless_geometry)

# ---------------------------------------------------------------------------
# Application (Qt + OCCT visualization)
# ---------------------------------------------------------------------------
if(FURNIFYME_BUILD_APP)
  find_package(Qt6 REQUIRED COMPONENTS Widgets)
  set(CMAKE_AUTOMOC ON)

  set(APP_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/OcctViewWidget.cpp
    src/DocumentModel.cpp
    src/SketchController.cpp
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
    add_executable(furnifyme ${APP_SOURCES})
    target_link_libraries(furnifyme PRIVATE
      furnify_geometry
      Qt6::Widgets
      TKService TKV3d TKOpenGl
    )
  else()
    message(STATUS "FurnifyMe: app sources not written yet - building geometry + tests only")
  endif()
endif()
