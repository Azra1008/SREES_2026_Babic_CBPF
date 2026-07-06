set(CBPLUGIN_NAME cbpf)   # naziv plugina (dylib: libcbpf.dylib)

file(GLOB CBPLUGIN_SOURCES  ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)
file(GLOB CBPLUGIN_INCS     ${CMAKE_CURRENT_LIST_DIR}/src/*.h)
file(GLOB CBPLUGIN_INC_GUI  ${NATID_SDK_INC}/gui/*.h)
file(GLOB CBPLUGIN_INC_TD   ${NATID_SDK_INC}/td/*.h)
file(GLOB CBPLUGIN_INC_SC   ${NATID_SDK_INC}/sc/*.h)
file(GLOB CBPLUGIN_INC_FO   ${NATID_SDK_INC}/fo/*.h)
file(GLOB CBPLUGIN_INC_ARCH ${NATID_SDK_INC}/arch/*.h)

add_library(${CBPLUGIN_NAME} SHARED ${CBPLUGIN_SOURCES} ${CBPLUGIN_INCS}
			${CBPLUGIN_INC_GUI} ${CBPLUGIN_INC_TD} ${CBPLUGIN_INC_SC} ${CBPLUGIN_INC_FO} ${CBPLUGIN_INC_ARCH})

source_group("src"      FILES ${CBPLUGIN_SOURCES})
source_group("inc"      FILES ${CBPLUGIN_INCS})
source_group("inc\\gui" FILES ${CBPLUGIN_INC_GUI})
source_group("inc\\sc"  FILES ${CBPLUGIN_INC_SC})
source_group("inc\\arch" FILES ${CBPLUGIN_INC_ARCH})

target_link_libraries(${CBPLUGIN_NAME} debug ${MU_LIB_DEBUG} optimized ${MU_LIB_RELEASE}
									   debug ${MATRIX_LIB_DEBUG} optimized ${MATRIX_LIB_RELEASE}
									   debug ${NATGUI_LIB_DEBUG} optimized ${NATGUI_LIB_RELEASE})

target_compile_definitions(${CBPLUGIN_NAME} PUBLIC PLUGIN_EXPORTS)

setIDEPropertiesForLib(${CBPLUGIN_NAME})
