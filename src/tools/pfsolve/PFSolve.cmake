set(PFSOLVE_NAME pfsolve)
file(GLOB PFSOLVE_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)
file(GLOB PFSOLVE_INC_SC ${NATID_SDK_INC}/sc/*.h)
add_executable(${PFSOLVE_NAME} ${PFSOLVE_SOURCES} ${PFSOLVE_INC_SC})
target_link_libraries(${PFSOLVE_NAME} debug ${MU_LIB_DEBUG} optimized ${MU_LIB_RELEASE}
									  debug ${MATRIX_LIB_DEBUG} optimized ${MATRIX_LIB_RELEASE}
									  debug ${MODSOLVER_LIB_DEBUG} optimized ${MODSOLVER_LIB_RELEASE})
setIDEPropertiesForExecutable(${PFSOLVE_NAME})
