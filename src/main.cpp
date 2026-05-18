// MIT License
//
// Copyright (c) 2024-2026 Advanced Micro Devices, Inc. All rights reserved.
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

#include <QIcon>
#include <QPixmap>
#include <filesystem>
#include <string>
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    int res = 0;
    {
        QApplication a(argc, argv);
        QIcon icon(QPixmap(":/amd.ico"));
        a.setWindowIcon(icon);

#ifdef _WIN32
        a.setStyle(QStyleFactory::create("Fusion"));
#endif

        // Parse CLI arguments: positional paths auto-detected by filename
        // Recognizes: code.json, snapshots.json, directories, .rocpd files
        std::string input_path;
        for (int i = 1; i < argc; i++)
        {
            std::string arg = argv[i];
            std::string basename = arg.substr(arg.find_last_of("/\\") + 1);

            if (basename == "code.json")
                MainWindow::cli_code_json_override = std::filesystem::absolute(arg).string();
            else if (basename == "snapshots.json")
                MainWindow::cli_snapshots_json_override = std::filesystem::absolute(arg).string();
            else if (input_path.empty())
                input_path = arg;
        }

        MainWindow w(input_path);
        w.setWindowIcon(icon);
        w.show();
        res = a.exec();
    }
    MemTracker::Dump();
    return res;
}
