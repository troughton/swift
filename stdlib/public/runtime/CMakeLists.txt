set(swift_runtime_compile_flags ${SWIFT_RUNTIME_CORE_CXX_FLAGS})
set(swift_runtime_linker_flags ${SWIFT_RUNTIME_CORE_LINK_FLAGS})

if(SWIFT_RUNTIME_CLOBBER_FREED_OBJECTS)
  list(APPEND swift_runtime_compile_flags
      "-DSWIFT_RUNTIME_CLOBBER_FREED_OBJECTS=1")
endif()

if(SWIFT_RUNTIME_CRASH_REPORTER_CLIENT)
  list(APPEND swift_runtime_compile_flags
      "-DSWIFT_HAVE_CRASHREPORTERCLIENT=1")
endif()

set(swift_runtime_leaks_sources)
if(SWIFT_RUNTIME_ENABLE_LEAK_CHECKER)
  list(APPEND swift_runtime_compile_flags
       "-DSWIFT_RUNTIME_ENABLE_LEAK_CHECKER=1")
  set(swift_runtime_leaks_sources Leaks.mm)
endif()

if(SWIFT_RUNTIME_ENABLE_COW_EXISTENTIALS)
  list(APPEND swift_runtime_compile_flags
    "-DSWIFT_RUNTIME_ENABLE_COW_EXISTENTIALS=1")
endif()

set(section_magic_compile_flags ${swift_runtime_compile_flags})

list(APPEND swift_runtime_compile_flags
     "-D__SWIFT_CURRENT_DYLIB=swiftCore")

set(swift_runtime_objc_sources
    ErrorObject.mm
    SwiftObject.mm
    SwiftValue.mm
    Reflection.mm
    "${SWIFT_SOURCE_DIR}/lib/Demangling/OldRemangler.cpp"
    "${SWIFT_SOURCE_DIR}/lib/Demangling/Remangler.cpp")

set(swift_runtime_sources
    AnyHashableSupport.cpp
    Casting.cpp
    CygwinPort.cpp
    Demangle.cpp
    Enum.cpp
    ErrorObjectConstants.cpp
    ErrorObjectNative.cpp
    Errors.cpp
    ErrorDefaultImpls.cpp
    Exclusivity.cpp
    Heap.cpp
    HeapObject.cpp
    ImageInspectionMachO.cpp
    ImageInspectionELF.cpp
    ImageInspectionWin32.cpp
    KnownMetadata.cpp
    Metadata.cpp
    MetadataLookup.cpp
    MutexPThread.cpp
    MutexWin32.cpp
    Once.cpp
    Portability.cpp
    ProtocolConformance.cpp
    RefCount.cpp
    RuntimeEntrySymbols.cpp
    "${SWIFT_SOURCE_DIR}/lib/Demangling/OldDemangler.cpp"
    "${SWIFT_SOURCE_DIR}/lib/Demangling/Demangler.cpp"
    "${SWIFT_SOURCE_DIR}/lib/Demangling/NodePrinter.cpp"
    "${SWIFT_SOURCE_DIR}/lib/Demangling/Context.cpp"
    "${SWIFT_SOURCE_DIR}/lib/Demangling/ManglingUtils.cpp"
    "${SWIFT_SOURCE_DIR}/lib/Demangling/Punycode.cpp")

# Acknowledge that the following sources are known.
set(LLVM_OPTIONAL_SOURCES
#    swift_sections.S
    MutexPThread.cpp
    MutexWin32.cpp
    CygwinPort.cpp
    ImageInspectionInit.cpp
    ImageInspectionELF.cpp
    ImageInspectionStatic.cpp
    StaticBinaryELF.cpp
    ${swift_runtime_sources}
    ${swift_runtime_objc_sources}
    ${swift_runtime_leaks_sources})

set(swift_runtime_library_compile_flags ${swift_runtime_compile_flags})
list(APPEND swift_runtime_library_compile_flags -DswiftCore_EXPORTS)

set(sdk "${SWIFT_HOST_VARIANT_SDK}")
if(SWIFT_BUILD_STATIC_STDLIB AND "${sdk}" STREQUAL "LINUX")
  list(REMOVE_ITEM swift_runtime_sources ImageInspectionELF.cpp)
  set(static_binary_lnk_file_list)
  string(TOLOWER "${sdk}" lowercase_sdk)

  # These two libraries are only used with the static swiftcore
  add_swift_library(swiftImageInspectionStatic STATIC
    ImageInspectionStatic.cpp
    StaticBinaryELF.cpp
    C_COMPILE_FLAGS ${swift_runtime_library_compile_flags}
    LINK_FLAGS ${swift_runtime_linker_flags})
  set_target_properties(swiftImageInspectionStatic PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${SWIFTSTATICLIB_DIR}/${lowercase_sdk}")

  add_swift_library(swiftImageInspectionShared STATIC
    ImageInspectionELF.cpp
    C_COMPILE_FLAGS ${swift_runtime_library_compile_flags}
    LINK_FLAGS ${swift_runtime_linker_flags})
  set_target_properties(swiftImageInspectionShared PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${SWIFTSTATICLIB_DIR}/${lowercase_sdk}")

  swift_install_in_component(stdlib
    TARGETS swiftImageInspectionStatic swiftImageInspectionShared
    DESTINATION "lib/swift_static/${lowercase_sdk}")

  # Generate the static-executable-args.lnk file used for ELF systems (eg linux)
  set(linkfile "${lowercase_sdk}/static-executable-args.lnk")
  add_custom_command_target(swift_static_binary_${sdk}_args
    COMMAND
      "${CMAKE_COMMAND}" -E copy
      "${SWIFT_SOURCE_DIR}/utils/static-executable-args.lnk"
      "${SWIFTSTATICLIB_DIR}/${linkfile}"
    OUTPUT
      "${SWIFTSTATICLIB_DIR}/${linkfile}"
    DEPENDS
      "${SWIFT_SOURCE_DIR}/utils/static-executable-args.lnk")

  list(APPEND static_binary_lnk_file_list ${swift_static_binary_${sdk}_args})
  swift_install_in_component(stdlib
    FILES "${SWIFTSTATICLIB_DIR}/${linkfile}"
    DESTINATION "lib/swift_static/${lowercase_sdk}")
  add_custom_target(static_binary_magic ALL DEPENDS ${static_binary_lnk_file_list})

  add_swift_library(swiftImageInspectionShared OBJECT_LIBRARY TARGET_LIBRARY
    ImageInspectionELF.cpp
    C_COMPILE_FLAGS ${swift_runtime_library_compile_flags}
    LINK_FLAGS ${swift_runtime_linker_flags}
    INSTALL_IN_COMPONENT never_install)
endif()

add_swift_library(swiftRuntime OBJECT_LIBRARY TARGET_LIBRARY
  ${swift_runtime_sources}
  ${swift_runtime_objc_sources}
  ${swift_runtime_leaks_sources}
  C_COMPILE_FLAGS ${swift_runtime_library_compile_flags}
  LINK_FLAGS ${swift_runtime_linker_flags}
  INSTALL_IN_COMPONENT never_install)

set(ELFISH_SDKS)
foreach(sdk ${SWIFT_CONFIGURED_SDKS})
  if("${SWIFT_SDK_${sdk}_OBJECT_FORMAT}" STREQUAL "ELF")
    list(APPEND ELFISH_SDKS "${sdk}")
  endif()
endforeach()

# Windows including MinGW does not need the begin, end sections.
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
elseif ("${CMAKE_SYSTEM_NAME}" STREQUAL "CYGWIN")
else()
  add_swift_library(section_magic_loader OBJECT_LIBRARY IS_STDLIB IS_STDLIB_CORE
      ImageInspectionInit.cpp
      C_COMPILE_FLAGS ${section_magic_compile_flags}
      TARGET_SDKS "${ELFISH_SDKS}"
      LINK_FLAGS ${swift_runtime_linker_flags}
      INSTALL_IN_COMPONENT never_install)
  add_swift_library(section_magic_begin OBJECT_LIBRARY IS_STDLIB IS_STDLIB_CORE
      swift_sections.S
      C_COMPILE_FLAGS ${section_magic_compile_flags} "-DSWIFT_BEGIN"
      TARGET_SDKS "${ELFISH_SDKS}"
      LINK_FLAGS ${swift_runtime_linker_flags}
      INSTALL_IN_COMPONENT never_install)
  add_swift_library(section_magic_end OBJECT_LIBRARY IS_STDLIB IS_STDLIB_CORE
      swift_sections.S
      C_COMPILE_FLAGS ${section_magic_compile_flags} "-DSWIFT_END"
      LINK_FLAGS ${swift_runtime_linker_flags}
      TARGET_SDKS "${ELFISH_SDKS}"
      INSTALL_IN_COMPONENT never_install)
endif()


set(object_target_list)

add_custom_target(section_magic ALL DEPENDS ${object_target_list})
