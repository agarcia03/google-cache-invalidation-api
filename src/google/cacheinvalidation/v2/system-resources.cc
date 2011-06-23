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

// Interfaces for the system resources used by the Ticl. System resources are an
// abstraction layer over the host operating system that provides the Ticl with
// the ability to schedule events, send network messages, store data, and
// perform logging.
//
// NOTE: All the resource types and SystemResources are required to be
// thread-safe.

#include "google/cacheinvalidation/v2/system-resources.h"

namespace invalidation {

const TimeDelta Scheduler::kNoDelay = TimeDelta::FromMilliseconds(0);

}  // namespace invalidation
