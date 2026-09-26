/*
    Copyright (C) 2017 by Sergey A Kryukov: derived work
    http://www.SAKryukov.org
    http://www.codeproject.com/Members/SAKryukov

    Based on original work by Sergey Ryazanov:
    "The Impossibly Fast C++ Delegates", 18 Jul 2005
    https://www.codeproject.com/articles/11015/the-impossibly-fast-c-delegates

    MIT license:
    http://en.wikipedia.org/wiki/MIT_License

    Original publication: https://www.codeproject.com/Articles/1170503/The-Impossibly-Fast-Cplusplus-Delegates-Fixed
*/

#pragma once
#include "DelegateBase.h"
#include <type_traits>
#include <utility>

namespace SA {

template <typename T>
class delegate;
template <typename T>
class multicast_delegate;

template <typename RET, typename... PARAMS>
class delegate<RET(PARAMS...)> final : private delegate_base<RET(PARAMS...)> {
   public:
    delegate() = default;
    ~delegate() = default;
    delegate(delegate&&) = default;
    delegate& operator=(delegate&&) = default;

    [[nodiscard]] bool isNull() const { return invocation.stub == nullptr; }
    explicit operator bool() const { return invocation.stub != nullptr; }

    bool operator==(void* ptr) const { return (ptr == nullptr) && this->isNull(); }     //operator ==
    bool operator!=(void* ptr) const { return (ptr != nullptr) || (!this->isNull()); }  //operator !=

    delegate(const delegate& another) = default;

    template <typename LAMBDA>
    delegate(const LAMBDA& lambda) {
        assign((void*)(&lambda), lambda_stub<LAMBDA>);
    }  //delegate

    // stateless lambda temporaries: convert to function pointer (safe)
    template <typename LAMBDA>
        requires std::is_convertible_v<LAMBDA, RET (*)(PARAMS...)> && (!std::is_same_v<std::decay_t<LAMBDA>, delegate>)
    delegate(LAMBDA&& lambda) {
        using FuncPtr = RET (*)(PARAMS...);
        FuncPtr funcPtr = std::forward<LAMBDA>(lambda);
        assign(reinterpret_cast<void*>(funcPtr), function_pointer_stub);
    }  //delegate

    // prevent binding to stateful lambda temporaries (dangling pointer)
    template <typename LAMBDA>
        requires(!std::is_convertible_v<LAMBDA, RET (*)(PARAMS...)>) &&
                    (!std::is_same_v<std::decay_t<LAMBDA>, delegate>)
    delegate(LAMBDA&& lambda) = delete;

    delegate& operator=(const delegate& another) = default;

    template <typename LAMBDA>  // template instantiation is not needed, will be deduced (inferred):
    delegate& operator=(const LAMBDA& instance) {
        assign((void*)(&instance), lambda_stub<LAMBDA>);
        return *this;
    }  //operator =

    // stateless lambda temporaries: convert to function pointer (safe)
    template <typename LAMBDA>
        requires std::is_convertible_v<LAMBDA, RET (*)(PARAMS...)> && (!std::is_same_v<std::decay_t<LAMBDA>, delegate>)
    delegate& operator=(LAMBDA&& lambda) {
        using FuncPtr = RET (*)(PARAMS...);
        FuncPtr funcPtr = std::forward<LAMBDA>(lambda);
        assign(reinterpret_cast<void*>(funcPtr), function_pointer_stub);
        return *this;
    }  //operator =

    // prevent binding to stateful lambda temporaries (dangling pointer)
    template <typename LAMBDA>
        requires(!std::is_convertible_v<LAMBDA, RET (*)(PARAMS...)>) &&
                    (!std::is_same_v<std::decay_t<LAMBDA>, delegate>)
    delegate& operator=(LAMBDA&& lambda) = delete;

    bool operator==(const delegate& another) const { return invocation == another.invocation; }
    bool operator!=(const delegate& another) const { return invocation != another.invocation; }

    bool operator==(const multicast_delegate<RET(PARAMS...)>& another) const { return another == (*this); }
    bool operator!=(const multicast_delegate<RET(PARAMS...)>& another) const { return another != (*this); }

    template <class T, RET (T::*TMethod)(PARAMS...)>
    static delegate create(T* instance) {
        return delegate(instance, method_stub<T, TMethod>);
    }  //create

    template <class T, RET (T::*TMethod)(PARAMS...) const>
    static delegate create(T const* instance) {
        return delegate(const_cast<T*>(instance), const_method_stub<T, TMethod>);
    }  //create

    template <RET (*TMethod)(PARAMS...)>
    static delegate create() {
        return delegate(nullptr, function_stub<TMethod>);
    }  //create

    template <typename LAMBDA>
    static delegate create(const LAMBDA& instance) {
        return delegate((void*)(&instance), lambda_stub<LAMBDA>);
    }  //create

    // stateless lambda temporaries: convert to function pointer (safe)
    template <typename LAMBDA>
        requires std::is_convertible_v<LAMBDA, RET (*)(PARAMS...)> && (!std::is_same_v<std::decay_t<LAMBDA>, delegate>)
    static delegate create(LAMBDA&& lambda) {
        using FuncPtr = RET (*)(PARAMS...);
        FuncPtr funcPtr = std::forward<LAMBDA>(lambda);
        return delegate(reinterpret_cast<void*>(funcPtr), function_pointer_stub);
    }  //create

    // prevent binding to stateful lambda temporaries (dangling pointer)
    template <typename LAMBDA>
        requires(!std::is_convertible_v<LAMBDA, RET (*)(PARAMS...)>) &&
                    (!std::is_same_v<std::decay_t<LAMBDA>, delegate>)
    static delegate create(LAMBDA&& instance) = delete;

    RET operator()(PARAMS... arg) const { return (*invocation.stub)(invocation.object, arg...); }  //operator()

   private:
    delegate(void* anObject, typename delegate_base<RET(PARAMS...)>::stub_type aStub) {
        invocation.object = anObject;
        invocation.stub = aStub;
    }  //delegate

    void assign(void* anObject, typename delegate_base<RET(PARAMS...)>::stub_type aStub) {
        this->invocation.object = anObject;
        this->invocation.stub = aStub;
    }  //assign

    template <class T, RET (T::*TMethod)(PARAMS...)>
    static RET method_stub(void* this_ptr, PARAMS... params) {
        T* p = static_cast<T*>(this_ptr);
        return (p->*TMethod)(params...);
    }  //method_stub

    template <class T, RET (T::*TMethod)(PARAMS...) const>
    static RET const_method_stub(void* this_ptr, PARAMS... params) {
        T* const p = static_cast<T*>(this_ptr);
        return (p->*TMethod)(params...);
    }  //const_method_stub

    template <RET (*TMethod)(PARAMS...)>
    static RET function_stub(void* /*this_ptr*/, PARAMS... params) {
        return (TMethod)(params...);
    }  //function_stub

    // stub for runtime function pointers (used by stateless lambda temporaries)
    static RET function_pointer_stub(void* func_ptr, PARAMS... params) {
        using FuncPtr = RET (*)(PARAMS...);
        return reinterpret_cast<FuncPtr>(func_ptr)(params...);
    }  //function_pointer_stub

    template <typename LAMBDA>
    static RET lambda_stub(void* this_ptr, PARAMS... arg) {
        LAMBDA* p = static_cast<LAMBDA*>(this_ptr);
        return (p->operator())(arg...);
    }  //lambda_stub

    friend class multicast_delegate<RET(PARAMS...)>;
    typename delegate_base<RET(PARAMS...)>::InvocationElement invocation;

};  //class delegate

} /* namespace SA */
