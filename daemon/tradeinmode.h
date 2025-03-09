/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <string_view>

// Return true if adbd should transition to trade-in mode.
bool should_enter_tradeinmode();

// Transition adbd to the given trade-in mode secontext.
void enter_tradeinmode(const char* seclabel);

// Returns whether the given command string is allowed while in trade-in mode.
bool allow_tradeinmode_command(std::string_view name);

// Returns whether adbd is currently in trade-in mode (eg enter_tradeinmode was called).
bool is_in_tradeinmode();

// Returns whether the "tradeinmode enter" command was used. This command places the device in
// "trade-in evaluation" mode, granting normal adb shell without authorization. In this mode, a
// factory reset is guaranteed on reboot.
bool is_in_tradein_evaluation_mode();
