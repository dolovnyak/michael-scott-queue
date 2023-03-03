#pragma once

#include <iostream>
#include <unordered_set>
#include <array>

namespace msq {

#ifdef __APPLE__
static pthread_mutex_t g_cout_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
#elif __linux__
static pthread_mutex_t g_cout_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
#endif

template<class T>
void dummy_debug(T t) {
    pthread_mutex_lock(&g_cout_mutex);
    std::cout << t << std::endl;
    pthread_mutex_unlock(&g_cout_mutex);
}

template<class T, class... Args>
void dummy_debug(T t, Args... args) {
    pthread_mutex_lock(&g_cout_mutex);
    std::cout << t;
    dummy_debug(args...);
    pthread_mutex_unlock(&g_cout_mutex);
}

#ifndef MSQ_DEBUG
#define LOG_DEBUG(...)
#else
#define LOG_DEBUG(...)          dummy_debug(__VA_ARGS__);
#endif

template<class PtrType, size_t Max_Hazard_Pointers_Num, size_t Max_Threads_Num>
class HazardPointerManager {

public:
    using ProtectedPtrType = PtrType;

    class InnerHazardPointer {
    public:
        std::atomic<bool> free{true};
        std::atomic<ProtectedPtrType> ptr;
    };

    class DataTLS {
    public:

        ~DataTLS() {
            LOG_DEBUG("TLS destructed in thread ", std::this_thread::get_id());
        }

        DataTLS(HazardPointerManager<PtrType, Max_Hazard_Pointers_Num, Max_Threads_Num>* manager_tls)
                : _manager_tls(manager_tls) {
            for (auto& _inner_hazard: _inner_hazard_ptr_array) {
                _inner_hazard.free.store(true);
            }
            LOG_DEBUG("DataTLS constructed in thread ", std::this_thread::get_id());
        }

        std::atomic<bool> free{false};
        std::atomic<DataTLS*> next{nullptr};


        /// Allocate always happens from the same thread.
        InnerHazardPointer* TryAllocateHazardPtr() {
            if (_current_hazard_ptr_index >= Max_Hazard_Pointers_Num) {
                return nullptr;
            }
            _inner_hazard_ptr_array[_current_hazard_ptr_index].free.store(false);
            return &_inner_hazard_ptr_array[_current_hazard_ptr_index++];
        }

        /// Deallocate always happens from the same thread.
        void DeallocateHazardPtr(InnerHazardPointer* ptr) {
            ptr->free.store(true);

            /// it's work because hazard pointers are creating and deleting in the same order.
            --_current_hazard_ptr_index;
        }

        bool TryAddRetiredPtr(ProtectedPtrType ptr) {
            if (_current_retired_ptr_index == _max_retired_ptrs_num()) {
                return false;
            }
            _retired_ptr_array[_current_retired_ptr_index++] = ptr;
            return true;
        }

        void ClearRetiredPointers() {
            _manager_tls->_clearing_call_number.fetch_add(1, std::memory_order_relaxed);

            std::unordered_set<ProtectedPtrType> used_hazard_pointers = _manager_tls->GetUsedHazardPointers();
            std::array<ProtectedPtrType, _max_retired_ptrs_num()> new_array;
            int new_index = 0;
            for (int i = 0; i < _current_retired_ptr_index; ++i) {
                if (used_hazard_pointers.find(_retired_ptr_array[i]) != used_hazard_pointers.end()) {
                    new_array[new_index++] = _retired_ptr_array[i];
                }
                else {
                    delete _retired_ptr_array[i];
                }
            }
            _retired_ptr_array = new_array;
            _current_retired_ptr_index = new_index;
        }

        void ForceClearRetiredPointers() {
            for (int i = 0; i < _current_retired_ptr_index; ++i) {
                delete _retired_ptr_array[i];
            }
        }


    private:
        friend class HazardPointerManager<PtrType, Max_Hazard_Pointers_Num, Max_Threads_Num>;

        HazardPointerManager<PtrType, Max_Hazard_Pointers_Num, Max_Threads_Num>* _manager_tls;

        static constexpr int _max_hazard_ptrs_num() {
            return Max_Hazard_Pointers_Num;
        }

        static constexpr int _max_retired_ptrs_num() {
            return Max_Hazard_Pointers_Num * Max_Threads_Num;
        }

        std::array<InnerHazardPointer, Max_Hazard_Pointers_Num> _inner_hazard_ptr_array;
        std::atomic<int> _current_hazard_ptr_index{0};


        std::array<ProtectedPtrType, _max_retired_ptrs_num()> _retired_ptr_array;
        int _current_retired_ptr_index = 0;
    };

private:
    class ReleaserTLS {
    public:
        ReleaserTLS(const std::shared_ptr<bool>& is_manager_destructed)
                : _is_manager_destructed(is_manager_destructed) {}

        void SetTLS(DataTLS* tls) {
            _tls = tls;
        }

        ~ReleaserTLS() {
            if (!*_is_manager_destructed) {
                _tls->free.store(true, std::memory_order_relaxed);
            }
        }

    private:
        DataTLS* _tls;

        /// Note:
        /// according https://en.cppreference.com/w/cpp/language/storage_duration thread storage duration:
        /// "The storage for the object is allocated when the thread begins and deallocated when the thread ends.
        /// Each thread has its own instance of the object."
        /// But on MacOS I've got address sanitizer error that ReleaserTLS which was constructed not in main thread
        /// was destructed in main thread after all other threads and HazardPointerManager destructions,
        /// so this flag fixed the compiler non-compliance with the C++ standard on MacOS.
        std::shared_ptr<bool> _is_manager_destructed;
    };

public:
    HazardPointerManager(std::atomic<size_t>& clearing_call_number)
            : _clearing_call_number(clearing_call_number),
              _is_destructed(std::make_shared<bool>(false)) {}

    ~HazardPointerManager() {
        *_is_destructed = true;
        DataTLS* current = _head_tls.load();

        while (current != nullptr) {
            DataTLS* next = current->next.load();
            current->ForceClearRetiredPointers();
            delete current;
            current = next;
        }
        LOG_DEBUG("HazardPointerManager destructed in thread ", std::this_thread::get_id());
    }

    DataTLS* GetTLS() {
        static thread_local DataTLS* tls = nullptr;

        if (tls != nullptr) {
            return tls;
        }

        /// if thread finished ReleaserTLS will clear it TLS using destructor
        static thread_local ReleaserTLS releaser(_is_destructed);

        /// if released tls exist - return it.
        for (DataTLS* current = _head_tls.load(); current != nullptr; current = current->next.load()) {
            bool true_cas = true;
            if (current->free.compare_exchange_strong(true_cas, false)) {
                releaser.SetTLS(current);
                tls = current;
                return tls;
            }
        }

        tls = new DataTLS(this);
        releaser.SetTLS(tls);
        while (true) {
            DataTLS* head = _head_tls.load();
            tls->next = head;
            if (_head_tls.compare_exchange_strong(head, tls)) {
                return tls;
            }
        }
    }

    std::unordered_set<ProtectedPtrType> GetUsedHazardPointers() {
        DataTLS* head = _head_tls.load(std::memory_order_acquire);

        std::unordered_set<ProtectedPtrType> res;
        while (head != nullptr) {
            if (head->free.load(std::memory_order_relaxed)) {
                head = head->next.load(std::memory_order_acquire);
                continue;
            }

            for (int i = 0; i < head->_max_hazard_ptrs_num(); ++i) {
                if (!head->_inner_hazard_ptr_array[i].free.load()) {
                    res.emplace(head->_inner_hazard_ptr_array[i].ptr.load());
                }
            }
            head = head->next.load(std::memory_order_acquire);
        }
        return res;
    }

private:
    std::atomic<DataTLS*> _head_tls{nullptr};

    std::atomic<size_t>& _clearing_call_number;

    std::shared_ptr<bool> _is_destructed;
};

template<class Manager>
class HazardPointer {
    using TLS = typename Manager::DataTLS;
    using ProtectedPtrType = typename Manager::ProtectedPtrType;
    using InnerHazardPtr = typename Manager::InnerHazardPointer;

public:
    HazardPointer(Manager* manager_tls)
            : _tls(manager_tls->GetTLS()) {
        _inner_hazard_pointer = _tls->TryAllocateHazardPtr();
        if (_inner_hazard_pointer == nullptr) {
            throw std::logic_error(
                    "Can't allocate new hazard pointer, probably the limit of hazard pointer number has been exceeded");
        }
    }

    ~HazardPointer() {
        _tls->DeallocateHazardPtr(_inner_hazard_pointer);
    }

    void Retire() {
        if (!_tls->TryAddRetiredPtr(_inner_hazard_pointer->ptr)) {
            _tls->ClearRetiredPointers();
            if (!_tls->TryAddRetiredPtr(_inner_hazard_pointer->ptr)) {
                throw std::logic_error("Still there is no space for retired_ptr, after clearing");
            }
        }
    }

    ProtectedPtrType Protect(const std::atomic<ProtectedPtrType>& ptr) {
        /// This construction is important because we can sleep on first ptr.load() and during this sleep
        /// clearing function can start and delete ptr that we loaded.
        while (true) {
            _inner_hazard_pointer->ptr = ptr.load(std::memory_order_acquire);
            if (_inner_hazard_pointer->ptr == ptr.load(std::memory_order_acquire)) {
                return _inner_hazard_pointer->ptr;
            }
        }
    }

private:
    TLS* _tls;
    InnerHazardPtr* _inner_hazard_pointer;
};

template<class T, size_t Max_Threads_Num>
class Queue {
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

    using ManagerHP = HazardPointerManager<Node*, 3, Max_Threads_Num>;
    using HazardPtr = HazardPointer<ManagerHP>;

    std::atomic<Node*> _head_ref{new Node(nullptr, _statistic)};
    std::atomic<Node*> _tail_ref{_head_ref.load(std::memory_order_relaxed)};

public:
    Queue() : _hazard_manager(_statistic.clearing_function_call_number) {}

    ~Queue() {
        LOG_DEBUG("Queue destructed in thread ", std::this_thread::get_id());

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
            Node* tail_next = tail->next.load(
                    std::memory_order_acquire); /// tail_next can't change if tail hasn't changed, so we shouldn't protect it

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
                if (_head_ref.compare_exchange_strong(head, head_next, std::memory_order_release,
                                                      std::memory_order_relaxed)) {
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

}