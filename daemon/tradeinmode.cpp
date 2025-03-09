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
#include <unistd.h>

#include <regex>

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/strings.h>

#if defined(__ANDROID__)
#include <log/log_properties.h>
#include "selinux/android.h"
#endif

#if defined(__ANDROID__) && !defined(__ANDROID_RECOVERY__)
#include <com_android_tradeinmode_flags.h>
#endif

static bool in_tradeinmode = false;
static constexpr char kTradeInModeProp[] = "persist.adb.tradeinmode";

static constexpr int TIM_DISABLED = -1;
static constexpr int TIM_UNSET = 0;
static constexpr int TIM_FOYER = 1;
static constexpr int TIM_EVALUATION_MODE = 2;

bool should_enter_tradeinmode() {
#if defined(__ANDROID__) && !defined(__ANDROID_RECOVERY__)
    if (!com_android_tradeinmode_flags_enable_trade_in_mode()) {
        return false;
    }
    return android::base::GetIntProperty(kTradeInModeProp, TIM_UNSET) == TIM_FOYER;
#else
    return false;
#endif
}

void enter_tradeinmode(const char* seclabel) {
#if defined(__ANDROID__)
    if (selinux_android_setcon(seclabel) < 0) {
        PLOG(ERROR) << "Could not set SELinux context";

        // Flag TIM as failed so we don't enter a restart loop.
        android::base::SetProperty(kTradeInModeProp, std::to_string(TIM_DISABLED));

        _exit(1);
    }

    // Keep a separate global flag for TIM in case the property changes (for
    // example, if it's set while as root for testing).
    in_tradeinmode = true;
#endif
}

bool is_in_tradeinmode() {
    return in_tradeinmode;
}

bool is_in_tradein_evaluation_mode() {
    return android::base::GetIntProperty(kTradeInModeProp, TIM_UNSET) == TIM_EVALUATION_MODE;
}

bool allow_tradeinmode_command(std::string_view name) {
#if defined(__ANDROID__)
    // Allow "adb root" from trade-in-mode so that automated testing is possible.
    if (__android_log_is_debuggable() && android::base::ConsumePrefix(&name, "root:")) {
        return true;
    }
#endif

    // Allow "shell tradeinmode" with only simple arguments.
    std::regex tim_pattern("shell[^:]*:tradeinmode(\\s*|\\s[A-Za-z0-9_\\-\\s]*)");
    return std::regex_match(std::string(name), tim_pattern);
}
