#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#define TOTK_STATIC_ASSERT_ABI(Type, ExpectedSize) \
    static_assert(sizeof(Type) == (ExpectedSize), "ABI size mismatch for " #Type)

#define TOTK_STATIC_ASSERT_OFFSETOF(Type, Member, ExpectedOffset) \
    static_assert(offsetof(Type, Member) == (ExpectedOffset), "ABI offset mismatch for " #Type "::" #Member)

namespace totk::abi {

using GuestAddr = uint64_t;

// Guest pointer template resolving directly through identity mapping
template <typename T>
class GuestPtr {
public:
    GuestPtr() : addr_(0) {}
    explicit GuestPtr(GuestAddr addr) : addr_(addr) {}

    T *get() const {
        return addr_ ? reinterpret_cast<T *>(static_cast<uintptr_t>(addr_)) : nullptr;
    }

    T &operator*() const { return *get(); }
    T *operator->() const { return get(); }

    GuestAddr raw() const { return addr_; }
    explicit operator bool() const { return addr_ != 0; }

    bool operator==(const GuestPtr<T> &other) const { return addr_ == other.addr_; }
    bool operator!=(const GuestPtr<T> &other) const { return addr_ != other.addr_; }

private:
    GuestAddr addr_;
};

} // namespace totk::abi
