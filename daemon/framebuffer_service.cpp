/*
 * Copyright (C) 2007 The Android Open Source Project
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

#include "framebuffer_service.h"

#include "adb.h"
#include "adb_io.h"
#include "adb_utils.h"
#include "sysdeps.h"

// This version number defines the format of the fbinfo struct.
// It must match versioning in ddms where this data is consumed.
#define DDMS_RAWIMAGE_VERSION 2
struct fbinfo {
    unsigned int version;
    unsigned int bpp;
    unsigned int colorSpace;
    unsigned int size;
    unsigned int width;
    unsigned int height;
    unsigned int red_offset;
    unsigned int red_length;
    unsigned int blue_offset;
    unsigned int blue_length;
    unsigned int green_offset;
    unsigned int green_length;
    unsigned int alpha_offset;
    unsigned int alpha_length;
} __attribute__((packed));

static void reformat_screencap(int in_fd, int out_fd) {
    // Read the screencap header.
    int w, h, format, colorSpace;
    if (!ReadFdExactly(in_fd, &w, 4) || !ReadFdExactly(in_fd, &h, 4) ||
        !ReadFdExactly(in_fd, &format, 4) || !ReadFdExactly(in_fd, &colorSpace, 4)) {
        PLOG(ERROR) << "couldn't read screencap header";
        return;
    }

    // Translate that into the adb framebuffer header.
    struct fbinfo fbinfo = {};
    fbinfo.version = DDMS_RAWIMAGE_VERSION;
    fbinfo.colorSpace = colorSpace;
    switch (format) {
        case 1:  // RGBA_8888
            fbinfo.bpp = 32;
            fbinfo.size = w * h * 4;
            fbinfo.width = w;
            fbinfo.height = h;
            fbinfo.red_offset = 0;
            fbinfo.red_length = 8;
            fbinfo.green_offset = 8;
            fbinfo.green_length = 8;
            fbinfo.blue_offset = 16;
            fbinfo.blue_length = 8;
            fbinfo.alpha_offset = 24;
            fbinfo.alpha_length = 8;
            break;
        case 2:  // RGBX_8888
            fbinfo.bpp = 32;
            fbinfo.size = w * h * 4;
            fbinfo.width = w;
            fbinfo.height = h;
            fbinfo.red_offset = 0;
            fbinfo.red_length = 8;
            fbinfo.green_offset = 8;
            fbinfo.green_length = 8;
            fbinfo.blue_offset = 16;
            fbinfo.blue_length = 8;
            fbinfo.alpha_offset = 24;
            fbinfo.alpha_length = 0;
            break;
        case 3:  // RGB_888
            fbinfo.bpp = 24;
            fbinfo.size = w * h * 3;
            fbinfo.width = w;
            fbinfo.height = h;
            fbinfo.red_offset = 0;
            fbinfo.red_length = 8;
            fbinfo.green_offset = 8;
            fbinfo.green_length = 8;
            fbinfo.blue_offset = 16;
            fbinfo.blue_length = 8;
            fbinfo.alpha_offset = 24;
            fbinfo.alpha_length = 0;
            break;
        case 4:  // RGB_565
            fbinfo.bpp = 16;
            fbinfo.size = w * h * 2;
            fbinfo.width = w;
            fbinfo.height = h;
            fbinfo.red_offset = 11;
            fbinfo.red_length = 5;
            fbinfo.green_offset = 5;
            fbinfo.green_length = 6;
            fbinfo.blue_offset = 0;
            fbinfo.blue_length = 5;
            fbinfo.alpha_offset = 0;
            fbinfo.alpha_length = 0;
            break;
        case 5:  // BGRA_8888
            fbinfo.bpp = 32;
            fbinfo.size = w * h * 4;
            fbinfo.width = w;
            fbinfo.height = h;
            fbinfo.red_offset = 16;
            fbinfo.red_length = 8;
            fbinfo.green_offset = 8;
            fbinfo.green_length = 8;
            fbinfo.blue_offset = 0;
            fbinfo.blue_length = 8;
            fbinfo.alpha_offset = 24;
            fbinfo.alpha_length = 8;
            break;
        default:
            LOG(ERROR) << "bad screencap format: " << format;
            return;
    }

    // Write the modified header.
    if (!WriteFdExactly(out_fd, &fbinfo, sizeof(fbinfo))) {
        PLOG(ERROR) << "framebuffer service couldn't write header";
        return;
    }

    // Copy the raw pixel data.
    char buf[BUFSIZ];
    size_t left = fbinfo.size;
    while (left > 0) {
        size_t chunk_size = std::min(sizeof(buf), left);
        int bytes = adb_read(in_fd, buf, chunk_size);
        if (bytes == -1) {
            PLOG(ERROR) << "framebuffer service read failed";
            return;
        }
        left -= bytes;
        if (!WriteFdExactly(out_fd, buf, bytes)) {
            PLOG(ERROR) << "framebuffer service write failed";
            return;
        }
    }
}

void framebuffer_service(unique_fd out_fd) {
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) == -1) {
        PLOG(ERROR) << "framebuffer service pipe() failed";
        return;
    }

    pid_t pid = fork();
    if (pid == -1) {
        PLOG(ERROR) << "framebuffer service fork() failed";
        return;
    }

    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        adb_close(fds[0]);
        adb_close(fds[1]);
        const char* args[] = {"screencap", nullptr};
        execvp(args[0], (char**)args);
        perror_exit("exec() screencap failed");
    }

    reformat_screencap(fds[0], out_fd.get());
    adb_close(fds[0]);
    adb_close(fds[1]);

    TEMP_FAILURE_RETRY(waitpid(pid, nullptr, 0));
}
