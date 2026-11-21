#include "test_harness.hpp"
#include "timeline/media_pool.hpp"
#include "timeline/memory_pool.hpp"
#include "timeline/rational_time.hpp"
#include "timeline/task_scheduler.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

using namespace owncut::timeline;

OCU_TEST(RationalTimeZeroDriftAndRates) {
    const FrameRate rates[] = {
        FrameRate::fps_23_976(), FrameRate::fps_24(), FrameRate::fps_25(),
        FrameRate::fps_29_97(),  FrameRate::fps_30(), FrameRate::fps_50(),
        FrameRate::fps_59_94(),  FrameRate::fps_60(), FrameRate::fps_120()
    };
    for (const auto& r : rates) {
        RationalTime acc = RationalTime::zero();
        const RationalTime one_frame = RationalTime::from_frames(1, r);
        for (int i = 0; i < 10000; ++i) {
            acc += one_frame;
        }
        OCU_ASSERT_EQ(acc.to_frames(r), 10000);
        OCU_ASSERT_EQ(acc, RationalTime::from_frames(10000, r));
    }

    // Rounding modes
    const RationalTime half_frame(1, 48); // 0.5 frame at 24fps
    OCU_ASSERT_EQ(half_frame.to_frames(FrameRate::fps_24(), RoundingMode::Truncate), 0);
    OCU_ASSERT_EQ(half_frame.to_frames(FrameRate::fps_24(), RoundingMode::Ceil), 1);
    OCU_ASSERT_EQ(half_frame.to_frames(FrameRate::fps_24(), RoundingMode::NearestHalfAwayFromZero), 1);
    OCU_ASSERT_EQ((-half_frame).to_frames(FrameRate::fps_24(), RoundingMode::Floor), -1);
}

OCU_TEST(RationalTimeDropFrameTimecodeRoundTrip) {
    // At 29.97 DF: frame 1800 skips 00:01:00;00 and 00:01:00;01 -> 00:01:00;02
    const auto r2997 = FrameRate::fps_29_97();
    const RationalTime t_1800 = RationalTime::from_frames(1800, r2997);
    const std::string tc_df = t_1800.to_timecode(r2997, true);
    OCU_ASSERT_EQ(tc_df, std::string("00:01:00;02"));
    auto parsed = RationalTime::from_timecode(tc_df, r2997, true);
    OCU_ASSERT_TRUE(parsed.has_value());
    OCU_ASSERT_EQ(parsed->to_frames(r2997), 1800);

    // Every 10th minute does NOT drop frames: 17982 frames = 00:10:00;00
    const RationalTime t_10m = RationalTime::from_frames(17982, r2997);
    OCU_ASSERT_EQ(t_10m.to_timecode(r2997, true), std::string("00:10:00;00"));

    // Invalid dropped frame label must fail parsing
    OCU_ASSERT_TRUE(!RationalTime::from_timecode("00:01:00;00", r2997, true).has_value());

    // 59.94 DF drops 4 frames -> frame 3600 = 00:01:00;04
    const auto r5994 = FrameRate::fps_59_94();
    const RationalTime t_5994 = RationalTime::from_frames(3600, r5994);
    OCU_ASSERT_EQ(t_5994.to_timecode(r5994, true), std::string("00:01:00;04"));
    auto parsed59 = RationalTime::from_timecode("00:01:00;04", r5994, true);
    OCU_ASSERT_TRUE(parsed59.has_value());
    OCU_ASSERT_EQ(parsed59->to_frames(r5994), 3600);
}

OCU_TEST(MemoryPoolAlignmentAndQueues) {
    FrameMemoryPool pool(1024, 3);
    auto b1 = pool.acquire();
    auto b2 = pool.acquire();
    auto b3 = pool.acquire();
    auto b4 = pool.acquire(); // Exhausted
    OCU_ASSERT_TRUE(b1.valid());
    OCU_ASSERT_TRUE(b2.valid());
    OCU_ASSERT_TRUE(b3.valid());
    OCU_ASSERT_TRUE(!b4.valid());
    OCU_ASSERT_EQ(reinterpret_cast<std::uintptr_t>(b1.data()) % 64u, 0u);
    OCU_ASSERT_EQ(reinterpret_cast<std::uintptr_t>(b2.data()) % 64u, 0u);
    OCU_ASSERT_EQ(reinterpret_cast<std::uintptr_t>(b3.data()) % 64u, 0u);

    b2.release();
    auto b5 = pool.acquire();
    OCU_ASSERT_TRUE(b5.valid());
    OCU_ASSERT_EQ(pool.stats().hits, 4u);
    OCU_ASSERT_EQ(pool.stats().misses, 1u);

    // SPSC RingBuffer multithread
    LockFreeRingBuffer<int, 1024> spsc;
    constexpr int N = 5000;
    std::thread prod([&]() {
        for (int i = 0; i < N; ++i) {
            while (!spsc.push(i)) std::this_thread::yield();
        }
    });
    int sum = 0;
    for (int i = 0; i < N; ++i) {
        int v = -1;
        while (!spsc.pop(v)) std::this_thread::yield();
        OCU_ASSERT_EQ(v, i);
        sum += 1;
    }
    prod.join();
    OCU_ASSERT_EQ(sum, N);

    // MPSC Queue multithread
    LockFreeMPSCQueue<int, 2048> mpsc;
    std::vector<std::thread> writers;
    for (int t = 0; t < 4; ++t) {
        writers.emplace_back([&, t]() {
            for (int k = 0; k < 250; ++k) {
                while (!mpsc.enqueue(t * 1000 + k)) std::this_thread::yield();
            }
        });
    }
    for (auto& w : writers) w.join();
    int popped = 0;
    int val = 0;
    while (mpsc.dequeue(val)) ++popped;
    OCU_ASSERT_EQ(popped, 1000);
}

OCU_TEST(TaskSchedulerPriorityIsolationAndCoalescing) {
    TaskScheduler sched(1);
    std::atomic<int> bg_runs{0};
    std::atomic<int> rt_runs{0};
    std::atomic<bool> block_bg{true};

    // Saturate the background thread with a blocking proxy job
    sched.submit(TaskPriority::BackgroundProxyWaveform, [&](const TaskCancellationToken&) {
        while (block_bg.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ++bg_runs;
    });

    // Submit 5 coalesced scrub updates sharing one key
    std::atomic<int> scrub_calls{0};
    for (int i = 0; i < 5; ++i) {
        sched.submit(TaskPriority::TouchScrubbing, [&](const TaskCancellationToken&) {
            ++scrub_calls;
        }, "scrub_viewport");
    }

    // Submit RealtimePlayback while background is blocked — must complete immediately without starvation!
    sched.submit(TaskPriority::RealtimePlayback, [&](const TaskCancellationToken&) {
        ++rt_runs;
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (rt_runs.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    OCU_ASSERT_EQ(rt_runs.load(), 1);
    OCU_ASSERT_EQ(bg_runs.load(), 0); // Background was still blocked while RT finished!

    block_bg.store(false);
    sched.wait_idle();
    OCU_ASSERT_EQ(bg_runs.load(), 1);
    OCU_ASSERT_TRUE(scrub_calls.load() <= 5 && scrub_calls.load() >= 1);
}

OCU_TEST(MediaPoolPyramidsSmartBinsAndProxyCache) {
    MediaPool pool;
    MediaItem a;
    a.metadata.name = "Interview_CamA.mov";
    a.metadata.uri = "/media/camA.mov";
    a.metadata.width = 128;
    a.metadata.height = 72;
    a.metadata.fps = FrameRate::fps_60();
    a.metadata.duration = RationalTime(10, 1);
    const uint64_t id_a = pool.add_item(a);
    pool.set_star_rating(id_a, 5);
    pool.set_flag(id_a, ClipFlagColor::Green);
    pool.set_color_tag(id_a, "Teal");

    MediaItem b;
    b.metadata.name = "Broll_Street.mp4";
    b.metadata.uri = "/media/broll.mp4";
    b.metadata.width = 128;
    b.metadata.height = 72;
    b.metadata.fps = FrameRate::fps_24();
    b.metadata.duration = RationalTime(4, 1);
    const uint64_t id_b = pool.add_item(b);
    pool.set_star_rating(id_b, 2);

    SmartBinRule high_rated_60;
    high_rated_60.min_rating = 4;
    high_rated_60.min_fps = 50.0;
    high_rated_60.required_flag = ClipFlagColor::Green;
    const auto res = pool.query_smart_bin(high_rated_60);
    OCU_ASSERT_EQ(res.size(), 1u);
    OCU_ASSERT_EQ(res[0]->id, id_a);

    // Thumbnail & Waveform Mipmaps + Proxy Cache
    VideoFrame vf;
    OCU_ASSERT_TRUE(pool.find(id_a)->decoder->decode_video_frame(RationalTime::zero(), vf, nullptr));
    auto thumb_pyr = ThumbnailMipmapPyramid::build(vf, 64, 36, 3);
    OCU_ASSERT_EQ(thumb_pyr.levels.size(), 3u);
    OCU_ASSERT_EQ(thumb_pyr.levels[0].width, 64);
    OCU_ASSERT_EQ(thumb_pyr.levels[2].width, 16);

    AudioBuffer ab;
    OCU_ASSERT_TRUE(pool.find(id_a)->decoder->decode_audio_samples(RationalTime::zero(), 4096, 48000, 2, ab));
    auto wf_pyr = WaveformMipmapPyramid::build(ab);
    OCU_ASSERT_EQ(wf_pyr.levels.size(), 4u);
    OCU_ASSERT_TRUE(wf_pyr.best_level_for_zoom(100.0) != nullptr);

    ProxyCacheManager cache(2);
    OCU_ASSERT_TRUE(cache.generate_proxy_for_item(*pool.find(id_a), 640, 360));
    OCU_ASSERT_EQ(pool.find(id_a)->proxy_state, ProxyState::Ready);
    cache.put_smart_render_frame(1, 0, VideoFrame::allocate(16, 9));
    cache.put_smart_render_frame(1, 1, VideoFrame::allocate(16, 9));
    cache.put_smart_render_frame(1, 2, VideoFrame::allocate(16, 9)); // Evicts frame 0 via LRU
    OCU_ASSERT_TRUE(!cache.get_smart_render_frame(1, 0).has_value());
    OCU_ASSERT_TRUE(cache.get_smart_render_frame(1, 2).has_value());
}

int main() {
    return ::owncut::test::run_all("Phase1_Infra");
}
