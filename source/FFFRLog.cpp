/**
 * Copyright 2019 Matthew Oliver
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "FFFRLog.h"

// Required for stack trace
extern "C" {
#include <libavutil/error.h>
}

#include <sstream>
#if XS_PLATFORM == XS_WINDOWS
#    include <Windows.h>
#    pragma comment(lib, "Dbghelp")
#    include <DbgHelp.h>
#elif XS_PLATFORM == XS_LINUX
#    include <execinfo.h>
#    define UNW_LOCAL_ONLY
#    include <cxxabi.h>
#    include <libunwind.h>
#    include <stdio.h>
#    include <stdlib.h>
#    include <string.h>
extern const char* __progname;
#endif

using namespace std;

namespace FfFrameReader {
void Log::logMessage(const string& text, const LogLevel severity)
{
    string log;
    switch (severity) {
        case LogLevel::Info:
            log = "Info: ";
            break;
        case LogLevel::Warning:
            log = "Warning: ";
            break;
        case LogLevel::Error:
            log = "Error: ";
            break;
        default:
            break;
    }
    (log += text) += "\n";
    lock_guard<mutex> lockMutex(m_mutex);
    m_fileHandle.write(log.data(), log.size());
    // Ensure critical data is logged immediately to buffer so it is not lost should a crash occur
    m_fileHandle.flush();
}

void Log::logInfo(const string& text)
{
    logMessage(text, LogLevel::Info);
}

void Log::logWarning(const string& text)
{
    logMessage(text, LogLevel::Warning);
}

void Log::logError(const string& text)
{
    logMessage(text, LogLevel::Error);
}

void Log::logStackTrace()
{
    lock_guard<mutex> lockMutex(m_mutex);
    // Log the stack trace
    m_fileHandle.write("Stack Trace: \n", 14);
#if XS_PLATFORM == XS_WINDOWS
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);
    void* stack[200];
    const uint16_t frames = CaptureStackBackTrace(0, 200, stack, nullptr);
    SYMBOL_INFO* const _restrict symbol =
        static_cast<SYMBOL_INFO*>(calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1));
    if (symbol == nullptr) {
        return;
    }
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    for (uint32_t i = 1; i < frames; i++) // Start from 1 to avoid logging this function
    {
        SymFromAddr(process, reinterpret_cast<DWORD64>(stack[i]), nullptr, symbol);
        DWORD displacement;
        IMAGEHLP_LINE64 line;
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        if (!strstr(symbol->Name, "VSDebugLib::") &&
            SymGetLineFromAddr64(process, reinterpret_cast<DWORD64>(stack[i]), &displacement, &line)) {
            string tempBuffer = "Function: ";
            tempBuffer += symbol->Name;
            tempBuffer += ", Line: ";
            stringstream ss;
            string s;
            ss << static_cast<uint32_t>(line.LineNumber);
            ss >> s;
            tempBuffer += s;
            tempBuffer += '\n';
            m_fileHandle.write(tempBuffer.data(), tempBuffer.size());
            // Ensure critical data is logged immediately to buffer so it is not lost should a crash occur
            m_fileHandle.flush();
        }
        if (0 == strcmp(symbol->Name, "main")) {
            break;
        }
    }
    free(symbol);
#elif XS_PLATFORM == XS_LINUX
    char file[256], name[256];
    unw_cursor_t cursor;
    unw_context_t uc;
    unw_getcontext(&uc);
    unw_init_local(&cursor, &uc);
    while (unw_step(&cursor) > 0) {
        unw_word_t ip, sp, off;
        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        unw_get_reg(&cursor, UNW_REG_SP, &sp);
        unw_get_proc_name(&cursor, name, sizeof(name), &off);

        // prepare command to be executed
        // our program need to be passed after the -e parameter
        static char buf[256];
        sprintf(buf, "/usr/bin/addr2line -C -e ./%s -f -i %lx", __progname, (long)ip);

        FILE* f = popen(buf, "r");

        if (f == nullptr) {
            perror(buf);
            return 0;
        }

        // get function name
        fgets(buf, 256, f);
        // get file and line
        fgets(buf, 256, f);

        int32_t line;
        if (buf[0] != '?') {
            // file name is until ':'
            while (*p != ':') {
                p++;
            }
            *p++ = 0;
            // after file name follows line number
            strcpy(file, buf);
            sscanf(p, "%d", &line);
        } else {
            strcpy(file, "unknown");
            line = -1;
        }
        pclose(f);

        if (line >= 0) {
            stTempBuffer = "Function: ";
            int32_t status;
            char* _restrict realname = abi::__cxa_demangle(name, 0, 0, &status);
            if (realname) {
                stTempBuffer += realname;
                free(realname);
            } else {
                stTempBuffer += name;
            }
            stTempBuffer += ", File: ";
            stTempBuffer += file;
            stTempBuffer += ", Line: ";
            stringstream ss;
            string s;
            ss << (uint32_t)line.LineNumber;
            ss >> s;
            stTempBuffer += s;
            stTempBuffer += '\n';
            m_fileHandle.write(stTempBuffer.data(), stTempBuffer.size());
            // Ensure critical data is logged immediately to buffer so it is not lost should a crash occur
            m_fileHandle.flush();
        }
    }
#endif
}

std::string Log::getFfmpegErrorString(const int err)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE];
    av_make_error_string(buffer, AV_ERROR_MAX_STRING_SIZE, err);
    return buffer;
}

Log::~Log() noexcept
{
    try {
        if (m_fileHandle.is_open()) {
            m_fileHandle.close();
        }
    } catch (...) {
    }
}

bool Log::load() noexcept
{
    m_fileHandle = fstream("FfFrameReader.log", std::ios::out);
    // Check if file handle is valid
    return m_fileHandle.is_open();
}
} // namespace FfFrameReader