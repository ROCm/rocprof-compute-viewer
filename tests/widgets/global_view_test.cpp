// MIT License
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#include <gtest/gtest.h>
#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTemporaryDir>
#include <fstream>
#include <optional>
#include "data/json_emitter.h"
#include "data/load_validation.h"
#include "data/record_handlers.h"
#include "mainwindow.h"
#include "wave/event_track.h"
#include "wave/overlay_utils.h"
#include "wave/waveglobal.h"

// Substitute only the application boundary and unused lazy wave loading.
std::vector<QColor> MainWindow::dispatchcolors{Qt::green};
void MainWindow::incrementGlobalViewMipmap(int, int) {}
std::shared_ptr<WaveInstance> DataStore::getWave(const WaveEntry&) { return nullptr; }

namespace
{
trace_event_record_t eventAt(
    int64_t time, int type = ROCPROF_TRACE_DECODER_EVENT_CS_PARTIAL_FLUSH, int me = 0, int pipe = 0, uint64_t payload = 0
)
{
    trace_event_record_t event{};
    event.time = time;
    event.type = static_cast<decltype(event.type)>(type);
    event.me_id = me;
    event.pipe_id = pipe;
    event.payload.raw = payload;
    return event;
}

trace_event_record_t dispatchEndAt(int64_t time, uint64_t dispatch_id, int me = 0, int pipe = 0)
{
    return eventAt(time, ROCPROF_TRACE_DECODER_EVENT_DISPATCH_END, me, pipe, dispatch_id);
}

dispatch_record_t dispatchAt(int64_t time, int me = 0, int pipe = 0, uint64_t dispatch_id = 0)
{
    dispatch_record_t dispatch{};
    dispatch.time = time;
    dispatch.me_id = me;
    dispatch.pipe_id = pipe;
    dispatch.dispatch_id = dispatch_id;
    return dispatch;
}

/// Start/end occupancy pair for one wave in its own slot.
void addWave(DataStore& store, int se, int slot, int64_t begin, int64_t end, int me, int pipe)
{
    occupancy_record_t start{};
    start.time = begin;
    start.start = 1;
    start.wave_id = slot;
    start.me_id = me;
    start.pipe_id = pipe;
    auto stop = start;
    stop.time = end;
    stop.start = 0;
    auto& records = store.occupancy_by_se[se];
    records.push_back(start);
    records.push_back(stop);
}

QImage renderWidget(QWidget& widget)
{
    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    widget.render(&painter);
    return image;
}

const EventSpan* findSpan(const EventSubTrack& track, EventSpan::Kind kind, int64_t begin)
{
    for (const auto& span : track.spans)
        if (span.kind == kind && span.begin == begin) return &span;
    return nullptr;
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

    // Events no longer create placeholder wave rows; the timeline still spans them.
    EXPECT_TRUE(view->findChildren<QOutsideWaveView*>().empty());
    EXPECT_EQ(QGlobalView::PosToClock(0), 1464);
    EXPECT_EQ(QGlobalView::MaxTime(), 43567339);
    EXPECT_GT(view->minimumSizeHint().width(), QGlobalView::ClockToPos(43567339));
    EXPECT_GT(scroll.horizontalScrollBar()->maximum(), 0);

    auto model = view->EventModel();
    ASSERT_TRUE(model);
    ASSERT_EQ(model->tracks.size(), 1u);
    EXPECT_EQ(model->tracks[0].se, 0);
    EXPECT_EQ(model->tracks[0].lanes, 1);

    QEventTrack track(model);
    view->eventTrack = &track;
    view->setScrollArea(&scroll);
    // Mirrors the app: the track spans the label panel plus the scroll area.
    track.resize(QTickHeader::LEFT_MARGIN + scroll.width(), track.sizeHint().height());
    track.show();

    // A late event must remain reachable by scrolling the waves' scroll area.
    const int lane_y = QEventTrack::BAND_PAD_PX + track.laneHeight() / 2;
    scroll.horizontalScrollBar()->setValue(scroll.horizontalScrollBar()->maximum());
    const int event_x = QTickHeader::LEFT_MARGIN + QGlobalView::ClockToPos(43567339) -
                        scroll.horizontalScrollBar()->value();
    ASSERT_GT(event_x, QTickHeader::LEFT_MARGIN);
    ASSERT_LT(event_x, track.width());
    QColor flush = WindowColors::DecoderEventColor(ROCPROF_TRACE_DECODER_EVENT_CACHE_FLUSH);
    flush.setAlpha(255);
    EXPECT_EQ(renderWidget(track).pixelColor(event_x, lane_y), flush);

    const QImage shown = renderWidget(track);
    view->SetDecoderEventGroups(0);
    EXPECT_NE(shown, renderWidget(track));
    view->eventTrack = nullptr;

    view->SetMip(9);
    QApplication::processEvents();
    EXPECT_GT(view->minimumSizeHint().width(), 80000);
}

TEST_F(GlobalViewTest, JsonDispatchIdPairsDispatchEnd)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto write = [&](const char* name, const nlohmann::json& data)
    { std::ofstream(dir.path().toStdString() + "/" + name) << data; };
    write("filenames.json", {{"version", "3.1.0"}, {"gfxip", 12}, {"wave_filenames", nullptr}});
    write("code.json", {{"version", "3.1.0"}, {"code", nullptr}});

    // time, cu, simd, wave_id, start, kernel_id, me_id, pipe_id, is_ext, workgroup_id, cluster_id, dispatch_id
    nlohmann::json rows = nlohmann::json::array();
    rows.push_back(nlohmann::json::array({110, 0, 1, 0, 1, 0, 1, 2, 0, 0, 0, 5}));
    rows.push_back(nlohmann::json::array({300, 0, 1, 0, 0, 0, 1, 2, 0, 0, 0, 5}));

    nlohmann::json timeline = nlohmann::json::array();
    timeline.push_back(
        {{"kind", "dispatch"}, {"time", 100}, {"me_id", 1}, {"pipe_id", 2}, {"kernel_id", 0}, {"dispatch_id", 5}}
    );
    timeline.push_back(
        {{"kind", "event"},
         {"time", 300},
         {"type", ROCPROF_TRACE_DECODER_EVENT_DISPATCH_END},
         {"me_id", 1},
         {"pipe_id", 2},
         {"payload", 5}}
    );

    nlohmann::json occupancy_json;
    occupancy_json["version"] = "3.1.0";
    occupancy_json["0"] = rows;
    occupancy_json["events"]["0"] = timeline;
    write("occupancy.json", occupancy_json);

    DataStore store;
    RecordDispatcher dispatcher;
    OccupancyHandler occupancy(store);
    dispatcher.addHandler(&occupancy);
    JsonRecordEmitter emitter(dir.path().toStdString(), dispatcher, store);
    emitter.run();

    ASSERT_EQ(store.dispatch_records_by_se.at(0).size(), 1u);
    EXPECT_EQ(store.dispatch_records_by_se.at(0)[0].dispatch_id, 5u);
    ASSERT_EQ(store.occupancy_by_se.at(0).size(), 2u);
    EXPECT_EQ(store.occupancy_by_se.at(0)[0].dispatch_id, 5u);

    QGlobalView view(store);
    auto model = view.EventModel();
    ASSERT_TRUE(model);
    const EventSpan* range = findSpan(model->tracks[0], EventSpan::Kind::Dispatch, 100);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->end, 300);
}

TEST_F(GlobalViewTest, EventOnlyShaderEnginesGetEventSubTracks)
{
    DataStore store;
    store.trace_events_by_se[3] = {eventAt(100)};
    store.dispatch_records_by_se[7] = {dispatchAt(500)};
    store.trace_events_by_se[8] = {};
    store.dispatch_records_by_se[9] = {};

    QGlobalView view(store);
    EXPECT_TRUE(view.findChildren<QOutsideWaveView*>().empty());
    EXPECT_EQ(QGlobalView::PosToClock(0), 100);
    EXPECT_EQ(QGlobalView::MaxTime(), 500);

    auto model = view.EventModel();
    ASSERT_TRUE(model);
    ASSERT_EQ(model->tracks.size(), 2u);
    EXPECT_EQ(model->tracks[0].se, 3);
    EXPECT_EQ(model->tracks[1].se, 7);
    EXPECT_EQ(model->tracks[1].spans[0].kind, EventSpan::Kind::Dispatch);
    EXPECT_FALSE(model->tracks[1].spans[0].IsRange());

    store.trace_events_by_se.clear();
    InputInfo input;
    input.type = InputType::JSON_DIR;
    EXPECT_EQ(validateLoadedData(input, &store, false).status, LoadValidationStatus::Success);
}

TEST_F(GlobalViewTest, EventsDoNotAddWaveRows)
{
    DataStore store;
    addWave(store, 1, 0, 200, 300, 0, 0);
    store.occupancy_by_se[1][0].cu = store.occupancy_by_se[1][1].cu = 2;
    store.trace_events_by_se[1] = {eventAt(100)};
    store.trace_events_by_se[0] = {eventAt(400)};
    // An orphan end sample does not create a wave row.
    store.occupancy_by_se[2] = {store.occupancy_by_se[1][1]};
    store.trace_events_by_se[2] = {eventAt(500)};
    QGlobalView view(store);
    const auto rows = view.findChildren<QOutsideWaveView*>();
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0]->se, 1);
    EXPECT_EQ(rows[0]->cu, 2);

    auto model = view.EventModel();
    ASSERT_TRUE(model);
    ASSERT_EQ(model->tracks.size(), 3u);
    EXPECT_EQ(model->tracks[0].se, 0);
    EXPECT_EQ(model->tracks[1].se, 1);
    EXPECT_EQ(model->tracks[2].se, 2);
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
    EXPECT_FALSE(view.EventModel());
    EXPECT_EQ(QGlobalView::PosToClock(0), 0);
    EXPECT_EQ(QGlobalView::MaxTime(), 0);
}

TEST_F(GlobalViewTest, DispatchRangeEndsAtMatchingDispatchEnd)
{
    DataStore store;
    store.dispatch_records_by_se[0] = {
        dispatchAt(1000, 1, 0, 2),
        dispatchAt(100, 1, 0, 1),
        dispatchAt(2000, 1, 1, 1), // Same ID on another pipe
        dispatchAt(3000, 0, 0, 0), // No dispatch ID: stays a point
    };
    store.trace_events_by_se[0] = {
        dispatchEndAt(500, 1, 1, 0),
        dispatchEndAt(1500, 2, 3, 0), // ME 3 shares the queue with ME 1
        dispatchEndAt(2500, 1, 1, 1),
        dispatchEndAt(160, 7, 0, 0),  // No such dispatch
        dispatchEndAt(3100, 0, 0, 0), // ID 0 never pairs
    };
    addWave(store, 0, 0, 120, 900, 1, 0); // Waves don't extend the range: the dispatch end already covers them
    // A dispatch end on another SE never pairs with SE 0's dispatch.
    store.trace_events_by_se[1] = {dispatchEndAt(120, 1, 1, 0)};

    QGlobalView view(store);
    auto model = view.EventModel();
    ASSERT_TRUE(model);
    ASSERT_EQ(model->tracks.size(), 2u);
    const auto& se0 = model->tracks[0];

    const EventSpan* first = findSpan(se0, EventSpan::Kind::Dispatch, 100);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->end, 500);
    EXPECT_GE(first->kid, 0);
    EXPECT_EQ(se0.dispatches[first->index].time, 100);

    const EventSpan* second = findSpan(se0, EventSpan::Kind::Dispatch, 1000);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->end, 1500);
    EXPECT_EQ(second->kid, first->kid); // Same entry point, same kernel

    const EventSpan* other_pipe = findSpan(se0, EventSpan::Kind::Dispatch, 2000);
    ASSERT_NE(other_pipe, nullptr);
    EXPECT_EQ(other_pipe->end, 2500);

    const EventSpan* point = findSpan(se0, EventSpan::Kind::Dispatch, 3000);
    ASSERT_NE(point, nullptr);
    EXPECT_FALSE(point->IsRange());

    // Paired dispatch ends are folded into their ranges; unpaired ones stay as events.
    EXPECT_EQ(findSpan(se0, EventSpan::Kind::TraceEvent, 500), nullptr);
    EXPECT_EQ(findSpan(se0, EventSpan::Kind::TraceEvent, 1500), nullptr);
    EXPECT_EQ(findSpan(se0, EventSpan::Kind::TraceEvent, 2500), nullptr);
    EXPECT_NE(findSpan(se0, EventSpan::Kind::TraceEvent, 160), nullptr);
    EXPECT_NE(findSpan(se0, EventSpan::Kind::TraceEvent, 3100), nullptr);
    EXPECT_NE(findSpan(model->tracks[1], EventSpan::Kind::TraceEvent, 120), nullptr);
}

TEST_F(GlobalViewTest, OverlappingEventsPackIntoLanes)
{
    EventTrackModel model;
    model.AddShaderEngine(
        0,
        {dispatchAt(100, 0, 0, 1)},
        {eventAt(200), dispatchEndAt(300, 1), eventAt(400), eventAt(400)},
        {}
    );
    model.Layout(WaveOverlay::DecoderEventAllGroups);
    const auto& track = model.tracks[0];
    ASSERT_EQ(track.visible.size(), 4u);
    // Range [100, 300] in lane 0, the flush at 200 overlaps it; at 400 both lanes are free
    // again, but two events at the same time still need separate lanes.
    EXPECT_EQ(track.lanes, 2);
    EXPECT_EQ(track.max_len, 200);
    EXPECT_EQ(track.spans[track.visible[0]].lane, 0);
    EXPECT_EQ(track.spans[track.visible[1]].lane, 1);
    EXPECT_NE(track.spans[track.visible[2]].lane, track.spans[track.visible[3]].lane);

    // Filtering re-packs, so hidden groups don't leave empty lanes.
    model.Layout(WaveOverlay::DecoderEventGroupDispatch);
    EXPECT_EQ(track.lanes, 1);
    EXPECT_FALSE(model.AllHidden());
    model.Layout(0);
    EXPECT_EQ(track.lanes, 0);
    EXPECT_TRUE(model.AllHidden());
}

TEST_F(GlobalViewTest, FilteredSubTracksCollapse)
{
    auto model = std::make_shared<EventTrackModel>();
    model->AddShaderEngine(0, {}, {eventAt(100)}, {});
    model->AddShaderEngine(1, {}, {eventAt(100, ROCPROF_TRACE_DECODER_EVENT_GC_RINSE)}, {});

    QSplitter splitter(Qt::Vertical);
    auto* track = new QEventTrack(model);
    splitter.addWidget(track);
    splitter.addWidget(new QWidget());
    splitter.resize(400, 300);
    splitter.setSizes({80, 220});
    splitter.show();
    QApplication::processEvents();

    // GC rinse is hidden by default: only that SE's band thins out.
    EXPECT_GT(track->bandHeight(0), QEventTrack::THIN_BAND_PX);
    EXPECT_EQ(track->bandHeight(1), QEventTrack::THIN_BAND_PX);
    const int expanded = track->height();
    EXPECT_GT(expanded, QEventTrack::THIN_BAND_PX);

    track->SetDecoderEventGroups(WaveOverlay::DecoderEventGroupDispatch);
    QApplication::processEvents();
    EXPECT_EQ(track->height(), QEventTrack::THIN_BAND_PX);

    track->SetDecoderEventGroups(WaveOverlay::DecoderEventAllGroups);
    QApplication::processEvents();
    EXPECT_EQ(track->height(), expanded);
    EXPECT_GT(track->bandHeight(1), QEventTrack::THIN_BAND_PX);
}

TEST_F(GlobalViewTest, SubTracksCollapseToOneRow)
{
    auto model = std::make_shared<EventTrackModel>();
    model->AddShaderEngine(0, {}, {eventAt(100), eventAt(100), eventAt(100)}, {});
    model->AddShaderEngine(1, {}, {eventAt(100)}, {});
    QEventTrack track(model);
    track.resize(400, 200);
    ASSERT_EQ(model->tracks[0].lanes, 3);
    const int expanded = track.bandHeight(0);

    // Clicking the SE label toggles collapsing of bands with stacked lanes.
    auto click = [&](int y)
    {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, y), QPointF(10, y), Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(&track, &press);
    };
    click(QEventTrack::BAND_PAD_PX + 1);
    EXPECT_TRUE(track.isBandCollapsed(0));
    EXPECT_EQ(track.bandLanes(0), 1);
    EXPECT_LT(track.bandHeight(0), expanded);
    EXPECT_EQ(track.contentHeight(), track.bandHeight(0) + track.bandHeight(1));

    // Single-lane bands have nothing to collapse.
    click(track.bandHeight(0) + QEventTrack::BAND_PAD_PX + 1);
    EXPECT_FALSE(track.isBandCollapsed(1));

    click(QEventTrack::BAND_PAD_PX + 1);
    EXPECT_FALSE(track.isBandCollapsed(0));
    EXPECT_EQ(track.bandHeight(0), expanded);

    // Filtering still wins: a collapsed band with nothing visible stays thin.
    track.setBandCollapsed(0, true);
    track.SetDecoderEventGroups(WaveOverlay::DecoderEventGroupDispatch);
    EXPECT_EQ(track.bandHeight(0), QEventTrack::THIN_BAND_PX);
}

TEST_F(GlobalViewTest, RowsStayPackedWhenViewIsTallerThanContent)
{
    DataStore store;
    for (int se = 0; se < 3; ++se)
        for (int slot = 0; slot < 2; ++slot) addWave(store, se, slot, 100, 200, 0, 0);

    auto* view = new QGlobalView(store);
    QScrollArea scroll;
    scroll.setWidgetResizable(true);
    scroll.setWidget(view);
    scroll.resize(400, 600); // Much taller than the six rows
    scroll.show();
    QApplication::processEvents();

    // Extra height goes below the last row, so rows line up with the label panel.
    const auto rows = view->findChildren<QOutsideWaveView*>();
    ASSERT_EQ(rows.size(), 6);
    EXPECT_EQ(rows[0]->y(), 0);
    for (int i = 1; i < rows.size(); ++i) EXPECT_EQ(rows[i]->y(), rows[i - 1]->y() + rows[i - 1]->height());
}

TEST_F(GlobalViewTest, HoverPicksTheTopmostDrawnEvent)
{
    DataStore empty;
    QGlobalView view(empty);
    view.SetMip(0); // One pixel per cycle from clock 0

    // Dispatch range [100, 300] in lane 0, flush at 200 in lane 1.
    auto model = std::make_shared<EventTrackModel>();
    model->AddShaderEngine(
        0, {dispatchAt(100, 0, 0, 1)}, {eventAt(200), dispatchEndAt(300, 1)}, {}
    );
    QEventTrack track(model);
    track.resize(600, 100);
    ASSERT_EQ(model->tracks[0].lanes, 2);
    const auto& spans = model->tracks[0].spans;
    auto kindAt = [&](int clock, int lane, int dy = 0)
    {
        const QPoint pos(
            QTickHeader::LEFT_MARGIN + clock,
            QEventTrack::BAND_PAD_PX + lane * track.laneHeight() + track.laneHeight() / 2 + dy
        );
        const auto [band, span] = track.spanAt(pos);
        return span >= 0 ? std::optional(spans[span].kind) : std::nullopt;
    };

    EXPECT_EQ(kindAt(200, 0), EventSpan::Kind::Dispatch);
    EXPECT_EQ(kindAt(200, 1), EventSpan::Kind::TraceEvent);

    // Collapsed: the flush diamond is drawn over the range, so it wins under the cursor.
    track.setBandCollapsed(0, true);
    EXPECT_EQ(kindAt(200, 0), EventSpan::Kind::TraceEvent);
    EXPECT_EQ(kindAt(200 + track.markerRadius() + 2, 0), EventSpan::Kind::Dispatch);
    EXPECT_EQ(kindAt(150, 0), EventSpan::Kind::Dispatch);
    EXPECT_EQ(kindAt(450, 0), std::nullopt);

    // Within the diamond's horizontal reach but past its diagonal edge, the bar is what's visible.
    const int r = track.markerRadius();
    EXPECT_EQ(kindAt(200 + r / 2, 0, r / 2), EventSpan::Kind::TraceEvent);
    EXPECT_EQ(kindAt(200 + r - 1, 0, r - 1), EventSpan::Kind::Dispatch);
    EXPECT_EQ(kindAt(200, 0, r + 1), EventSpan::Kind::Dispatch);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
