INCLUDE_DIRECTORIES(.)

LINK_DIRECTORIES(${BUILD_DIR}/src)

SET(NebulaAI_SRCS
     NebulaAI.hpp
     NebulaAI.cpp
)

ADD_LIBRARY(NebulaAI-static STATIC ${NebulaAI_SRCS})
# The library target "NebulaAI-static" has a default OUTPUT_NAME of "NebulaAI-static", so change it.
SET_TARGET_PROPERTIES(NebulaAI-static PROPERTIES OUTPUT_NAME "NebulaAI")
TARGET_LINK_LIBRARIES(NebulaAI-static Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui)
SET_TARGET_PROPERTIES(NebulaAI-static PROPERTIES COMPILE_FLAGS "-DQT_STATICPLUGIN")
ADD_DEPENDENCIES(AllStaticPlugins NebulaAI-static)

SET_TARGET_PROPERTIES(NebulaAI-static PROPERTIES FOLDER "plugins/NebulaAI")
