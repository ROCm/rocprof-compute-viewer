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

#include "config.hpp"
#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QPalette>
#include <QTextStream>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "config/config.hpp"
#include "json/include/nlohmann/json.hpp"

bool bDarkTheme = false;

QColor StyleColor::ToColor(const std::string& style)
{
    assert(style.size() == 7);
    assert(style[0] == '#');

    auto getval = [&style](int pos) { return std::stoi(style.substr(pos, 2), 0, 16); };
    return QColor(getval(1), getval(3), getval(5));
}

static void addpair(nlohmann::json& json, const char* a, const char* b)
{
    nlohmann::json pair;
    pair.push_back(a);
    pair.push_back(b);
    json.push_back(pair);
}

class ColorSet
{
public:
    ColorSet(const nlohmann::json& json)
    {
        for (auto& [_, color] : json.items()) colors.emplace_back(StyleColor(color[0], color[1]));
    };
    std::vector<StyleColor> colors;
};

namespace WindowColors
{
QPalette& getLightPalette()
{
    static QPalette lightPalette = []()
    {
        QPalette palette;
        // Set colors for various roles
        palette.setColor(QPalette::Window, QColor(240, 240, 240));
        palette.setColor(QPalette::WindowText, Qt::black);
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::AlternateBase, QColor(233, 233, 233));
        palette.setColor(QPalette::ToolTipBase, Qt::white);
        palette.setColor(QPalette::ToolTipText, Qt::black);
        palette.setColor(QPalette::Text, Qt::black);
        palette.setColor(QPalette::Button, QColor(240, 240, 240));
        palette.setColor(QPalette::ButtonText, Qt::black);
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Link, QColor(0, 0, 255));
        palette.setColor(QPalette::Highlight, QColor(0, 120, 215));
        palette.setColor(QPalette::HighlightedText, Qt::white);

        // Set disabled colors
        palette.setColor(QPalette::Disabled, QPalette::Text, Qt::darkGray);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, Qt::darkGray);
        palette.setColor(QPalette::Disabled, QPalette::WindowText, Qt::darkGray);
        palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(190, 190, 190));
        palette.setColor(QPalette::Disabled, QPalette::HighlightedText, Qt::darkGray);

        return palette;
    }();

    return lightPalette;
}
QPalette& getDarkPalette()
{
    static QPalette darkPalette = []()
    {
        QPalette palette;

        // Set colors for various roles
        palette.setColor(QPalette::Window, QColor(53, 53, 53));
        palette.setColor(QPalette::WindowText, Qt::white);
        palette.setColor(QPalette::Base, QColor(42, 42, 42));
        palette.setColor(QPalette::AlternateBase, QColor(66, 66, 66));
        palette.setColor(QPalette::ToolTipBase, Qt::white); // Tooltips often stay light
        palette.setColor(QPalette::ToolTipText, Qt::black); // Tooltips often stay light
        palette.setColor(QPalette::Text, Qt::white);
        palette.setColor(QPalette::Button, QColor(53, 53, 53));
        palette.setColor(QPalette::ButtonText, Qt::white);
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Link, QColor(42, 130, 218));
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, Qt::black);

        // Set disabled colors
        palette.setColor(QPalette::Disabled, QPalette::Text, Qt::darkGray);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, Qt::darkGray);
        palette.setColor(QPalette::Disabled, QPalette::WindowText, Qt::darkGray);
        palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(80, 80, 80));
        palette.setColor(QPalette::Disabled, QPalette::HighlightedText, Qt::darkGray);

        return palette;
    }();

    return darkPalette;
}

bool isDark() { return bDarkTheme; }
void setDark(bool bDark)
{
    bDarkTheme = bDark;
    qApp->setPalette(bDarkTheme ? getDarkPalette() : getLightPalette());

    const_cast<QColor&>(Config::PlotColors(6)) = bDark ? QColor(255, 255, 255) : QColor(0, 0, 0);
}
const QColor& Background()
{
    static QColor dark(36, 36, 36);
    static QColor white(224, 224, 224);
    return bDarkTheme ? dark : white;
}
const Color& StripeBackground()
{
    static Color dark(QColor(27, 27, 27));
    static Color white(QColor(210, 210, 210));
    return bDarkTheme ? dark : white;
}
const QColor& TraceBackground()
{
    static QColor dark(36, 36, 36);
    static QColor white(224, 224, 224);
    return bDarkTheme ? dark : white;
}
const QColor& LineSlowHighlight()
{
    static QColor dark(16, 127, 16, 192);
    static QColor white(16, 255, 16, 192);
    return bDarkTheme ? dark : white;
}
const QColor& LineRefHighlight(bool bClick)
{
    static QColor dark(150, 80, 0);
    static QColor white(255, 220, 115);
    static QColor dark_click(50, 0, 120);
    static QColor white_click(255, 175, 75);
    return bClick ? (bDarkTheme ? dark_click : white_click) : (bDarkTheme ? dark : white);
}
const QColor& GraphBkg()
{
    static QColor dark(24, 24, 24);
    static QColor white(180, 180, 180);
    return bDarkTheme ? dark : white;
}
const QColor& HotspotBkg()
{
    static QColor dark(56, 56, 56);
    static QColor white(224, 224, 224);
    return bDarkTheme ? dark : white;
}
const QColor& HotspotOutline()
{
    static QColor dark(120, 120, 120);
    static QColor light(0, 0, 0);
    return bDarkTheme ? dark : light;
}
const QColor& MeasureTool()
{
    static QColor white(255, 32, 32, 100);
    static QColor dark(255, 100, 100, 160);
    return bDarkTheme ? dark : white;
}
const QColor& LatencyTextColor()
{
    static QColor dark(255, 80, 80);
    static QColor light(0, 0, 0);
    return bDarkTheme ? dark : light;
}
const QColor& UtilizationBarColorBg()
{
    static QColor dark = QColor(38, 38, 38).lighter(120);
    static QColor white = QColor(224, 224, 224).darker(120);
    return bDarkTheme ? dark : white;
}
QColor textColor() { return bDarkTheme ? Qt::white : Qt::black; }
QColor reverseTextColor() { return bDarkTheme ? Qt::black : Qt::white; }
}; // namespace WindowColors

namespace Config
{
const QColor& StallColor()
{
    static auto color = QColor(192,0,0);
    return color;
}

const QColor& IssueColor()
{
    static auto color = QColor(0,160,0);
    return color;
}

const std::vector<StyleColor>& StateColors()
{
    static ColorSet colorset_light(
        []()
        {
            nlohmann::json def_state;
            addpair(def_state, "NONE", "#ffffff");
            addpair(def_state, "IDLE", "#707070");
            addpair(def_state, "EXEC", "#00fe00");
            addpair(def_state, "WAIT", "#fefe00");
            addpair(def_state, "STALL", "#ff0000");
            return def_state;
        }()
    );
    // 20% darker than the light theme colors
    static ColorSet colorset_dark(
        []()
        {
            nlohmann::json def_state;
            addpair(def_state, "NONE", "#cccccc");  // 20% darker than #ffffff
            addpair(def_state, "IDLE", "#595959");  // 20% darker than #707070
            addpair(def_state, "EXEC", "#00cc00");  // 20% darker than #00fe00
            addpair(def_state, "WAIT", "#cccc00");  // 20% darker than #fefe00
            addpair(def_state, "STALL", "#cc0000"); // 20% darker than #ff0000
            return def_state;
        }()
    );
    return bDarkTheme ? colorset_dark.colors : colorset_light.colors;
}

const std::vector<StyleColor>& TokenColors()
{
    static ColorSet colorset_light(
        []()
        {
            nlohmann::json def_token;

            try
            {
                std::ifstream ifs("token_def.json");
                if (!ifs.is_open()) throw std::exception();

                nlohmann::json new_token;
                ifs >> new_token;

                for (auto& token : new_token["tokens"])
                    addpair(def_token, std::string(token[0]).c_str(), std::string(token[1]).c_str());
            }
            catch (const std::exception& e)
            {
                addpair(def_token, "NONE", "#a8a8a8");
                addpair(def_token, "SMEM", "#f0ff00");
                addpair(def_token, "SALU", "#a0ffa0");
                addpair(def_token, "VMEM", "#ffc432");
                addpair(def_token, "FLAT", "#80d0f8");
                addpair(def_token, "LDS", "#ff7000");
                addpair(def_token, "VALU", "#00a000");
                addpair(def_token, "JUMP", "#5038ff");
                addpair(def_token, "NEXT", "#8020ff");
                addpair(def_token, "IMMED", "#404040");
                addpair(def_token, "TRAP", "#ff0804");
                addpair(def_token, "MSG", "#101010");
                addpair(def_token, "BVH", "#ff0804");
                addpair(def_token, "MATRIX", "#006000");
            }
            return def_token;
        }()
    );

    static ColorSet colorset_dark(
        []()
        {
            nlohmann::json def_token;

            auto add_original = [&]()
            {
                addpair(def_token, "NONE", "#707070");
                addpair(def_token, "SMEM", "#e0f000");
                addpair(def_token, "SALU", "#a0f0a0");
                addpair(def_token, "VMEM", "#f0c432");
                addpair(def_token, "FLAT", "#80d0f0");
                addpair(def_token, "LDS", "#f07000");
                addpair(def_token, "VALU", "#00a000");
                addpair(def_token, "JUMP", "#5038f0");
                addpair(def_token, "NEXT", "#8020f0");
                addpair(def_token, "IMMED", "#282828");
                addpair(def_token, "TRAP", "#f00020");
                addpair(def_token, "MSG", "#080000");
                addpair(def_token, "BVH", "#f00804");
                addpair(def_token, "MATRIX", "#006000");
            };

            bool keep_original = true;

            try
            {
                std::ifstream ifs("token_def.json");
                if (ifs.is_open())
                {
                    nlohmann::json new_token;
                    ifs >> new_token;

                    try {
                        keep_original = bool(new_token["keep_original"]);
                    } catch(std::exception&) {}

                    if (keep_original) add_original();
                    keep_original = false;

                    for (auto& token : new_token["tokens"])
                        addpair(def_token, std::string(token[0]).c_str(), std::string(token[1]).c_str());
                }
            }
            catch (const std::exception& e) {
                std::cout << e.what() << std::endl;
            }

            if (keep_original) add_original();
            return def_token;
        }()
    );

    return bDarkTheme ? colorset_dark.colors : colorset_light.colors;
}

const std::vector<std::pair<std::string, int>> CustomTokens()
{
    static std::vector<std::pair<std::string, int>> replace = []()
    {
        std::vector<std::pair<std::string, int>> ret;
        nlohmann::json key;

        key["v_mfma"] = "MATRIX";
        key["v_smfma"] = "MATRIX";
        key["v_wmma"] = "MATRIX";
        key["v_swmma"] = "MATRIX";

        try
        {
            std::ifstream ifs("token_def.json");
            if (!ifs.is_open()) throw std::exception();

            nlohmann::json new_asm;
            ifs >> new_asm;

            for (auto& [isa, value] : new_asm["asmkeys"].items()) key[isa] = value;
        }
        catch (const std::exception& e) {}

        for (auto& [match, inst] : key.items())
        {
            for (size_t i = 0; i < TokenColors().size(); i++)
            {
                if (inst == TokenColors().at(i).name)
                {
                    ret.push_back({match, i});
                    break;
                }
            }
        }
        return ret;
    }();
    return replace;
}

const QColor& PlotColors(int index)
{
    static std::vector<QColor> colors = {
        { 32,  32, 255},
        {255,  32,  32},
        { 32, 255,  32},

        {255, 255,  32},
        {255,  32, 255},
        { 32, 255, 255},

        {255, 255, 255},

        {128, 192,   0},
        {  0, 128, 192},
        {192,   0, 128},

        {180, 100,   0},
        {  0, 180, 100},
        {100,   0, 180},

        {160, 255, 255},
        {255, 160, 255},
        {255, 255, 160}
    };

    return colors.at(index % colors.size());
}
}; // namespace Config
