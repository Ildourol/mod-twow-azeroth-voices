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
endif()
