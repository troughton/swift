//===--- UUID.cpp - UUID generation ---------------------------------------===//
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
// This is an interface over the standard OSF uuid library that gives UUIDs
// sane value semantics and operators.
//
//===----------------------------------------------------------------------===//

#include "swift/Basic/UUID.h"

// WIN32 doesn't natively support <uuid/uuid.h>. Instead, we use Win32 APIs.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX 1
#include <objbase.h>
#include <string>
#else
#include <uuid/uuid.h>
#endif

using namespace swift;

swift::UUID::UUID(FromRandom_t) {
#if defined(_WIN32)
  ::UUID uuid;
  ::CoCreateGuid(&uuid);

  memcpy(Value, &uuid, Size);
#else
  uuid_generate_random(Value);
#endif
}

swift::UUID::UUID(FromTime_t) {
#if defined(_WIN32)
  ::GUID uuid;
  ::CoCreateGuid(&uuid);

  memcpy(Value, &uuid, Size);
#else
  uuid_generate_time(Value);
#endif
}

swift::UUID::UUID() {
#if defined(_WIN32)
  ::GUID uuid = GUID();

  memcpy(Value, &uuid, Size);
#else
  uuid_clear(Value);
#endif
}

Optional<swift::UUID> swift::UUID::fromString(const char *s) {
#if defined(_WIN32)
  swift::UUID result;
  int n = 0;
  sscanf(s,
    "%2hhx%2hhx%2hhx%2hhx-%2hhx%2hhx-%2hhx%2hhx-%2hhx%2hhx-%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%n",
    &result.Value[0], &result.Value[1], &result.Value[2], &result.Value[3],
    &result.Value[4], &result.Value[5], &result.Value[6], &result.Value[7],
    &result.Value[8], &result.Value[9], &result.Value[10], &result.Value[11],
    &result.Value[12], &result.Value[13], &result.Value[14], &result.Value[15], &n);
  if (n != 36 || s[n] != '\0')
    return None;
  return result;
#else
  swift::UUID result;
  if (uuid_parse(s, result.Value))
    return None;
  return result;
#endif
}

void swift::UUID::toString(llvm::SmallVectorImpl<char> &out) const {
  out.resize(UUID::StringBufferSize);
#if defined(_WIN32)
  sprintf(out.data(),
    "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
    Value[0], Value[1], Value[2], Value[3],
    Value[4], Value[5], Value[6], Value[7],
    Value[8], Value[9], Value[10], Value[11],
    Value[12], Value[13], Value[14], Value[15]);
#else
  uuid_unparse_upper(Value, out.data());
#endif
  // Pop off the null terminator.
  assert(out.back() == '\0' && "did not null-terminate?!");
  out.pop_back();
}

int swift::UUID::compare(UUID y) const {
#if defined(_WIN32)
  ::GUID uuid1;
  memcpy(&uuid1, Value, Size);

  ::GUID uuid2;
  memcpy(&uuid2, y.Value, Size);

  return memcmp(Value, y.Value, Size);
#else
  return uuid_compare(Value, y.Value);
#endif
}

llvm::raw_ostream &swift::operator<<(llvm::raw_ostream &os, UUID uuid) {
  llvm::SmallString<UUID::StringBufferSize> buf;
  uuid.toString(buf);
  os << buf;
  return os;
}
