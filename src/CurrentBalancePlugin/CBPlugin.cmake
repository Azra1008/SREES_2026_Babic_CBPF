set(CBPLUGIN_NAME cbpf)				#Naziv prvog projekta u solution-u

file(GLOB CBPLUGIN_CPP_COMMON_SOURCES  ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)
file(GLOB CBPLUGIN_CPP_COMMON_INCS  ${CMAKE_CURRENT_LIST_DIR}/src/*.h)
file(GLOB CBPLUGIN_INC_GUI  ${NATID_SDK_INC}/gui/*.h)
file(GLOB CBPLUGIN_INC_TD  ${NATID_SDK_INC}/td/*.h)
file(GLOB CBPLUGIN_INC_CNT  ${NATID_SDK_INC}/cnt/*.h)
file(GLOB CBPLUGIN_INC_MU  ${NATID_SDK_INC}/mu/*.h)
file(GLOB CBPLUGIN_INC_MEM  ${NATID_SDK_INC}/mem/*.h)
file(GLOB CBPLUGIN_INC_FO ${NATID_SDK_INC}/fo/*.h)
file(GLOB CBPLUGIN_INC_SC ${NATID_SDK_INC}/sc/*.h)
file(GLOB CBPLUGIN_INC_SYST ${NATID_SDK_INC}/syst/*.h)
file(GLOB CBPLUGIN_INC_DENSE ${NATID_SDK_INC}/dense/*.h)
file(GLOB CBPLUGIN_INC_SPARSE ${NATID_SDK_INC}/sparse/*.h)

# add shared library (plugin is a shared executatable binary file)
add_library(${CBPLUGIN_NAME} SHARED ${CBPLUGIN_CPP_COMMON_SOURCES} ${CBPLUGIN_INC_GUI} ${CBPLUGIN_CPP_COMMON_INCS}
							${CBPLUGIN_INC_TD} ${CBPLUGIN_INC_SYST}
							${CBPLUGIN_INC_CNT} ${CBPLUGIN_INC_MU} ${CBPLUGIN_INC_MEM} ${CBPLUGIN_INC_FO}
							${CBPLUGIN_INC_SC} ${CBPLUGIN_INC_DENSE} ${CBPLUGIN_INC_SPARSE})

source_group("inc\\inc"        FILES ${CBPLUGIN_CPP_COMMON_INCS})
source_group("inc\\gui"        FILES ${CBPLUGIN_INC_GUI})
source_group("inc\\td"        FILES ${CBPLUGIN_INC_TD})
source_group("inc\\cnt"        FILES ${CBPLUGIN_INC_CNT})
source_group("inc\\dense"        FILES ${CBPLUGIN_INC_DENSE})
source_group("inc\\mu"        FILES ${CBPLUGIN_INC_MU})
source_group("inc\\mem"        FILES ${CBPLUGIN_INC_MEM})
source_group("inc\\fo"        FILES ${CBPLUGIN_INC_FO})
source_group("inc\\sc"        FILES ${CBPLUGIN_INC_SC})
source_group("inc\\sparse"        FILES ${CBPLUGIN_INC_SPARSE})
source_group("inc\\syst"        FILES ${CBPLUGIN_INC_SYST})

source_group("src\\cpp"			FILES ${CBPLUGIN_CPP_COMMON_SOURCES})

target_link_libraries(${CBPLUGIN_NAME} debug ${MU_LIB_DEBUG} optimized ${MU_LIB_RELEASE}
										debug ${MATRIX_LIB_DEBUG} optimized ${MATRIX_LIB_RELEASE}
									  debug ${NATGUI_LIB_DEBUG} optimized ${NATGUI_LIB_RELEASE})

target_compile_definitions(${CBPLUGIN_NAME} PUBLIC PLUGIN_EXPORTS)

setIDEPropertiesForLib(${CBPLUGIN_NAME})
