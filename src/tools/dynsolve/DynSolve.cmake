set(DYNSOLVE_NAME dynsolve)
file(GLOB DYNSOLVE_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)
file(GLOB DYNSOLVE_INC_SC ${NATID_SDK_INC}/sc/*.h)
add_executable(${DYNSOLVE_NAME} ${DYNSOLVE_SOURCES} ${DYNSOLVE_INC_SC})
target_link_libraries(${DYNSOLVE_NAME} debug ${MU_LIB_DEBUG} optimized ${MU_LIB_RELEASE}
									  debug ${MATRIX_LIB_DEBUG} optimized ${MATRIX_LIB_RELEASE}
									  debug ${MODSOLVER_LIB_DEBUG} optimized ${MODSOLVER_LIB_RELEASE})
setIDEPropertiesForExecutable(${DYNSOLVE_NAME})
