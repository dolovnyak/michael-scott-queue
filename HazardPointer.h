#pragma once

#include "HazardPointerManager.h"

namespace hp {

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
            _inner_hazard_pointer->ptr = ptr.load(std::memory_order_relaxed);
            return _inner_hazard_pointer->ptr;
        }

    private:
        TLS* _tls;
        InnerHazardPtr* _inner_hazard_pointer;
    };

}