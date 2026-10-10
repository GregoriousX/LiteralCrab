cmake_minimum_required(VERSION 3.21)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" LITERALCRAB_VERSION LIMIT_COUNT 1)
string(STRIP "${LITERALCRAB_VERSION}" LITERALCRAB_VERSION)
project(LiteralCrab VERSION ${LITERALCRAB_VERSION} LANGUAGES C CXX)

# The DI1 target and its tests share the vendored x86 MinHook implementation.
include(di1/CMakeLists.txt)
