#include <vector>
#include <thread>
#include <unistd.h>
#include <future>
#include <cassert>
#include <boost/lockfree/queue.hpp>

#include "MichaelScottQueue.h"

static const int iterations = 99999;
static const int producer_number = 50;
static const int consumer_number = 100;

using MSQueue = MichaelScottQueue<size_t, producer_number + consumer_number>;
static std::atomic<size_t> g_final_sum{0};
static std::atomic<size_t> g_boost_final_sum{0};

class Sync {
public:
    Sync() : boost_queue(iterations) {

    }

    MSQueue queue;
    boost::lockfree::queue<size_t> boost_queue;
    std::atomic<bool> exit{false};
    std::atomic<int> counter{0};
};

void* producer_routine(void* arg) {
    debug("Start producer_routine with thread id ", std::this_thread::get_id());

    auto* sync = reinterpret_cast<Sync*>(arg);

    for (int i = 0; i < iterations; ++i) {
        sync->queue.push(i + 1);
        sync->boost_queue.push(i + 1);
    }

    sync->counter.fetch_add(1);

    while (sync->counter.load(std::memory_order_relaxed) < producer_number) {}
    sync->exit.store(true, std::memory_order_relaxed);

    debug("Finish producer_routine with thread id ", std::this_thread::get_id());

    return nullptr;
}

void consumer_routine(void* arg) {
    debug("Start consumer_routine with thread id ", std::this_thread::get_id());
    size_t local_res = 0;
    size_t boost_local_res = 0;

    Sync* sync = reinterpret_cast<Sync*>(arg);

    while (!(sync->exit.load() && sync->queue.empty())) {
        size_t pop_res = 0;
        size_t boost_pop_res = 0;
        if (!sync->queue.pop(pop_res) && !sync->boost_queue.pop(boost_pop_res)) {
            std::this_thread::yield();
            continue;
        }
        local_res += pop_res;
        boost_local_res += boost_pop_res;
    }

    g_final_sum.fetch_add(local_res);
    g_boost_final_sum.fetch_add(boost_local_res);
    debug("Finish consumer_routine ", std::this_thread::get_id(), ", with result ", local_res);
}

int main() {
    Sync sync;

//    if (!sync.boost_queue.is_lock_free()) {
//        debug("boost::lockfree::queue is not lockfree");
//    }
//    else {
//        debug("boost::lockfree::queue is lockfree");
//    }

    std::vector<std::thread> producer_threads;
    std::vector<std::thread> consumer_threads;

    for (int i = 0; i < producer_number; ++i) {
        producer_threads.emplace_back(producer_routine, &sync);
    }

    for (int i = 0; i < consumer_number; ++i) {
        consumer_threads.emplace_back(consumer_routine, &sync);
    }

    for (auto& thread: producer_threads) {
        thread.join();
    }

    for (auto& thread: consumer_threads) {
        thread.join();
    }

    size_t expected_res = 0;
    for (int i = 0; i < producer_number; ++i) {
        for (int j = 0; j < iterations; ++j) {
            expected_res += j + 1;
        }
    }
    debug("expected res:       ", expected_res);
    debug("boost final value:  ", g_boost_final_sum.load());
    debug("final value:        ", g_final_sum.load());

    auto& statistic = sync.queue.GetStatistic();
    debug("\nstatistic:",
          "\nsuccessful push number: ", statistic.successful_push_number.load(),
          "\nsuccessful pop number: ", statistic.successful_pop_number.load(),
          "\nempty pop number: ", statistic.empty_pop_number.load(),
          "\nclearing function call number: ", statistic.clearing_function_call_number.load(),
          "\nloop iterations in successful push: ", statistic.loop_iterations_number_in_push.load(),
          "\naverage loop iterations in successful push: ",
          (double) statistic.loop_iterations_number_in_push.load() / (double) statistic.successful_push_number.load(),
          "\nloop iterations in successful pop: ", statistic.loop_iterations_number_in_pop.load(),
          "\naverage loop iterations in successful pop: ",
          (double) statistic.loop_iterations_number_in_pop.load() / (double) statistic.successful_pop_number.load(),
          "\nconstructed nodes number: ", statistic.constructed_nodes_number.load(),
          "\ndestructed nodes number: ", statistic.destructed_nodes_number.load());
    return 0;
}
