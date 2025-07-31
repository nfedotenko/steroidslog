/*
 * Copyright(c) 2025 Nikolai Fedotenko.
 * Distributed under the MIT License (http://opensource.org/licenses/MIT)
 */

#pragma once

template <size_t BufSize> class SmallFunction {
    using Storage = std::aligned_storage_t<BufSize, alignof(void*)>;
    using InvokeFn = void (*)(const Storage&);
    using DestroyFn = void (*)(Storage&);
    using CloneFn = void (*)(const Storage&, Storage&);

    Storage buf_;
    InvokeFn invoke_{nullptr};
    DestroyFn destroy_{nullptr};
    CloneFn clone_{nullptr};

public:
    SmallFunction() = default;

    template <typename F> SmallFunction(F f) {
        static_assert(sizeof(F) <= BufSize,
                      "Capture too large for SmallFunction buffer");
        new (&buf_) F(std::move(f));
        invoke_ = +[](const Storage& s) {
            const F& fn = *reinterpret_cast<const F*>(&s);
            fn();
        };
        destroy_ = +[](Storage& s) {
            F& fn = *reinterpret_cast<F*>(&s);
            fn.~F();
        };
        clone_ = +[](const Storage& src, Storage& dst) {
            const F& fn = *reinterpret_cast<const F*>(&src);
            new (&dst) F(fn);
        };
    }

    SmallFunction(const SmallFunction& o) {
        if (o.clone_) {
            o.clone_(o.buf_, buf_);
            invoke_ = o.invoke_;
            destroy_ = o.destroy_;
            clone_ = o.clone_;
        }
    }
    SmallFunction(SmallFunction&& o) noexcept {
        if (o.clone_) {
            o.clone_(o.buf_, buf_);
            invoke_ = o.invoke_;
            destroy_ = o.destroy_;
            clone_ = o.clone_;
            o.destroy_(o.buf_);
            o.invoke_ = o.destroy_ = o.clone_ = nullptr;
        }
    }

    SmallFunction& operator=(const SmallFunction& o) {
        if (this != &o) {
            if (destroy_)
                destroy_(buf_);
            if (o.clone_) {
                o.clone_(o.buf_, buf_);
                invoke_ = o.invoke_;
                destroy_ = o.destroy_;
                clone_ = o.clone_;
            } else {
                invoke_ = destroy_ = clone_ = nullptr;
            }
        }
        return *this;
    }
    SmallFunction& operator=(SmallFunction&& o) noexcept {
        if (this != &o) {
            if (destroy_)
                destroy_(buf_);
            if (o.clone_) {
                o.clone_(o.buf_, buf_);
                invoke_ = o.invoke_;
                destroy_ = o.destroy_;
                clone_ = o.clone_;
                o.destroy_(o.buf_);
                o.invoke_ = o.destroy_ = o.clone_ = nullptr;
            } else {
                invoke_ = destroy_ = clone_ = nullptr;
            }
        }
        return *this;
    }

    ~SmallFunction() {
        if (destroy_)
            destroy_(buf_);
    }

    void operator()() const {
        if (invoke_)
            invoke_(buf_);
    }

    explicit operator bool() const { return invoke_ != nullptr; }
};
