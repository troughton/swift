add_swift_library(swiftImmediate STATIC
  Immediate.cpp
  REPL.cpp
  DEPENDS swift-syntax-generated-headers
  LINK_LIBRARIES
    swiftIDE
    swiftFrontend
    swiftSILGen
    swiftSILOptimizer
    swiftIRGen
    edit
  LLVM_COMPONENT_DEPENDS
    linker mcjit)

