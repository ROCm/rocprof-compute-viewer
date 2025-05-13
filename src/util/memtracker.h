// MIT License
//
// Copyright (c) 2024-2025 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once
#include <iostream>
#include <unordered_map>

//#define DEBUGMODE
// clang-format off

#ifdef DEBUGMODE
    class MemTracker {
    public:
        static int count;
        MemTracker(const char* file, int line): name(std::string(file)+':'+std::to_string(line)) {
            classes[name] = classes[name] + 1;
        }
        virtual ~MemTracker() { classes[name] -= 1; }

        static std::unordered_map<std::string, int> classes;
        static void Dump() {
            for (auto& pair : classes) if (pair.second) 
                std::cout << pair.first << " " << pair.second << std::endl; 
        }
        const std::string name;
    };
#else
    class MemTracker {
    public:
        static int count;
        MemTracker(const char* file, int line) { count += 1; }
        virtual ~MemTracker() { count -= 1; }

        static std::unordered_map<std::string, int> classes;
        static void Dump() { if (count) std::cout << "Warning - Leftover allocs: " << count << std::endl; }
    };
#endif

#define set_tracked() private: MemTracker tracker = {__FILE__, __LINE__};

#define QWARNING(exp, msg, todo) if (!(exp)) { std::cout << "Warning: " << __FILE__ << ":" << __LINE__ << " " << msg << std::endl; todo; };

#ifndef _WIN32
    #define BUILTIN_TRAP() __builtin_trap()
#else
    #define BUILTIN_TRAP() exit(1)
#endif

#define QASSERT(exp, msg) if (!(exp)) {                                               \
    std::cout << "Error: " << __FILE__ << ":" << __LINE__ << " " << msg << std::endl; \
    BUILTIN_TRAP();                                                                   \
};

#define FPS_LIMITER_TIMEOUT() 7 // 7ms = max 143 Hz refresh rate

#define IMPLEMENT_FPS_LIMITER()                                                                        \
{                                                                                                      \
    static auto last_time = std::chrono::system_clock::now();                                          \
    auto new_time = std::chrono::system_clock::now();                                                  \
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(new_time - last_time);       \
                                                                                                       \
    if (duration.count() < FPS_LIMITER_TIMEOUT()) return;                                              \
    last_time = new_time;                                                                              \
}

// clang-format on