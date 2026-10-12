// MIT License
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#include <gtest/gtest.h>
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <fstream>
#include "data/json_emitter.h"
#include "data/load_validation.h"
#include "data/record_handlers.h"
#include "mainwindow.h"
#include "wave/overlay_utils.h"
#include "wave/waveglobal.h"

// Substitute only the application boundary and unused lazy wave loading.
std::vector<QColor> MainWindow::dispatchcolors{Qt::green};
void MainWindow::incrementGlobalViewMipmap(int, int) {}
std::shared_ptr<WaveInstance> DataStore::getWave(const WaveEntry&) { return nullptr; }

namespace
{
trace_event_record_t eventAt(int64_t time)
{
    trace_event_record_t event{};
    event.time = time;
    event.type = ROCPROF_TRACE_DECODER_EVENT_CS_PARTIAL_FLUSH;
    return event;
}

QImage renderRow(QOutsideWaveView& row, int offset = 0)
{
    QImage image(256, row.height(), QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    row.render(&painter, QPoint(), QRegion(offset, 0, image.width(), image.height()));
    return image;
}

class GlobalViewTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        DataStore empty;
        QGlobalView reset(empty);
        reset.SetMip(10);
        reset.SetDecoderEventGroups(WaveOverlay::DecoderEventDefaultGlobalGroups);
    }
};
} // namespace

TEST_F(GlobalViewTest, LoadsAndRendersEventsWithoutWaves)
{
    // Same schema and null wave/code fields as the reported gfx12 trace.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto write = [&](const char* name, const nlohmann::json& data)
    { std::ofstream(dir.path().toStdString() + "/" + name) << data; };
    write(
        "filenames.json",
        {
            {"version",        "3.1.0"},
            {"gfxip",          12     },
            {"wave_filenames", nullptr}
    }
    );
    write(
        "code.json",
        {
            {"version", "3.1.0"},
            {"code",    nullptr}
    }
    );
    write(
        "occupancy.json",
        {
            {"version", "3.1.0"},
            {"events",
             {{"0",
               nlohmann::json::array(
                   {{{"kind", "event"}, {"time", 1464}, {"type", ROCPROF_TRACE_DECODER_EVENT_CS_PARTIAL_FLUSH}},
                    {{"kind", "event"}, {"time", 43567339}, {"type", ROCPROF_TRACE_DECODER_EVENT_CACHE_FLUSH}}}
               )}}             }
    }
    );

    DataStore store;
    RecordDispatcher dispatcher;
    OccupancyHandler occupancy(store);
    dispatcher.addHandler(&occupancy);
    JsonRecordEmitter emitter(dir.path().toStdString(), dispatcher, store);
    emitter.run();

    ASSERT_EQ(store.trace_events_by_se.at(0).size(), 2u);
    ASSERT_TRUE(store.occupancy_by_se.empty());
    ASSERT_TRUE(store.wave_hierarchy.empty());
    ASSERT_TRUE(store.code.empty());
    InputInfo input;
    input.type = InputType::JSON_DIR;
    EXPECT_EQ(validateLoadedData(input, &store, false).status, LoadValidationStatus::Success);

    auto* view = new QGlobalView(store);
    view->SetMip(10);
    QScrollArea scroll;
    scroll.resize(400, 100);
    scroll.setWidgetResizable(true);
    scroll.setWidget(view);
    scroll.show();
    QApplication::processEvents();

    auto rows = view->findChildren<QOutsideWaveView*>();
    ASSERT_EQ(rows.size(), 1);
    auto* row = rows.front();
    EXPECT_EQ(row->se, 0);
    EXPECT_EQ(row->cu, -1); // The events have no CU/SIMD/slot location.
    EXPECT_EQ(row->simd, -1);
    EXPECT_EQ(row->slot, -1);
    EXPECT_GE(row->height(), row->fontMetrics().height());
    EXPECT_EQ(QGlobalView::PosToClock(0), 1464);
    EXPECT_EQ(QGlobalView::MaxTime(), 43567339);
    EXPECT_GT(row->sizeHint().width(), QGlobalView::ClockToPos(43567339));
    EXPECT_GT(scroll.horizontalScrollBar()->maximum(), 0);

    const QImage shown = renderRow(*row);
    view->SetDecoderEventGroups(0);
    EXPECT_NE(shown, renderRow(*row));

    // A late event must remain reachable when the timeline has no wave bars.
    const int offset = QGlobalView::ClockToPos(43567339) - 128;
    const QImage hidden_end = renderRow(*row, offset);
    view->SetDecoderEventGroups(WaveOverlay::DecoderEventAllGroups);
    EXPECT_NE(hidden_end, renderRow(*row, offset));

    view->SetMip(9);
    QApplication::processEvents();
    EXPECT_GT(row->sizeHint().width(), 80000);
}

TEST_F(GlobalViewTest, CreatesRowsForEventAndDispatchOnlyShaderEngines)
{
    DataStore store;
    store.trace_events_by_se[3] = {eventAt(100)};
    dispatch_record_t dispatch{};
    dispatch.time = 500;
    store.dispatch_records_by_se[7] = {dispatch};
    store.trace_events_by_se[8] = {};
    store.dispatch_records_by_se[9] = {};

    QGlobalView view(store);
    const auto rows = view.findChildren<QOutsideWaveView*>();
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0]->se, 3);
    EXPECT_EQ(rows[1]->se, 7);
    EXPECT_EQ(QGlobalView::PosToClock(0), 100);
    EXPECT_EQ(QGlobalView::MaxTime(), 500);
    view.SetMip(0);
    rows[1]->resize(rows[1]->sizeHint());
    const QImage shown = renderRow(*rows[1], 300);
    view.SetDecoderEventGroups(0);
    EXPECT_NE(shown, renderRow(*rows[1], 300));

    store.trace_events_by_se.clear();
    InputInfo input;
    input.type = InputType::JSON_DIR;
    EXPECT_EQ(validateLoadedData(input, &store, false).status, LoadValidationStatus::Success);
}

TEST_F(GlobalViewTest, KeepsWaveRowsAndAddsMissingShaderEngineEvents)
{
    DataStore store;
    occupancy_record_t start{};
    start.time = 200;
    start.start = 1;
    start.cu = 2;
    auto end = start;
    end.time = 300;
    end.start = 0;
    store.occupancy_by_se[1] = {start, end};
    store.trace_events_by_se[1] = {eventAt(100)};
    store.trace_events_by_se[0] = {eventAt(400)};
    // An orphan end sample does not create a wave row, but events still need one.
    store.occupancy_by_se[2] = {end};
    store.trace_events_by_se[2] = {eventAt(500)};
    QGlobalView view(store);
    const auto rows = view.findChildren<QOutsideWaveView*>();
    ASSERT_EQ(rows.size(), 3);
    EXPECT_EQ(rows[0]->se, 0);
    EXPECT_EQ(rows[0]->cu, -1);
    EXPECT_EQ(rows[1]->se, 1);
    EXPECT_EQ(rows[1]->cu, 2);
    EXPECT_EQ(rows[2]->se, 2);
    EXPECT_EQ(rows[2]->cu, -1);
}

TEST_F(GlobalViewTest, RejectsEmptyEventCollections)
{
    DataStore store;
    store.trace_events_by_se[0] = {};
    store.dispatch_records_by_se[1] = {};
    InputInfo input;
    input.type = InputType::JSON_DIR;
    EXPECT_EQ(validateLoadedData(input, &store, false).status, LoadValidationStatus::LoadFailed);
    QGlobalView view(store);
    EXPECT_TRUE(view.findChildren<QOutsideWaveView*>().empty());
    EXPECT_EQ(QGlobalView::PosToClock(0), 0);
    EXPECT_EQ(QGlobalView::MaxTime(), 0);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
