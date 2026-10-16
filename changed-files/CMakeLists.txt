cmake_minimum_required(VERSION 3.25)

project(OStrands VERSION 0.7.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(COMMONLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE-NG")
if(NOT EXISTS "${COMMONLIB_DIR}/CMakeLists.txt")
	message(FATAL_ERROR "CommonLibSSE-NG is missing. From the project folder run:\n"
		"  git clone --depth 1 --branch v12.1.0 https://github.com/alandtse/CommonLibSSE-NG extern/CommonLibSSE-NG")
endif()

set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${COMMONLIB_DIR}" CommonLibSSE EXCLUDE_FROM_ALL)
include("${COMMONLIB_DIR}/cmake/CommonLibSSE.cmake")

add_commonlibsse_plugin(
	${PROJECT_NAME}
	NAME "OStrands"
	AUTHOR "Saint"
	SOURCES
		src/main.cpp
		src/ExternalDrips.cpp
		src/Anatomy.cpp
		src/AutoPairs.cpp
		src/Body.cpp
		src/FluidManager.cpp
		src/Hooks.cpp
		src/OCumBridge.cpp
		src/OStimBridge.cpp
		src/SexLabBridge.cpp
		src/Settings.cpp
		src/SettingsMenu.cpp
		src/StrandManager.cpp
		src/fluid/Clusters.cpp
		src/fluid/Ground.cpp
		src/fluid/Skin.cpp
		src/fluid/Solver.cpp
		src/fluid/Surface.cpp
)

find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h" REQUIRED)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "0_OStrands")
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(${PROJECT_NAME} PRIVATE src extern/ostim ${SIMPLEINI_INCLUDE_DIRS})
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
