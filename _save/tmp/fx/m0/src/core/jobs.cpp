#include "jobs.h"
#include <thread>
#include <condition_variable>
#include <deque>

namespace Jobs {
namespace {
struct Job {
    std::function<void()> fn;
    JobCounter* counter;
};
std::mutex g_mutex;
std::condition_variable g_cv;
std::deque<Job> g_queues[kJobPriorityCount];
std::vector<std::thread> g_threads;
bool g_quit = false;
std::atomic<int> g_lowCount{0};

bool popJob(Job& out, int maxPrio) {
    for (int p = 0; p <= maxPrio; p++) {
        if (!g_queues[p].empty()) {
            out = std::move(g_queues[p].front());
            g_queues[p].pop_front();
            if (p == kJobLow) g_lowCount--;
            return true;
        }
    }
    return false;
}

void runJob(Job& j) {
    j.fn();
    if (j.counter) j.counter->pending.fetch_sub(1, std::memory_order_acq_rel);
}

void workerMain() {
    for (;;) {
        Job j;
        {
            std::unique_lock<std::mutex> lk(g_mutex);
            g_cv.wait(lk, [] {
                return g_quit || !g_queues[0].empty() || !g_queues[1].empty() || !g_queues[2].empty();
            });
            if (g_quit) return;
            if (!popJob(j, kJobLow)) continue;
        }
        runJob(j);
    }
}
}  // namespace

void init(int n) {
    g_quit = false;
    for (int i = 0; i < n; i++) g_threads.emplace_back(workerMain);
}
void shutdown() {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_quit = true;
    }
    g_cv.notify_all();
    for (auto& t : g_threads) t.join();
    g_threads.clear();
    for (auto& q : g_queues) q.clear();
}
int workerCount() { return (int)g_threads.size(); }

void submit(std::function<void()> fn, JobPriority prio, JobCounter* counter) {
    if (counter) counter->pending.fetch_add(1, std::memory_order_acq_rel);
    if (g_threads.empty()) {  // no workers: run inline
        fn();
        if (counter) counter->pending.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_queues[prio].push_back(Job{std::move(fn), counter});
        if (prio == kJobLow) g_lowCount++;
    }
    g_cv.notify_one();
}

void wait(JobCounter& counter) {
    while (!counter.done()) {
        Job j;
        bool got;
        {
            std::lock_guard<std::mutex> lk(g_mutex);
            got = popJob(j, kJobNormal);
        }
        if (got) runJob(j);
        else std::this_thread::yield();
    }
}

void parallelFor(int count, const std::function<void(int)>& fn, int granularity) {
    if (count <= 0) return;
    if (g_threads.empty() || count <= granularity) {
        for (int i = 0; i < count; i++) fn(i);
        return;
    }
    JobCounter c;
    for (int start = 0; start < count; start += granularity) {
        int end = Min(count, start + granularity);
        submit([&fn, start, end] { for (int i = start; i < end; i++) fn(i); }, kJobHigh, &c);
    }
    wait(c);
}

int pendingLow() { return g_lowCount.load(); }
}  // namespace Jobs
