#include <unordered_set>
#include <vector>
#include <thread>
#include <array>
#include <mutex>
#include <unistd.h>
#include <future>

#include "MichaelScottQueue.h"

using MSQueue = MichaelScottQueue<size_t, 25>;
static std::atomic<size_t> g_final_sum = 0;
static const int iterations = 1000000;

class Sync {
public:
    Sync() {

    }

    MSQueue queue;
    std::atomic<bool> exit = false;
};


void* producer_routine(void* arg) {
    int thread_id = GenerateId();

    debug("Start producer_routine with thread id ", thread_id);

    auto* sync = reinterpret_cast<Sync*>(arg);

    for (int i = 0; i < iterations; ++i) {
        sync->queue.push(i + 1);
    }

    sleep(1);
    sync->exit.store(true);

    return nullptr;
}

void consumer_routine(void* arg) {
    debug("Start consumer_routine with thread id ", std::this_thread::get_id());
    size_t local_res = 0;

    Sync* sync = reinterpret_cast<Sync*>(arg);

    while (!(sync->exit && sync->queue.empty())) {
        size_t pop_res = 0;
        if (!sync->queue.pop(pop_res)) {
            std::this_thread::yield();
            continue;
        }
        local_res += pop_res;
    }

        g_final_sum.fetch_add(local_res);
    debug("Finish consumer_routine ", std::this_thread::get_id(), ", with result ", local_res);
}

int main() {
    Sync sync;

    std::thread consumer_thread(producer_routine, &sync);
    std::thread consumer_thread2(producer_routine, &sync);

    std::vector<std::thread> producer_threads;

    for (int i = 0; i < 20; ++i) {
        producer_threads.emplace_back(consumer_routine, &sync);
    }

    consumer_thread.join();
    consumer_thread2.join();

    for (auto& thread: producer_threads) {
        thread.join();
    }

    size_t expected_res = 0;
    for (int i = 0; i < iterations; ++i) {
        expected_res += i + 1;
    }
    for (int i = 0; i < iterations; ++i) {
        expected_res += i + 1;
    }
    debug("expected res: ", expected_res);

    debug("FINAL VALUE: ", g_final_sum.load());

    auto& statistic = sync.queue.GetStatistic();
    debug("\nstatistic:",
          "\nsuccessful push number: ", statistic.successful_push_number.load(),
          "\nsuccessful pop number: ", statistic.successful_pop_number.load(),
          "\nempty pop number: ", statistic.empty_pop_number.load(),
          "\nclearing function call number: ", statistic.clearing_function_call_number.load(),
          "\nloop iterations in successful push: ", statistic.loop_iterations_number_in_push.load(),
          "\naverage loop iterations in successful push: ",
          (double)statistic.loop_iterations_number_in_push.load() / (double)statistic.successful_push_number.load(),
          "\nloop iterations in successful pop: ", statistic.loop_iterations_number_in_pop.load(),
          "\naverage loop iterations in successful pop: ",
          (double)statistic.loop_iterations_number_in_pop.load() / (double)statistic.successful_pop_number.load(),
          "\nconstructed nodes number: ", statistic.constructed_nodes_number.load(),
          "\ndestructed nodes number: ", statistic.destructed_nodes_number.load());
    return 0;
}
