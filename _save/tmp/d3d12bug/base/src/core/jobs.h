// Minimal work-stealing-free job system: a shared priority queue served by a
// fixed pool of worker threads. Background (streaming) jobs use low priority.
#pragma once
#include "base.h"

enum JobPriority { kJobHigh = 0, kJobNormal = 1, kJobLow = 2, kJobPriorityCount = 3 };

struct JobCounter {
    std::atomic<int> pending{0};
    bool done() const { return pending.load(std::memory_order_acquire) == 0; }
};

namespace Jobs {
void init(int numWorkers);
void shutdown();
int workerCount();
// Queue a job. If counter is non-null it's incremented now and decremented on completion.
void submit(std::function<void()> fn, JobPriority prio = kJobNormal, JobCounter* counter = nullptr);
// Wait for counter to reach zero; the calling thread helps execute high/normal jobs meanwhile.
void wait(JobCounter& counter);
// Run fn(i) for i in [0,count) across workers, blocking until complete.
void parallelFor(int count, const std::function<void(int)>& fn, int granularity = 1);
// Number of queued low-priority jobs (used to throttle streaming).
int pendingLow();
}  // namespace Jobs
