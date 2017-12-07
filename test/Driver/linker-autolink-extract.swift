// RUN: %swiftc_driver -driver-print-jobs -target x86_64-unknown-linux-gnu -g %s | %FileCheck -check-prefix DEBUG_LINUX %s
// RUN: %swiftc_driver -driver-print-jobs -target x86_64-unknown-windows-msvc -g %s | %FileCheck -check-prefix DEBUG_WINDOWS %s

// REQUIRES: autolink-extract

// DEBUG_LINUX: bin/swift
// DEBUG_LINUX-NEXT: bin/swift-autolink-extract
// DEBUG_LINUX-NEXT: bin/swift
// DEBUG_LINUX-NEXT: bin/swift -modulewrap
// DEBUG_LINUX-NEXT: bin/clang++{{"? }}
// DEBUG_LINUX: -o main
// DEBUG_LINUX-NOT: dsymutil

// DEBUG_WINDOWS: bin/swift
// DEBUG_WINDOWS-NEXT: bin/swift-autolink-extract
// DEBUG_WINDOWS-NEXT: bin/swift
// DEBUG_WINDOWS-NEXT: bin/swift -modulewrap
// DEBUG_WINDOWS-NEXT: bin/clang++{{"? }}
// DEBUG_WINDOWS: -o main
