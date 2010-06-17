// Copyright 2010 Google Inc. All Rights Reserved.
// Author: akalin@google.com (Fred Akalin)
//
// Defines compiler-specific utility functions for the Tango
// invalidation client library.

#ifndef IPC_INVALIDATION_PUBLIC_INCLUDE_COMPILER_SPECIFIC_H_
#define IPC_INVALIDATION_PUBLIC_INCLUDE_COMPILER_SPECIFIC_H_

// google3 code doesn't compile with MSVC, so we don't need anything
// here.

#define ALLOW_THIS_IN_INITIALIZER_LIST(code) code

#endif  // IPC_INVALIDATION_PUBLIC_INCLUDE_COMPILER_SPECIFIC_H_
