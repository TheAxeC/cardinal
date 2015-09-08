# Minimum required version
CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
# Required to build a shared library on linux
SET_PROPERTY(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)
# Some rubbish for Cygwin
set(CMAKE_LEGACY_CYGWIN_WIN32 0)

# Set the used directories
set(ROOT_DIR "${CMAKE_BINARY_DIR}/../../" )

# The src folder
set(SRC_DIR "src/")

# folder containing all files for the command line interface
set(CLI_DIR "cli/")
# folder containing all files for the virtual machine
set(VM_DIR "vm/")

set(INC_DIR "include/" CACHE PATH "The path to the include folder")
set(LIB_DIR "lib/")
set(BIN_DIR "bin/")

# Set if you want to use C or C++
set(USE_C 0)

# The version number.
set (Script_VERSION_MAJOR 1)
set (Script_VERSION_MINOR 0)

# Set the include directories
include_directories(SYSTEM ${ROOT_DIR}/${INC_DIR} ${ROOT_DIR}/${SRC_DIR})

# Find all source files
FILE(GLOB SRCFILES ${ROOT_DIR}/${SRC_DIR}/${VM_DIR}/*.c)
FILE(GLOB CLIFILES ${ROOT_DIR}/${SRC_DIR}/${CLI_DIR}/*.c)

# Set the output directories
SET(EXECUTABLE_OUTPUT_PATH ${ROOT_DIR}/${BIN_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${ROOT_DIR}/${BIN_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${ROOT_DIR}/${LIB_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${ROOT_DIR}/${LIB_DIR})

# Set the executable type
if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
	set(NAME cardinald)
	set(EXEC cardinal-executable-debug)
	set(SLIB cardinal-shared-library-debug)
	set(LIB cardinal-static-library-debug)
else()
	set(NAME cardinal)
	set(EXEC cardinal-executable)
	set(SLIB cardinal-shared-library)
	set(LIB cardinal-static-library)
endif (${CMAKE_BUILD_TYPE} STREQUAL "Debug")

# Build the executable
ADD_EXECUTABLE(${EXEC} ${SRCFILES} ${CLIFILES})
# Build the Shared library
ADD_LIBRARY(${SLIB} SHARED ${SRCFILES})
# Build the static library
ADD_LIBRARY(${LIB} STATIC ${SRCFILES})

# Ensures that the libraries and exe have the same name
SET_TARGET_PROPERTIES(${EXEC} PROPERTIES OUTPUT_NAME ${NAME})
SET_TARGET_PROPERTIES(${SLIB} PROPERTIES OUTPUT_NAME ${NAME})
SET_TARGET_PROPERTIES(${LIB} PROPERTIES OUTPUT_NAME ${NAME})

# Set the debug flags
set(DEBUG_FLAGS "-DDEBUG")
# Set the release flags
set(RELEASE_FLAGS "-O3 -s")

if (USE_C) 
	# Set the project name
	project("Cardinal" C)
	# Set c warnings
	set(warnings "-std=c99 -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast")
	# link the libraries to the executable
	target_link_libraries (${EXEC} m)
	# Set the debug flags
	set(CMAKE_C_FLAGS_DEBUG ${DEBUG_FLAGS})
	# Set the release flags
	set(CMAKE_C_FLAGS_RELEASE ${RELEASE_FLAGS})
else()
	# Set the project name
	project("Cardinal")
	# Set c++ warnings
	set(warnings "-std=c++98")
	# Set the debug flags
	set(CMAKE_CXX_FLAGS_DEBUG ${DEBUG_FLAGS})
	# Set the release flags
	set(CMAKE_CXX_FLAGS_RELEASE ${RELEASE_FLAGS})
	# Force the cpp compiler
	SET(CMAKE_C_COMPILER ${CMAKE_CXX_COMPILER})
endif (USE_C) 

# Set warnings
set(warnings ${warnings}
	"-c -Wall -Winline -Wextra -Wswitch-default"
	"-Wmissing-declarations -Wcast-qual -Wpointer-arith"
	" -Wmissing-include-dirs -Wunreachable-code -Wundef"
	"-Wcast-align -Winit-self -Wshadow -Wno-variadic-macros"
)

add_definitions (${warnings})

