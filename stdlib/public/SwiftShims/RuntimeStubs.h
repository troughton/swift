//===--- RuntimeStubs.h -----------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// Misc stubs for functions which should be defined in the core standard
// library, but are difficult or impossible to write in Swift at the
// moment.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_STDLIB_SHIMS_RUNTIMESTUBS_H_
#define SWIFT_STDLIB_SHIMS_RUNTIMESTUBS_H_

#include "LibcShims.h"

#ifdef __cplusplus
#ifndef __swift__
namespace swift {
#endif
extern "C" {
#endif

SWIFT_BEGIN_NULLABILITY_ANNOTATIONS

SWIFT_RUNTIME_STDLIB_API
__swift_ssize_t
swift_stdlib_readLine_stdin(unsigned char * _Nullable * _Nonnull LinePtr);

SWIFT_RUNTIME_STDLIB_API
char * _Nullable * _Nonnull
_swift_stdlib_getUnsafeArgvArgc(int * _Nonnull outArgLen);
  
SWIFT_RUNTIME_STDLIB_API
void
_swift_stdlib_overrideUnsafeArgvArgc(char * _Nullable * _Nonnull argv, int argc);

SWIFT_END_NULLABILITY_ANNOTATIONS

#ifdef __cplusplus
} // extern "C"
#ifndef __swift__
} // namespace swift
#endif
#endif

#endif // SWIFT_STDLIB_SHIMS_RUNTIMESTUBS_H_

