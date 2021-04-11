project(downstream-keyer VERSION 0.0.2)
set(PROJECT_FULL_NAME "Downstream keyer")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/version.h)

set(downstream-keyer_HEADERS
	downstream-keyer-dock.hpp
	downstream-keyer.hpp
	version.h)
set(downstream-keyer_SOURCES
	downstream-keyer-dock.cpp
	downstream-keyer.cpp)

if(WIN32)
	get_filename_component(ISS_FILES_DIR "${CMAKE_BINARY_DIR}\\..\\package" ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_FILES_DIR}" ISS_FILES_DIR)
	get_filename_component(ISS_PACKAGE_DIR "${CMAKE_PACKAGE_PREFIX}\\.." ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_PACKAGE_DIR}" ISS_PACKAGE_DIR)
	get_filename_component(ISS_SOURCE_DIR "${PROJECT_SOURCE_DIR}" ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_SOURCE_DIR}" ISS_SOURCE_DIR)
	configure_file("installer.iss.in"
			"${PROJECT_BINARY_DIR}/installer.iss"
		)
	configure_file(resource.rc.in downstream-keyer.rc)
	list(APPEND downstream-keyer_SOURCES
		downstream-keyer.rc)
endif()

add_library(downstream-keyer MODULE
	${downstream-keyer_HEADERS}
	${downstream-keyer_SOURCES})

target_link_libraries(downstream-keyer
		obs-frontend-api
		Qt5::Widgets
		libobs)

set_target_properties(downstream-keyer PROPERTIES FOLDER "plugins/exeldro")
install_obs_plugin_with_data(downstream-keyer data)
