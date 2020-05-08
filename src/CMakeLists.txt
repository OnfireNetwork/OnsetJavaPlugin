set(PLUGIN_CONFIG_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/config_headers")

configure_file(
	"version.hpp.in"
	"${PLUGIN_CONFIG_INCLUDE_DIR}/version.hpp"
	@ONLY
)

add_library(OnsetJavaPlugin MODULE
	JavaEnv.cpp
	JavaEnv.hpp
	Plugin.cpp
	Plugin.hpp
	PluginInterface.cpp
	Singleton.hpp
)

find_package(Java REQUIRED)
find_package(JNI REQUIRED)
include(UseJava)

target_include_directories(OnsetJavaPlugin PRIVATE 
	${PLUGIN_CONFIG_INCLUDE_DIR}
	${HORIZONSDK_INCLUDE_DIR}
	${JNI_INCLUDE_DIRS}
)

set_property(TARGET OnsetJavaPlugin PROPERTY PREFIX "")

set_property(TARGET OnsetJavaPlugin PROPERTY CXX_STANDARD 17)
set_property(TARGET OnsetJavaPlugin PROPERTY CXX_STANDARD_REQUIRED ON)

if(MSVC)
	target_compile_definitions(OnsetJavaPlugin PRIVATE
		_CRT_SECURE_NO_WARNINGS
		NOMINMAX
	)
endif()

# increase warning levels
if (MSVC)
	target_compile_options(OnsetJavaPlugin PRIVATE
		/W4
		/WX
		/permissive-
		/wd4505
	)
elseif(UNIX)
	target_compile_options(OnsetJavaPlugin PRIVATE
		-Wall
		-Wextra
		-pedantic
		-Werror
		-Wno-unused-variable
		-Wno-unused-function
	)
endif()

target_link_libraries(OnsetJavaPlugin ${HORIZONSDK_LIBRARY} ${JNI_LIBRARIES})

if(UNIX)
	if(NOT APPLE)
		target_link_libraries(OnsetJavaPlugin rt)
	endif()
	
	set_target_properties(OnsetJavaPlugin PROPERTIES
		INSTALL_RPATH "$ORIGIN/..:$ORIGIN/")
endif()

install(TARGETS OnsetJavaPlugin DESTINATION "plugins/")

set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
if(WIN32)
	set(CPACK_GENERATOR ZIP)
else()
	set(CPACK_GENERATOR TGZ)
endif()

include(CPack)
