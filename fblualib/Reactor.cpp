/*
 * Copyright 2015 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fblualib/Reactor.h>

// The actual Reactor class is in util/Reactor.cpp, but make sure that
// gLoopingState is defined even if that isn't linked in.

namespace fblualib {

namespace detail {
thread_local LoopingState gLoopingState;
}  // namespace detail

}  // namespaces
