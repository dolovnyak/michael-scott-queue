#pragma once

#include "internal/HazardPointer.h"

template<class T, size_t Max_Threads_Num>
class MichaelScottQueue {
public:
    class Statistic {
    public:
        ~Statistic() {
            LOG_DEBUG("Statistic destructed in thread ", std::this_thread::get_id());
        }

        std::atomic<size_t> constructed_nodes_number{0};
        std::atomic<size_t> destructed_nodes_number{0};
        std::atomic<size_t> loop_iterations_number_in_push{0};
        std::atomic<size_t> successful_push_number{0};
        std::atomic<size_t> loop_iterations_number_in_pop{0};
        std::atomic<size_t> successful_pop_number{0};
        std::atomic<size_t> empty_pop_number{0};
        std::atomic<size_t> clearing_function_call_number{0};
    };

private:
    class Node {
    public:
        Node(Node* next, T value, Statistic& statistic) : next(next), value(value), _statistic(statistic) {
            _statistic.constructed_nodes_number.fetch_add(1, std::memory_order_relaxed);
        }

        Node(Node* next, Statistic& statistic) : next(next), _statistic(statistic) {
            _statistic.constructed_nodes_number.fetch_add(1, std::memory_order_relaxed);
        }

        ~Node() {
            _statistic.destructed_nodes_number.fetch_add(1, std::memory_order_relaxed);
        }

        std::atomic<Node*> next;

        /// hack for creation sentinel node if user type doesn't have default constructor
        union {
            T value;
        };

    private:
        Statistic& _statistic;
    };

    using ManagerHP = hp::HazardPointerManager<Node*, 3, Max_Threads_Num>;
    using HazardPtr = hp::HazardPointer<ManagerHP>;

    std::atomic<Node*> _head_ref{new Node(nullptr, _statistic)};
    std::atomic<Node*> _tail_ref{_head_ref.load(std::memory_order_relaxed)};

public:
    MichaelScottQueue() : _hazard_manager(_statistic.clearing_function_call_number) {}

    ~MichaelScottQueue() {
        LOG_DEBUG("MichaelScottQueue destructed in thread ", std::this_thread::get_id());

        /// queue must be destroyed in one thread when others have finished working with it.
        Node* current = _head_ref.load(std::memory_order_relaxed);
        while (current != nullptr) {
            Node* next = current->next.load(std::memory_order_relaxed);
            delete current;
            current = next;
        }
    }

    void push(T value) {
        int loop_times_before_success = 0;

        Node* new_node = new Node(nullptr, value, _statistic);
        HazardPtr hazard_pointer = HazardPtr(&_hazard_manager);

        while (true) {
            ++loop_times_before_success;

            Node* tail = hazard_pointer.Protect(_tail_ref);
            Node* tail_next = tail->next.load(std::memory_order_acquire); /// tail_next can't change if tail hasn't changed, so we shouldn't protect it

            Node* cas_nullptr = nullptr;
            if (tail_next != nullptr) {
                _tail_ref.compare_exchange_weak(tail, tail_next, std::memory_order_release, std::memory_order_relaxed);
            }
            else if (tail->next.compare_exchange_strong(cas_nullptr, new_node, std::memory_order_release,
                                                        std::memory_order_relaxed)) {
                _tail_ref.compare_exchange_weak(tail, new_node, std::memory_order_release, std::memory_order_relaxed);

                _statistic.loop_iterations_number_in_push.fetch_add(loop_times_before_success,
                                                                    std::memory_order_relaxed);
                _statistic.successful_push_number.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
    }

    bool pop(T& result) {
        int loop_times_before_success = 0;

        HazardPtr hp_head = HazardPtr(&_hazard_manager);      /// for safe "_head_ref.compare_exchange(head, head_next)"
        HazardPtr hp_head_next = HazardPtr(&_hazard_manager); /// for safe "result = head_next->value;"
        HazardPtr hp_tail = HazardPtr(&_hazard_manager);      /// for safe "_tail_ref.compare_exchange(tail, head_next)"

        while (true) {
            ++loop_times_before_success;

            Node* head = hp_head.Protect(_head_ref);
            Node* tail = hp_tail.Protect(_tail_ref);
            Node* head_next = hp_head_next.Protect(head->next);

            if (head == tail) {
                if (head_next == nullptr) {
                    _statistic.empty_pop_number.fetch_add(1, std::memory_order_relaxed);
                    return false;
                }
                _tail_ref.compare_exchange_weak(tail, head_next, std::memory_order_release, std::memory_order_relaxed);
            }
            else {
                if (_head_ref.compare_exchange_strong(head, head_next, std::memory_order_release, std::memory_order_relaxed)) {
                    result = head_next->value;

                    hp_head.Retire();

                    _statistic.loop_iterations_number_in_pop.fetch_add(loop_times_before_success,
                                                                       std::memory_order_relaxed);
                    _statistic.successful_pop_number.fetch_add(1, std::memory_order_relaxed);
                    return true;
                }
            }
        }
    }

    [[nodiscard]] bool empty() {
        HazardPtr hp_head(&_hazard_manager);
        Node* head = hp_head.Protect(_head_ref);

        return head->next.load(std::memory_order_acquire) == nullptr;
    }

    const Statistic& GetStatistic() {
        return _statistic;
    }

private:
    Statistic _statistic;
    ManagerHP _hazard_manager;
};
