/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

#include <cstddef>
#include <new>
#include <utility>

template <size_t BufSize>
class SmallFunction {
    using InvokeFn = void(*)(SmallFunction&);
    using DestroyFn = void(*)(SmallFunction&) noexcept;
    using CloneFn = void(*)(const SmallFunction&, SmallFunction&);

public:
    SmallFunction() = default;

    template <typename F> 
    SmallFunction(F f) {
        static_assert(sizeof(F) <= BufSize,
                      "Callable too large for SmallFunction buffer");
        new (buf_) F(std::move(f));
        invoke_ = +[](SmallFunction& self) {
            auto& fn = *reinterpret_cast<F*>(self.buf_);
            fn();
        };
        destroy_ = +[](SmallFunction& self) noexcept {
            auto& fn = *reinterpret_cast<F*>(self.buf_);
            fn.~F();
        };
        clone_ = +[](const SmallFunction& src, SmallFunction& dst) {
            auto& fn = *reinterpret_cast<const F*>(src.buf_);
            new (dst.buf_) F(fn);
        };
    }

    SmallFunction(const SmallFunction& o) {
        if (o.clone_) {
            o.clone_(o, *this);
            invoke_ = o.invoke_;
            destroy_ = o.destroy_;
            clone_ = o.clone_;
        }
    }
    SmallFunction(SmallFunction&& o) noexcept {
        if (o.clone_) {
            o.clone_(o, *this);
            invoke_ = o.invoke_;
            destroy_ = o.destroy_;
            clone_ = o.clone_;
            o.destroy_(o);
            o.invoke_ = nullptr;
            o.destroy_ = nullptr;
            o.clone_ = nullptr;
        }
    }

    SmallFunction& operator=(const SmallFunction& o) {
        if (this != &o) {
            if (destroy_) {
                destroy_(*this);
            }
            if (o.clone_) {
                o.clone_(o, *this);
                invoke_ = o.invoke_;
                destroy_ = o.destroy_;
                clone_ = o.clone_;
            } else {
                invoke_ = nullptr;
                destroy_ = nullptr;
                clone_ = nullptr;
            }
        }
        return *this;
    }
    SmallFunction& operator=(SmallFunction&& o) noexcept {
        if (this != &o) {
            if (destroy_) {
                destroy_(*this);
            }
            if (o.clone_) {
                o.clone_(o, *this);
                invoke_ = o.invoke_;
                destroy_ = o.destroy_;
                clone_ = o.clone_;
                o.destroy_(o);
                o.invoke_ = nullptr;
                o.destroy_ = nullptr;
                o.clone_ = nullptr;
            } else {
                invoke_ = nullptr;
                destroy_ = nullptr;
                clone_ = nullptr;
            }
        }
        return *this;
    }

    ~SmallFunction() {
        if (destroy_) {
            destroy_(*this);
        }
    }

    void operator()() {
        if (invoke_) {
            invoke_(*this);
        }
    }

    explicit operator bool() const { return invoke_ != nullptr; }

private:
    alignas(std::max_align_t) std::byte buf_[BufSize];

    InvokeFn invoke_{nullptr};
    DestroyFn destroy_{nullptr};
    CloneFn clone_{nullptr};
};
