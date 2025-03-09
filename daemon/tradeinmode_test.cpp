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
#include "tradeinmode.h"

#include <gtest/gtest.h>

TEST(TradeInModeTest, ValidateCommand) {
    EXPECT_FALSE(allow_tradeinmode_command("shell:blah"));
    EXPECT_TRUE(allow_tradeinmode_command("shell,-x:tradeinmode"));
    EXPECT_TRUE(allow_tradeinmode_command("shell:tradeinmode"));
    EXPECT_FALSE(allow_tradeinmode_command("shell:tradeinmodebad"));
    EXPECT_TRUE(allow_tradeinmode_command("shell:tradeinmode getstatus"));
    EXPECT_TRUE(allow_tradeinmode_command("shell:tradeinmode getstatus -c 1234"));
    EXPECT_TRUE(allow_tradeinmode_command("shell:tradeinmode enter"));
    EXPECT_FALSE(allow_tradeinmode_command("shell:tradeinmode && ls"));
}
