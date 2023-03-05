#include <vector>
#include <thread>
#include <future>
#include <boost/lockfree/queue.hpp>

#include "MichaelScottQueue.h"

static const int g_iterations_num = 99999;
static const int g_consumer_iterations_before_die = 500;
static const int g_producer_number = 20;
static const int g_consumer_number = 10;

using MSQueue = msq::Queue<size_t, g_producer_number + g_consumer_number>;
static std::atomic<size_t> g_final_sum{0};
static std::atomic<size_t> g_boost_final_sum{0};

class Sync {
public:
    Sync() : boost_queue(g_iterations_num) {

    }

    boost::lockfree::queue<size_t> boost_queue;
    MSQueue queue;
    std::atomic<bool> exit{false};
    std::atomic<int> counter{0};
    std::atomic<int> current_consumers_num{g_consumer_number};
};

void* producer_routine(void* arg) {
    msq::MSQ_LOG_DEBUG("Start producer_routine with thread id ", std::this_thread::get_id());

    auto* sync = reinterpret_cast<Sync*>(arg);

    for (int i = 0; i < g_iterations_num; ++i) {
        sync->queue.push(i + 1);
        sync->boost_queue.push(i + 1);
    }

    sync->counter.fetch_add(1);

    while (sync->counter.load(std::memory_order_relaxed) < g_producer_number) {}
    sync->exit.store(true, std::memory_order_relaxed);

    msq::MSQ_LOG_DEBUG("Finish producer_routine with thread id ", std::this_thread::get_id());

    return nullptr;
}

void consumer_routine(void* arg) {
    msq::MSQ_LOG_DEBUG("Start consumer_routine with thread id ", std::this_thread::get_id());
    size_t local_res = 0;
    size_t boost_local_res = 0;

    Sync* sync = reinterpret_cast<Sync*>(arg);

    int i = 0;
    while (!sync->exit.load(std::memory_order_relaxed) || !sync->queue.empty() || !sync->boost_queue.empty()) {
        size_t pop_res = 0;
        size_t boost_pop_res = 0;
        if (!sync->queue.pop(pop_res) && !sync->boost_queue.pop(boost_pop_res)) {
            std::this_thread::yield();
            continue;
        }
        local_res += pop_res;
        boost_local_res += boost_pop_res;

        if (i == g_consumer_iterations_before_die) {
            break;
        }
        ++i;
    }

    sync->current_consumers_num.fetch_add(-1, std::memory_order_release);
    g_final_sum.fetch_add(local_res);
    g_boost_final_sum.fetch_add(boost_local_res);
    msq::MSQ_LOG_DEBUG("Finish consumer_routine ", std::this_thread::get_id(), ", with result ", local_res);
}

int main() {
    Sync sync;

    std::vector<std::thread> producer_threads;
    std::vector<std::thread> consumer_threads;

    for (int i = 0; i < g_producer_number; ++i) {
        producer_threads.emplace_back(producer_routine, &sync);
    }

    for (int i = 0; i < g_consumer_number; ++i) {
        consumer_threads.emplace_back(consumer_routine, &sync);
    }

    while (!sync.exit.load(std::memory_order_relaxed) || !sync.queue.empty() || !sync.boost_queue.empty()) {
        if (sync.current_consumers_num.load(std::memory_order_acquire) < g_consumer_number) {
            consumer_threads.emplace_back(consumer_routine, &sync);
            sync.current_consumers_num.fetch_add(1, std::memory_order_release);
        }
    }

    for (auto& thread: producer_threads) {
        thread.join();
    }

    for (auto& thread: consumer_threads) {
        thread.join();
    }
    producer_threads.clear();
    consumer_threads.clear();

    size_t expected_res = 0;
    for (int i = 0; i < g_producer_number; ++i) {
        for (int j = 0; j < g_iterations_num; ++j) {
            expected_res += j + 1;
        }
    }
    msq::MSQ_LOG_DEBUG("expected res:       ", expected_res);
    msq::MSQ_LOG_DEBUG("boost final value:  ", g_boost_final_sum.load());
    msq::MSQ_LOG_DEBUG("final value:        ", g_final_sum.load());

    auto& statistic = sync.queue.GetStatistic();
    msq::MSQ_LOG_DEBUG("\nstatistic:",
          "\nsuccessful push number: ", statistic.successful_push_number.load(),
          "\nsuccessful pop number: ", statistic.successful_pop_number.load(),
          "\nempty pop number: ", statistic.empty_pop_number.load(),
          "\nclearing function call number: ", statistic.clearing_function_call_number.load(),
          "\nloop g_iterations_num in successful push: ", statistic.loop_iterations_number_in_push.load(),
          "\naverage loop g_iterations_num in successful push: ",
          (double) statistic.loop_iterations_number_in_push.load() / (double) statistic.successful_push_number.load(),
          "\nloop g_iterations_num in successful pop: ", statistic.loop_iterations_number_in_pop.load(),
          "\naverage loop g_iterations_num in successful pop: ",
          (double) statistic.loop_iterations_number_in_pop.load() / (double) statistic.successful_pop_number.load(),
          "\nconstructed nodes number: ", statistic.constructed_nodes_number.load(),
          "\ndestructed nodes number: ", statistic.destructed_nodes_number.load());
    return 0;
}
