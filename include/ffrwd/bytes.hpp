#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace ffrwd {

/// A frame's or a packet's bytes: one allocation, moved and never copied
/// unless `clone` says so. What `fetch` hands over and what `Out::frame`
/// takes leave the module as they are.
class Bytes {
public:
    Bytes() = default;

    /// `size` bytes, zeroed.
    explicit Bytes(std::size_t size) : size_(size) {
        if (size > 0) data_ = static_cast<std::uint8_t*>(std::calloc(size, 1));
    }

    /// A copy of `size` bytes at `data`.
    Bytes(const void* data, std::size_t size) : Bytes(size) {
        if (size > 0) std::memcpy(data_, data, size);
    }

    Bytes(const std::vector<std::uint8_t>& bytes) : Bytes(bytes.data(), bytes.size()) {}

    explicit Bytes(std::string_view text) : Bytes(text.data(), text.size()) {}

    /// `size` bytes of `value`.
    static Bytes filled(std::size_t size, std::uint8_t value) {
        Bytes bytes(size);
        if (size > 0) std::memset(bytes.data_, value, size);
        return bytes;
    }

    /// Takes `data`, which `malloc` allocated, as its own.
    static Bytes adopt(std::uint8_t* data, std::size_t size) {
        Bytes bytes;
        bytes.data_ = data;
        bytes.size_ = size;
        return bytes;
    }

    Bytes(Bytes&& other) noexcept : data_(other.data_), size_(other.size_) {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    Bytes& operator=(Bytes&& other) noexcept {
        if (this != &other) {
            std::free(data_);
            data_ = other.data_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    Bytes(const Bytes&) = delete;
    Bytes& operator=(const Bytes&) = delete;

    ~Bytes() { std::free(data_); }

    Bytes clone() const { return Bytes(data_, size_); }

    /// Gives up the allocation, for `free` to release.
    std::uint8_t* release() {
        std::uint8_t* data = data_;
        data_ = nullptr;
        size_ = 0;
        return data;
    }

    std::uint8_t* data() { return data_; }
    const std::uint8_t* data() const { return data_; }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    std::uint8_t& operator[](std::size_t at) { return data_[at]; }
    std::uint8_t operator[](std::size_t at) const { return data_[at]; }

    std::uint8_t* begin() { return data_; }
    std::uint8_t* end() { return data_ + size_; }
    const std::uint8_t* begin() const { return data_; }
    const std::uint8_t* end() const { return data_ + size_; }

    std::span<std::uint8_t> span() { return {data_, size_}; }
    std::span<const std::uint8_t> span() const { return {data_, size_}; }

    std::vector<std::uint8_t> vector() const { return {begin(), end()}; }

    friend bool operator==(const Bytes& a, const Bytes& b) {
        return a.size_ == b.size_ && (a.size_ == 0 || std::memcmp(a.data_, b.data_, a.size_) == 0);
    }

private:
    std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace ffrwd
