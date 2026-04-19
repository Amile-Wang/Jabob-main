idf_component_register(
    SRCS ""
    INCLUDE_DIRS "include"
)

# 手動加入 libDSpotter.a
target_link_libraries(${COMPONENT_LIB} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/libDSpotter_Jabra_Trial.a)

target_link_options(${COMPONENT_LIB} INTERFACE -Wl,--gc-sections)