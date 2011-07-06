// Copyright 2011 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Useful utility functions for the TICL

#include "google/cacheinvalidation/v2/proto-helpers.h"
#include "google/cacheinvalidation/v2/string_util.h"

namespace invalidation {

/*
 * Three arrays that store the representation of each character from 0 to 255.
 * The ith number's octal representation is: CHAR_OCTAL_STRINGS1[i],
 * CHAR_OCTAL_STRINGS2[i], CHAR_OCTAL_STRINGS3[i]
 * <p>
 * E.g., if the number 128, these arrays contain 2, 0, 0 at index 128. We use
 * 3 char arrays instead of an array of strings since the code path for a
 * character append operation is quite a bit shorterthan the append operation
 * for strings.
 */
char ProtoHelpers::CHAR_OCTAL_STRINGS1[];
char ProtoHelpers::CHAR_OCTAL_STRINGS2[];
char ProtoHelpers::CHAR_OCTAL_STRINGS3[];
bool ProtoHelpers::is_initialized = false;

string ProtoHelpers::ToString(const string& bytes) {
  if (bytes.empty()) {
    return "";
  }
  // This is a racy initialization but that is ok since we are initializing to
  // the same values.
  if (!is_initialized) {
    // Initialize the array with the Octal string values so that we do not have
    //  to do string.format for every byte during runtime.
    for (int i = 0; i < ProtoHelpers::NUM_CHARS; i++) {
      string value = StringPrintf("%03o", i);
      ProtoHelpers::CHAR_OCTAL_STRINGS1[i] = value[0];
      ProtoHelpers::CHAR_OCTAL_STRINGS2[i] = value[1];
      ProtoHelpers::CHAR_OCTAL_STRINGS3[i] = value[2];
    }
    is_initialized = true;
  }
  string builder;
  builder.reserve(3 * bytes.length());
  for (int i = 0; i < bytes.length(); i++) {
    char c = bytes[i];
    switch (c) {
      case '\n': builder += '\\'; builder += 'n'; break;
      case '\r': builder += '\\'; builder += 'r'; break;
      case '\t': builder += '\\'; builder += 't'; break;
      case '\"': builder += '\\'; builder += '"'; break;
      case '\\': builder += '\\'; builder += '\\'; break;
      default:
        if ((c >= 32) && (c < 127) && c != '\'') {
          builder += c;
        } else {
          int byteValue = c;
          if (c < 0) {
            byteValue = c + 256;
          }
          builder += '\\';
          builder += CHAR_OCTAL_STRINGS1[byteValue];
          builder += CHAR_OCTAL_STRINGS2[byteValue];
          builder += CHAR_OCTAL_STRINGS3[byteValue];
        }
        break;
    }
  }
  return builder;
}

}  // namespace invalidation
