// MIT License
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#include <gtest/gtest.h>
#include <QApplication>
#include <QDir>
#include <QHeaderView>
#include <QImage>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>
#include <QToolButton>
#include <cmath>
#include "../config_test_settings.h"
#include "analysis/annotation.h"
#include "code/codecolumns.h"
#include "code/labelminimap.h"
#include "code/qcodelist.h"
#include "code/sourcefile.h"
#include "config/appconfig.h"

namespace
{
// Only the app boundary is substituted. Rendering, navigation within the
// listing, source references, annotations, and settings use production code.
class TestContext : public Isa::Context
{
public:
    QFont codeFont(const QFont&) const override { return font; }
    int currentIteration() const override { return iteration; }
    bool hiddenLatencyAvailable() const override { return hidden_latency_available; }
    void scalePainter(QPainter& painter) const override { painter.scale(scale, scale); }
    double painterScale() const override { return scale; }
    void selectInstruction(const ASMLine& instruction) override { selected_lines.push_back(instruction.line_number); }
    void listingChanged() override {}

    QFont font{"monospace", 10};
    int iteration = -1;
    bool hidden_latency_available = false;
    double scale = 1.0;
    std::vector<int> selected_lines;
};

std::vector<CodeData> makeCode(const std::vector<std::string>& text)
{
    std::vector<CodeData> code;
    for (int i = 0; i < static_cast<int>(text.size()); ++i)
        code.emplace_back(100 + 3 * i, 4, 0x1000 + 4 * i, 1, 16, 0, 0, 0, 0, text[i], "", std::vector<int64_t>{});
    return code;
}

class IsaViewerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        AppConfig::getInstance().setFontSize(9);
        for (int i = -1; i < ASMCodeline::ENUMTYPES; ++i)
        {
            AppConfig::getInstance().setColumnWidth(i, -1);
            AppConfig::getInstance().setColumnVisible(i, true);
        }
        WindowColors::setDark(false);
        HorizontalHotspot::is_sqtt_enabled = true;
        HorizontalHotspot::is_pcs_enabled = false;
        CyclesLabel::setStrategy(CyclesLabel::Strategy::SUM_ALL);
        Canvas::drawtype = Canvas::DrawType::DrawArrows;
        Canvas::active_annotation_id.clear();
        view = std::make_unique<QCodelist>(context);
        view->resize(1400, 450);
        view->Populate(makeCode({"label_a:", "v_add_f32 v0, v1, v2", "s_waitcnt vmcnt(0)", "label_b:", "s_endpgm"}));
        view->show();
        QApplication::processEvents();
    }

    void TearDown() override
    {
        view.reset();
        ASMCodeline::Clear();
        SourceLine::all_lines.clear();
        Annotation::Registry::instance().clearAll();
        WindowColors::setDark(false);
    }
    TestContext context;
    std::unique_ptr<QCodelist> view;
};

TEST_F(IsaViewerTest, FoldingSelectorIsVisibleAndDefaultsToExpandedLabels)
{
    const int original_height = view->elements[ASMCodeline::EASM]->height();
    auto* selector = view->findChild<QComboBox*>("isaFoldingMode");
    auto* expand = view->findChild<QToolButton*>("isaExpandAll");
    ASSERT_NE(selector, nullptr);
    ASSERT_NE(expand, nullptr);
    EXPECT_TRUE(selector->isVisible());
    EXPECT_EQ(selector->currentText(), "Fold by labels");
    EXPECT_GE(selector->width(), selector->sizeHint().width());
    // Controls share the existing column header, not a new toolbar row.
    EXPECT_EQ(
        selector->mapTo(view.get(), QPoint()).y(),
        view->findChild<CycleModeSelector*>()->mapTo(view.get(), QPoint()).y()
    );
    EXPECT_TRUE(view->rowMapping().foldingEnabled());
    EXPECT_EQ(view->rowMapping().count(), 5);
    QTest::mouseClick(view->elements[ASMCodeline::EASM], Qt::LeftButton, Qt::NoModifier, QPoint(7, 6));
    EXPECT_EQ(view->rowMapping().count(), 3);
    EXPECT_EQ(view->rowMapping().lineAt(1), 3);
    for (auto* column : view->elements) EXPECT_EQ(column->getLineIndex(QCodelist::lineheight() + 1), 3);
    EXPECT_EQ(view->elements[ASMCodeline::EASM]->height(), original_height);
    QTest::mouseClick(expand, Qt::LeftButton);
    EXPECT_EQ(view->rowMapping().count(), 5);
    view->toggleSection(0);
    selector->setCurrentIndex(0);
    EXPECT_FALSE(view->rowMapping().foldingEnabled());
    EXPECT_FALSE(expand->isEnabled());
    EXPECT_EQ(selector->currentText(), "No folding");
    EXPECT_EQ(view->rowMapping().count(), 5);
    view->setFoldingEnabled(true); // Programmatic changes also update the control.
    EXPECT_EQ(selector->currentText(), "Fold by labels");
    EXPECT_TRUE(expand->isEnabled());
    view->toggleSection(0);
    view->Populate(makeCode({"label_new:", "s_endpgm"}));
    EXPECT_EQ(view->rowMapping().count(), 2);
    EXPECT_FALSE(view->rowMapping().sectionAt(0)->collapsed);
}

TEST_F(IsaViewerTest, NavigationRevealsHiddenInstructionsAndLabels)
{
    view->setFoldingEnabled(true);
    view->toggleSection(0);
    view->toggleSection(3);
    view->Highlight(1, 1, true);
    EXPECT_GE(view->rowMapping().rowOf(1), 0);
    EXPECT_EQ(view->rowMapping().rowOf(4), -1);
    view->Highlight(3, 3, true);
    EXPECT_EQ(view->rowMapping().count(), 5);
}

TEST_F(IsaViewerTest, ClickingFoldedRowsNavigatesUsingDecoderInstructionIdentity)
{
    auto* instructions = view->elements[ASMCodeline::EASM];
    const int row_height = QCodelist::lineheight();
    QTest::mouseClick(instructions, Qt::LeftButton, Qt::NoModifier, QPoint(7, row_height / 2));
    EXPECT_TRUE(context.selected_lines.empty()); // Folding must not navigate.

    QTest::mouseClick(instructions, Qt::LeftButton, Qt::NoModifier, QPoint(30, 2 * row_height + row_height / 2));
    view->expandAll();
    QTest::mouseClick(instructions, Qt::LeftButton, Qt::NoModifier, QPoint(30, 2 * row_height + row_height / 2));
    // The same display row now refers to a different instruction, and neither
    // display row nor dense line index is the decoder's instruction identifier.
    EXPECT_EQ(context.selected_lines, (std::vector<int>{112, 106}));
    QTest::mouseClick(instructions, Qt::LeftButton, Qt::NoModifier, QPoint(30, 10 * row_height));
    EXPECT_EQ(context.selected_lines.size(), 2); // No navigation on empty rows.
}

TEST_F(IsaViewerTest, LabelMinimapClickRevealsSectionUsingFoldedRowCoordinates)
{
    std::vector<std::string> code(1000, "v_add_f32 v0, v1, v2");
    code[0] = "label_a:";
    code[200] = "label_b:";
    code[600] = "label_c:";
    view->Populate(makeCode(code));
    view->toggleSection(0);
    view->toggleSection(600);
    LabelMinimap minimap;
    minimap.Populate();
    minimap.resize(500, 300);
    minimap.show();
    QApplication::processEvents();
    auto* table = minimap.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 3);
    QTest::mouseClick(
        table->viewport(), Qt::LeftButton, Qt::NoModifier, table->visualItemRect(table->item(2, 0)).center()
    );
    EXPECT_EQ(view->rowMapping().rowOf(601), 402); // Target section was expanded.
    EXPECT_EQ(view->rowMapping().rowOf(1), -1);    // Unrelated section stays folded.
    EXPECT_EQ(view->scrollbar->value(), 401 * QCodelist::lineheight() - view->height() / 3);
}

TEST_F(IsaViewerTest, IterationChangesRefreshPaintedLatencyAndItsMeasurement)
{
    auto code = makeCode({"v_add_f32 v0, v1, v2"});
    code[0].exec = std::make_unique<CodeData::Exec>(0);
    code[0].exec->latency = {7, 12345};
    view->Populate(code);
    auto* column = view->elements[ASMCodeline::ELATENCY];
    auto* selector = view->findChild<CycleModeSelector*>();
    ASSERT_NE(selector, nullptr);
    selector->setCurrentIndex(static_cast<int>(CyclesLabel::Strategy::ITERATION));
    auto* label = column->getelement(0);
    ASSERT_NE(label, nullptr);
    QFontMetrics metrics(column->font());
    for (const auto& [iteration, expected] : std::vector<std::pair<int, std::string>>{
             {-1, "0"    },
             {0,  "7"    },
             {1,  "12345"},
             {2,  "0"    },
             {0,  "7"    }
    })
    {
        context.iteration = iteration;
        column->grab(); // Repaint without repopulating or changing strategy.
        EXPECT_EQ(label->getStdText(), expected);
        EXPECT_EQ(label->width(metrics), metrics.horizontalAdvance(QString::fromStdString(expected)));
    }
    selector->setCurrentIndex(static_cast<int>(CyclesLabel::Strategy::SUM));
    column->grab();
    EXPECT_EQ(label->getStdText(), "12352");
}

TEST_F(IsaViewerTest, LosingHiddenLatencyAnalysisDisablesItsViewAndFallsBack)
{
    QComboBox* selector = nullptr;
    for (auto* combo : view->findChildren<QComboBox*>())
        if (combo->findData(QString("nonhidden_latency")) >= 0) selector = combo;
    ASSERT_NE(selector, nullptr);
    const auto enabled = [&]
    {
        return selector->model()->flags(selector->model()->index(selector->findData(QString("nonhidden_latency")), 0)) &
               Qt::ItemIsEnabled;
    };
    EXPECT_FALSE(enabled());
    context.hidden_latency_available = true;
    view->refreshAnnotations();
    EXPECT_TRUE(enabled());
    selector->setCurrentIndex(selector->findData(QString("nonhidden_latency")));
    EXPECT_EQ(Canvas::active_annotation_id, "nonhidden_latency");
    context.hidden_latency_available = false;
    view->refreshAnnotations();
    EXPECT_FALSE(enabled());
    EXPECT_EQ(Canvas::drawtype, Canvas::DrawType::DrawArrows);
    EXPECT_TRUE(Canvas::active_annotation_id.empty());
    EXPECT_EQ(selector->currentIndex(), 0);
}

TEST_F(IsaViewerTest, SourceSelectionUsesStableIndexNotSparseDecoderLineNumber)
{
    SourceLine source(0, "kernel();", nullptr);
    source.refs.push_back(ASMCodeline::line_vec[2]);
    view->setFoldingEnabled(true);
    view->toggleSection(0);
    source.onMousePress();
    EXPECT_GE(view->rowMapping().rowOf(2), 0);
}

TEST_F(IsaViewerTest, SourceReferencesSkipMissingSnapshotsAndPreserveCallstackOrder)
{
    SourceFile file("kernel.cpp", "", nullptr);
    auto first = std::make_shared<SourceLine>(0, "first();", &file);
    auto second = std::make_shared<SourceLine>(1, "second();", &file);
    SourceLine::all_lines["kernel.cpp:1"] = first;
    SourceLine::all_lines["kernel.cpp:2"] = second;
    CodeData data(
        0, 2, 0, 0, 10, 0, 0, 0, 0, "v_add_f32", "missing:1 -> kernel.cpp:1 ->  -> kernel.cpp:2 -> missing:9", {}
    );
    ASMLine instruction(0, *data.line);
    ASSERT_EQ(instruction.line_ref.size(), 2);
    EXPECT_EQ(instruction.line_ref[0].lock(), first);
    EXPECT_EQ(instruction.line_ref[1].lock(), second);
    EXPECT_EQ(instruction.callstack()[0].first, "kernel.cpp:1");
    EXPECT_EQ(instruction.callstack()[1].first, "kernel.cpp:2");
    EXPECT_EQ(first->hotspot.sqtt.latency, 10);
    EXPECT_EQ(second->hotspot.sqtt.latency, 10);
    SourceLine::all_lines.clear();
}

TEST_F(IsaViewerTest, CollapsedArrowEndpointsAreNotDrawnOnAnotherRow)
{
    QImage empty(200, 200, QImage::Format_ARGB32_Premultiplied);
    empty.fill(Qt::transparent);
    const auto render_arrow = [&]
    {
        QImage image = empty;
        QPainter painter(&image);
        QColor color(Qt::red);
        view->connector->Connect(painter, 103, 112, 0, color);
        painter.end();
        return image;
    };
    EXPECT_NE(render_arrow(), empty);
    view->toggleSection(0);
    EXPECT_EQ(render_arrow(), empty);
    view->expandAll();
    EXPECT_NE(render_arrow(), empty);
}

TEST_F(IsaViewerTest, AnnotationBarsFollowVisibleRowsAfterFolding)
{
    auto category = std::make_unique<Annotation::Category>();
    category->id = "test";
    category->display_name = "Test bars";
    category->per_line[1].rows[0].push_back({10, QColor(Qt::red)});
    category->per_line[4].rows[0].push_back({10, QColor(Qt::blue)});
    Annotation::Registry::instance().publish(std::move(category));
    view->refreshAnnotations();
    view->selectAnnotation("test");
    view->setFoldingEnabled(true);
    view->toggleSection(0);
    for (double scale : {1.0, 0.5})
    {
        context.scale = scale;
        const auto image = view->connector->grab().toImage();
        // Label a is row 0, label b row 1, endpgm row 2; hidden add's red bar is
        // absent. Display scaling must not move bars away from their text rows.
        const int row_height = QCodelist::lineheight();
        EXPECT_EQ(image.pixelColor(10, 2 * row_height + row_height / 2), QColor(Qt::blue));
        for (int y = 0; y < image.height(); ++y) EXPECT_NE(image.pixelColor(10, y), QColor(Qt::red));
    }
}

TEST_F(IsaViewerTest, FontThemeAndViewportChangesKeepRowsAligned)
{
    view->Populate(makeCode(
        {"label_vector:",
         "v_add_f32 v0, v1, v2",
         "v_wmma_f32_16x16x16_f16 v[0:7], v[8:15]",
         "v_mfma_f32_16x16x4f32 a[0:3], v0, v1, a[0:3]",
         "s_add_u32 s0, s1, s2",
         "s_and_saveexec_b64 s[0:1], vcc",
         "s_cbranch_scc0 label_memory",
         "s_load_dwordx4 s[0:3], s[4:5], 0",
         "s_waitcnt vmcnt(0)",
         "s_wait_depctr 0",
         "s_delay_alu 0",
         "buffer_nop",
         "unknown v0",
         "label_memory:",
         "global_load_dword v0, v[1:2], off",
         "ds_read_b32 v0, v1",
         "scratch_store_dword off, v0, s0",
         "s_sendmsg sendmsg(MSG_INTERRUPT)",
         "s_endpgm"}
    ));
    view->setFoldingEnabled(true);
    for (bool dark : {false, true})
    {
        WindowColors::setDark(dark);
        context.font.setPointSize(dark ? 13 : 10);
        view->refreshLayout();
        QApplication::processEvents();
        const int row_height = QCodelist::lineheight();
        for (auto* column : view->elements)
        {
            EXPECT_EQ(column->getLineIndex(3 * row_height + 1), 3);
            if (!column->isHidden()) EXPECT_EQ(column->height(), view->connector->height());
        }
        EXPECT_EQ(
            view->scrollbar->maximum(), std::max(0, view->rowMapping().count() * row_height - view->connector->height())
        );
        // Optional artifacts for visual review; normal test runs write nothing.
        const QString directory = qEnvironmentVariable("RCV_TEST_SCREENSHOT_DIR");
        if (!directory.isEmpty())
            EXPECT_TRUE(view->grab().save(QDir(directory).filePath(dark ? "isa-dark.png" : "isa-light.png")));
    }
}

TEST_F(IsaViewerTest, WidthsPersistAcrossPopulationVisibilityAndViewerRecreation)
{
    auto* header = view->findChild<QHeaderView*>();
    ASSERT_NE(header, nullptr);
    header->resizeSection(ASMCodeline::EASM + 1, 275);
    header->resizeSection(ASMCodeline::EADDRESS + 1, 125);
    EXPECT_EQ(AppConfig::getInstance().getColumnWidth(ASMCodeline::EASM), 275);
    view->setColumnVisibility(ASMCodeline::EADDRESS, false);
    view->refreshLayout();
    view->setColumnVisibility(ASMCodeline::EADDRESS, true);
    EXPECT_EQ(view->elements[ASMCodeline::EADDRESS]->width(), 125);
    view->Populate(makeCode({"v_add_f32", "s_endpgm"}));
    QApplication::processEvents();
    EXPECT_EQ(view->elements[ASMCodeline::EASM]->width(), 275);
    view.reset();
    view = std::make_unique<QCodelist>(context);
    EXPECT_EQ(view->elements[ASMCodeline::EASM]->width(), 275);
}

TEST_F(IsaViewerTest, DraggingAndDoubleClickingDividerRestoresAutomaticSizing)
{
    auto* header = view->findChild<QHeaderView*>();
    ASSERT_NE(header, nullptr);
    const int column = ASMCodeline::EHIT + 1;
    const int automatic_width = header->sectionSize(column);
    const QPoint divider(header->sectionViewportPosition(column) + automatic_width - 1, header->height() / 2);
    QTest::mousePress(header->viewport(), Qt::LeftButton, Qt::NoModifier, divider);
    QTest::mouseMove(header->viewport(), divider + QPoint(75, 0));
    QTest::mouseRelease(header->viewport(), Qt::LeftButton, Qt::NoModifier, divider + QPoint(75, 0));
    EXPECT_EQ(view->elements[ASMCodeline::EHIT]->width(), automatic_width + 75);
    EXPECT_EQ(AppConfig::getInstance().getColumnWidth(ASMCodeline::EHIT), automatic_width + 75);
    QTest::mouseDClick(header->viewport(), Qt::LeftButton, Qt::NoModifier, divider + QPoint(75, 0));
    EXPECT_EQ(view->elements[ASMCodeline::EHIT]->width(), automatic_width);
    EXPECT_EQ(AppConfig::getInstance().getColumnWidth(ASMCodeline::EHIT), -1);
    context.font.setPointSize(19);
    view->refreshLayout();
    QApplication::processEvents();
    EXPECT_GT(view->elements[ASMCodeline::EHIT]->width(), automatic_width);
}

TEST_F(IsaViewerTest, AutomaticLatencyWidthTracksFontSizeAndKeepsManualWidths)
{
    auto* selector = view->findChild<CycleModeSelector*>();
    auto* header = view->findChild<QHeaderView*>();
    ASSERT_NE(selector, nullptr);
    ASSERT_NE(header, nullptr);
    auto& config = AppConfig::getInstance();
    const int latency_column = ASMCodeline::ELATENCY + 1;
    const auto check_font = [&](int size)
    {
        context.font.setPointSize(size);
        view->refreshLayout();
        QApplication::processEvents();
        EXPECT_EQ(selector->font().pointSize(), size);
        EXPECT_EQ(header->font().pointSize(), size);
        EXPECT_GE(selector->width(), selector->sizeHint().width());
        // Automatic sizing must not become a persisted pixel override.
        EXPECT_EQ(config.getColumnWidth(ASMCodeline::ELATENCY), -1);
        return header->sectionSize(latency_column);
    };
    const int large_width = check_font(11);
    const int small_width = check_font(9);
    EXPECT_LT(small_width, large_width);
    EXPECT_EQ(check_font(11), large_width);
    EXPECT_EQ(check_font(9), small_width);

    header->resizeSection(latency_column, 300);
    context.font.setPointSize(11);
    view->refreshLayout();
    QApplication::processEvents();
    EXPECT_EQ(header->sectionSize(latency_column), 300);
    EXPECT_EQ(config.getColumnWidth(ASMCodeline::ELATENCY), 300);
}

TEST_F(IsaViewerTest, FontRefreshPreservesFoldedScrollAnchorBeforePainting)
{
    std::vector<std::string> lines(1000, "v_add_f32 v0, v1, v2");
    lines[0] = "label_a:";
    lines[20] = "label_b:";
    view->Populate(makeCode(lines));
    view->toggleSection(0);
    view->scrollbar->setValue(80 * QCodelist::lineheight() + 3);
    auto* instruction = view->elements[ASMCodeline::EASM];
    const int anchor = instruction->getLineIndex(0);
    auto* header = view->findChild<QHeaderView*>();
    header->resizeSection(ASMCodeline::EASM + 1, 275);
    const int latency_width = view->elements[ASMCodeline::ELATENCY]->width();
    view->hide();

    context.font.setPointSize(16);
    view->refreshLayout();

    // No event processing or painting: scrolling and hit testing must already
    // use the new metrics, with the same instruction at the top of the viewport.
    EXPECT_EQ(instruction->getLineIndex(0), anchor);
    EXPECT_EQ(instruction->getLineIndex(QFontMetrics(instruction->font()).height() + 1), anchor + 1);
    EXPECT_EQ(view->scrollbar->value(), 80 * QFontMetrics(instruction->font()).height());
    EXPECT_GT(view->elements[ASMCodeline::ELATENCY]->width(), latency_width);
    EXPECT_EQ(instruction->width(), 275);
    EXPECT_EQ(AppConfig::getInstance().getColumnWidth(ASMCodeline::EASM), 275);
    EXPECT_EQ(AppConfig::getInstance().getColumnWidth(ASMCodeline::ELATENCY), -1);
}

TEST_F(IsaViewerTest, OnlyMnemonicIsColoredWithoutChangingFontWeightOrPainterState)
{
    for (bool dark : {false, true})
        for (const char* family : {"monospace", "serif"})
            for (const std::string instruction :
                 {"s_add_f32 s0, s1, s2",
                  " \tv_add_f32\tv0, v1, v2",
                  "s_branch label_a",
                  "ds_read_b32 v0, v1",
                  "s_load_dword s0, s[2:3], 0",
                  "global_load_dword v0, v[1:2], off",
                  "s_waitcnt vmcnt(0)",
                  "s_nop 0",
                  "s_sendmsg sendmsg(MSG_INTERRUPT)",
                  "scratch_load_dword v0, off, s0",
                  "s_endpgm",
                  "s_wait_depctr 0",
                  "s_wait_alu 0",
                  "s_set_vgpr_count 1",
                  "s_delay_alu 0",
                  "buffer_nop",
                  "buffer_unknown v0",
                  "image_unknown v0",
                  "v_label:",
                  "; v_add_f32",
                  "unknown v0, v1"})
            {
                SCOPED_TRACE(instruction + "/" + family + (dark ? "/dark" : "/light"));
                WindowColors::setDark(dark);
                const auto code = makeCode({instruction});
                ASMLine styled(0, *code[0].line);
                TextLineElement plain(instruction);
                QFont font(family, dark ? 16 : 12); // font and theme changes must not leave stale geometry
                font.setStyleStrategy(QFont::NoAntialias);
                const auto mnemonic = Isa::instructionMnemonic(instruction);
                const int end =
                    mnemonic.empty() ? 0 : static_cast<int>(mnemonic.data() - instruction.data() + mnemonic.size());
                auto render = [&](TextLineElement& element)
                {
                    QImage image(900, 64, QImage::Format_ARGB32_Premultiplied);
                    image.fill(WindowColors::Background());
                    QPainter painter(&image);
                    painter.setRenderHint(QPainter::TextAntialiasing, false);
                    painter.setFont(font);
                    QPen pen(WindowColors::textColor(), 2);
                    painter.setPen(pen);
                    element.paint(painter, 0, 45, 45, 3);
                    EXPECT_EQ(painter.pen(), pen);
                    EXPECT_EQ(painter.font(), font);
                    return image;
                };
                auto image = render(styled);
                const auto reference = render(plain);
                const auto expected = Isa::instructionColor(styled.instruction_kind, dark, WindowColors::textColor());
                int colored_pixels = 0;
                int operand_pixels = 0;
                const int operand_x =
                    3 + QFontMetrics(font).horizontalAdvance(QString::fromStdString(instruction.substr(0, end)));
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x)
                    {
                        if (expected != WindowColors::textColor() && image.pixelColor(x, y) == expected)
                        {
                            ++colored_pixels;
                            EXPECT_LT(x, operand_x + 1); // operands and comments are never tinted
                            image.setPixelColor(x, y, WindowColors::textColor());
                        }
                        else if (x > operand_x && image.pixelColor(x, y) == WindowColors::textColor())
                            ++operand_pixels;
                    }
                EXPECT_EQ(image, reference); // tint changes neither font weight nor glyph positions, including tabs
                if (styled.instruction_kind != Isa::InstructionKind::Other)
                {
                    EXPECT_GT(colored_pixels, 0); // includes operand-free s_endpgm in both themes
                    if (end < static_cast<int>(instruction.size())) EXPECT_GT(operand_pixels, 0);
                }
                else
                    EXPECT_EQ(colored_pixels, 0);

                // Styling retains the full-width hover and source-reference backgrounds.
                for (bool hover : {false, true})
                {
                    styled.setRefHighlight(true, false);
                    plain.setRefHighlight(true, false);
                    styled.setMouseHover(hover);
                    plain.setMouseHover(hover);
                    auto highlighted = render(styled);
                    for (int y = 0; y < highlighted.height(); ++y)
                        for (int x = 0; x < highlighted.width(); ++x)
                            if (highlighted.pixelColor(x, y) == expected)
                                highlighted.setPixelColor(x, y, WindowColors::textColor());
                    EXPECT_EQ(highlighted, render(plain));
                }
            }
}

TEST_F(IsaViewerTest, MnemonicPaletteHasReadableContrastOnBothRowBackgrounds)
{
    auto luminance = [](const QColor& color)
    {
        auto linear = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
    };
    for (bool dark : {false, true})
    {
        WindowColors::setDark(dark);
        for (auto kind :
             {Isa::InstructionKind::Vector,
              Isa::InstructionKind::Matrix,
              Isa::InstructionKind::ScalarMemory,
              Isa::InstructionKind::Wait,
              Isa::InstructionKind::ScalarAlu,
              Isa::InstructionKind::Memory,
              Isa::InstructionKind::Branch,
              Isa::InstructionKind::Lds,
              Isa::InstructionKind::Scratch,
              Isa::InstructionKind::Control})
        {
            const double foreground = luminance(Isa::instructionColor(kind, dark, WindowColors::textColor()));
            for (const QColor background : {WindowColors::Background(), QColor(WindowColors::StripeBackground())})
            {
                const double bg = luminance(background);
                EXPECT_GE((std::max(foreground, bg) + 0.05) / (std::min(foreground, bg) + 0.05), 4.5)
                    << "kind=" << static_cast<int>(kind) << ", dark=" << dark;
            }
        }
    }
}

TEST_F(IsaViewerTest, HorizontalOverflowStartsLeftAndKeepsDeliberateScrolling)
{
    QScrollArea area;
    area.setWidgetResizable(true);
    area.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area.setWidget(view.get());
    area.resize(500, 350);
    area.show();
    QApplication::processEvents();
    auto* bar = area.horizontalScrollBar();
    EXPECT_GT(bar->maximum(), 0);
    EXPECT_EQ(bar->value(), bar->minimum());
    view->Populate(makeCode({"label_a:", "v_add_f32 v0, v1, v2", "s_endpgm"}));
    QTest::qWait(10); // allow any queued layout/scroll callbacks to fire
    EXPECT_EQ(bar->value(), bar->minimum());
    bar->setValue(100);
    view->setFoldingEnabled(false);
    view->refreshLayout();
    area.resize(510, 360);
    QApplication::processEvents();
    EXPECT_EQ(bar->value(), 100);
    area.takeWidget(); // the fixture retains ownership
}

struct PaintCounts
{
    int widths = 0;
    int paints = 0;
};
class CountingElement : public TextLineElement
{
public:
    explicit CountingElement(PaintCounts& counts) : TextLineElement("1234"), counts(counts) {}
    int width(QFontMetrics& fm) override
    {
        ++counts.widths;
        return TextLineElement::width(fm);
    }
    void paint(QPainter& painter, int x, int y, int step, int overline) override
    {
        ++counts.paints;
        TextLineElement::paint(painter, x, y, step, overline);
    }
    PaintCounts& counts;
};

TEST_F(IsaViewerTest, DeepScrollingAndResizingOnlyVisitVisibleRows)
{
    const auto small_listing_widgets = view->findChildren<QWidget*>().size();
    view->Populate(makeCode(std::vector<std::string>(25000, "v_add_f32 v0, v1, v2")));
    PaintCounts counts;
    for (auto& line : ASMCodeline::line_vec)
        line->elements[ASMCodeline::EHIT] = std::make_unique<CountingElement>(counts);
    view->refreshLayout();
    QApplication::processEvents();
    const auto widgets = view->findChildren<QWidget*>().size();
    EXPECT_EQ(widgets, small_listing_widgets); // widget count does not scale with the listing
    counts = {};
    view->scrollbar->setValue(24000 * QCodelist::lineheight() + 3);
    view->resize(view->width() + 50, view->height() + 20);
    QApplication::processEvents();
    view->elements[ASMCodeline::EHIT]->grab();
    EXPECT_GT(counts.paints, 0);
    EXPECT_LT(counts.paints, 200);
    EXPECT_LT(counts.widths, 500);
    auto* header = view->findChild<QHeaderView*>();
    counts = {};
    header->resizeSection(ASMCodeline::EHIT + 1, 130);
    QApplication::processEvents();
    EXPECT_LT(counts.widths, 500);
    EXPECT_EQ(view->findChildren<QWidget*>().size(), widgets);
}

TEST_F(IsaViewerTest, TooltipEscapesMarkupAndClearsOnEmptyRows)
{
    view->Populate(makeCode({"v_add_f32 v0, <value>&"}));
    auto* column = view->elements[ASMCodeline::EASM];
    QTest::mouseMove(column, QPoint(30, 5));
    EXPECT_TRUE(column->toolTip().contains("&lt;value&gt;&amp;"));
    QTest::mouseMove(column, QPoint(30, 100));
    EXPECT_TRUE(column->toolTip().isEmpty());
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestConfig::IsolatedSettings settings;
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
