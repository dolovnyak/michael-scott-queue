#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <thread>
#include <unordered_set>

#include "DummyLog.h"

namespace hp {

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

            DataTLS(HazardPointerManager<PtrType, Max_Hazard_Pointers_Num, Max_Threads_Num>* manager_tls)
                    : _manager_tls(manager_tls) {
                for (auto& _inner_hazard: _inner_hazard_ptr_array) {
                    _inner_hazard.free.store(true, std::memory_order_relaxed);
                }
                debug("DataTLS created fot thread ", std::this_thread::get_id());
            }

            std::atomic<bool> free{false};
            std::atomic<DataTLS*> next{nullptr};


            /// Allocate and deallocate always happens from the same thread.
            InnerHazardPointer* TryAllocateHazardPtr() {
                if (_current_hazard_ptr_index >= Max_Hazard_Pointers_Num) {
                    return nullptr;
                }
                _inner_hazard_ptr_array[_current_hazard_ptr_index].free.store(false, std::memory_order_relaxed);
                return &_inner_hazard_ptr_array[_current_hazard_ptr_index++];
            }

            void DeallocateHazardPtr(InnerHazardPointer* ptr) {
                ptr->free.store(std::memory_order_relaxed);

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
            int _current_hazard_ptr_index = 0;


            std::array<ProtectedPtrType, _max_retired_ptrs_num()> _retired_ptr_array;
            int _current_retired_ptr_index = 0;
        };

    private:
        class ReleaserTLS {
        public:
            void SetTLS(DataTLS* tls) {
                _tls = tls;
            }

            ~ReleaserTLS() {
                _tls->free.store(true, std::memory_order_relaxed);
            }

        private:
            DataTLS* _tls;
        };


    public:
        HazardPointerManager(std::atomic<size_t>& clearing_call_number) : _clearing_call_number(clearing_call_number) {}

        ~HazardPointerManager() {
            DataTLS* head = _head_tls.load(std::memory_order_relaxed);

            while (head != nullptr) {
                head->ClearRetiredPointers();
                head = head->next.load(std::memory_order_relaxed);
            }
        }

        DataTLS* GetTLS() {
            static thread_local DataTLS* tls = nullptr;

            if (tls != nullptr) {
                return tls;
            }

            /// if thread finished ReleaserTLS will clear it TLS using destructor
            static thread_local ReleaserTLS releaser;

            /// if released tls exist - return it.
            for (DataTLS* current = _head_tls.load(std::memory_order_relaxed); current != nullptr; current = current->next.load()) {
                bool true_cas = true;
                if (current->free.compare_exchange_strong(true_cas, false, std::memory_order_relaxed)) {
                    releaser.SetTLS(current);
                    tls = current;
                    return tls;
                }
            }

            tls = new DataTLS(this);
            releaser.SetTLS(tls);
            while (true) {
                DataTLS* head = _head_tls.load(std::memory_order_relaxed);
                tls->next = head;
                if (_head_tls.compare_exchange_weak(head, tls, std::memory_order_relaxed)) {
                    return tls;
                }
            }
        }

        std::unordered_set<ProtectedPtrType> GetUsedHazardPointers() {
            DataTLS* head = _head_tls.load(std::memory_order_relaxed);

            std::unordered_set<ProtectedPtrType> res;
            while (head != nullptr && !head->free.load(std::memory_order_relaxed)) {
                for (int i = 0; i < head->_max_hazard_ptrs_num(); ++i) {
                    if (!head->_inner_hazard_ptr_array[i].free.load(std::memory_order_relaxed)) {
                        res.emplace(head->_inner_hazard_ptr_array[i].ptr.load(std::memory_order_relaxed));
                    }
                }
                head = head->next.load(std::memory_order_relaxed);
            }
            return res;
        }

    private:
        std::atomic<DataTLS*> _head_tls{nullptr};

        std::atomic<size_t>& _clearing_call_number;
    };
}