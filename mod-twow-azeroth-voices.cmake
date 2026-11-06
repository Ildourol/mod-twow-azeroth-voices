# Azeroth Voices uses the core's vendored cpp-httplib in HTTPS mode. The final
# mangosd already links OpenSSL, but a dynamically linked module needs the same
# dependency explicitly. Applying this to both possible targets also makes the
# static relationship visible and avoids platform-specific transitive-linking
# surprises.
if(TORTOISE_MODULE_CMAKE_PHASE STREQUAL "POST_TARGETS")
  # Azeroth Voices uses the core's vendored cpp-httplib in HTTPS mode.
  # Comply strictly with the server's bundled OpenSSL 1.1 architecture:
  # inject the core's bundled OpenSSL headers ahead of any external toolchain
  # (e.g. vcpkg) to ensure binary compatibility with the server engine.
  if(TORTOISE_CURRENT_MODULE_LINKAGE STREQUAL "dynamic")
    GetModuleProjectName("${TORTOISE_CURRENT_MODULE}" AV_TARGET)
  else()
    set(AV_TARGET modules)
  endif()

  if(TARGET ${AV_TARGET})
    if(WIN32)
      target_include_directories(${AV_TARGET} BEFORE PRIVATE
        "${CMAKE_SOURCE_DIR}/dep/windows/include/zlib"
        "${CMAKE_SOURCE_DIR}/dep/windows/include"
        "${CMAKE_SOURCE_DIR}/dep/include-windows")
      target_compile_definitions(${AV_TARGET} PRIVATE ZLIB_INTERNAL)
      if(OPENSSL_DEBUG_LIBRARIES)
        target_link_libraries(${AV_TARGET} PUBLIC
          $<$<CONFIG:Debug>:${OPENSSL_DEBUG_LIBRARIES}>
          $<$<NOT:$<CONFIG:Debug>>:${OPENSSL_LIBRARIES}>)
      else()
        target_link_libraries(${AV_TARGET} PUBLIC ${OPENSSL_LIBRARIES})
      endif()
    else()
      target_include_directories(${AV_TARGET} BEFORE PRIVATE
        ${OPENSSL_INCLUDE_DIR})
      target_link_libraries(${AV_TARGET} PUBLIC ${OPENSSL_LIBRARIES})
      if(OPENSSL_EXTRA_LIBRARIES)
        target_link_libraries(${AV_TARGET} PUBLIC ${OPENSSL_EXTRA_LIBRARIES})
      endif()
    endif()
    target_include_directories(${AV_TARGET} PRIVATE
      "${CMAKE_SOURCE_DIR}/src/game/MapNodes")
  endif()

  install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/data/"
    DESTINATION "modules/mod-azeroth-voices/data")
  # The native Turtle WoW AzerothVoices addon is shipped so an operator can
  # copy client/AzerothVoices into the client's Interface/AddOns directory.
  # The module answers both native .avaddon frames and legacy .llmc requests.
  install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/client/"
    DESTINATION "modules/mod-azeroth-voices/client")
  if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/conf/presets/mod-azeroth-voices-quieter.conf.dist")
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/conf/presets/mod-azeroth-voices-quieter.conf.dist"
      DESTINATION "modules/mod-azeroth-voices/conf/presets")
  endif()
  install(FILES "${CMAKE_CURRENT_LIST_DIR}/README.md"
    DESTINATION "modules/mod-azeroth-voices")
  if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/docs/CONFIGURATION_GUIDE.md")
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/docs/CONFIGURATION_GUIDE.md"
      DESTINATION "modules/mod-azeroth-voices/docs")
  endif()

  # Focused, engine-free unit tests. BUILD_TESTING is honoured when the core
  # enables CTest; AZEROTH_VOICES_BUILD_TESTS lets a module-only build opt in
  # without changing the core.
  option(AZEROTH_VOICES_BUILD_TESTS "Build the mod-azeroth-voices unit tests." OFF)
  if(BUILD_TESTING OR AZEROTH_VOICES_BUILD_TESTS)
    enable_testing()
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.19)
      # The core calls enable_testing() only when its own test switch is on, so
      # `ctest --test-dir <build>` at the build root would otherwise see no
      # tests. Turning it on for the top-level directory keeps both
      # `ctest --test-dir <build>` and `ctest --test-dir <build>/modules`
      # working without touching the core.
      cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL enable_testing)
    endif()
    add_executable(azeroth_voices_tests
      "${CMAKE_CURRENT_LIST_DIR}/tests/HttpPolicyTests.cpp")
    set_target_properties(azeroth_voices_tests PROPERTIES FOLDER "modules")
    add_test(NAME azeroth_voices_tests COMMAND azeroth_voices_tests)
    set_property(TARGET azeroth_voices_tests PROPERTY CXX_STANDARD 17)
  endif()
endif()
