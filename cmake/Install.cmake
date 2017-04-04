set(CPACK_GENERATOR ZIP)

set(CPACK_PACKAGE_VENDOR "Big Ladder Software LLC")

set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")

if ( "${CMAKE_BUILD_TYPE}" STREQUAL "" OR "${CMAKE_BUILD_TYPE}" STREQUAL "Release" )
  set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION}-${KIVA_PACKAGE_CONFIG}")
else()
  set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION}-${KIVA_PACKAGE_CONFIG}-${CMAKE_BUILD_TYPE}")
endif()

# Normal release files LICENSE, Change log, and README
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION "./")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

install(FILES "${CMAKE_SOURCE_DIR}/ChangeLog.md" DESTINATION "./")

set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
install(FILES "${CMAKE_SOURCE_DIR}/README.md" DESTINATION "./")

# Examples and WeatherData
install(DIRECTORY "${CMAKE_SOURCE_DIR}/examples" DESTINATION "./")

install(DIRECTORY "${CMAKE_SOURCE_DIR}/weather" DESTINATION "./")

# Libraries
include(CPack)

set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
include(InstallRequiredSystemLibraries)
install(FILES ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION "./" COMPONENT Libraries)