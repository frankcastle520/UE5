// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <cassert>
#include <cstdint>
#include <utility>
#include <type_traits>

namespace EpicRtc
{
    /**
     * Optional wrapper to automatically handle reference counting.
     * Used in Tests.
     */
    template <typename ElementType>
    class RefCountPtr
    {
        template <class T>
        friend class RefCountPtr;

    public:
        /**
         * Constructor.
         */
        constexpr RefCountPtr() noexcept = default;

        /**
         * Constructor from nullptr.
         */
        constexpr RefCountPtr(std::nullptr_t) noexcept;

        /**
         * Add reference counted object.
         * @param in Object to handle reference count.
         * @param bAddRef If initialization of object should add reference (ie COM objects "from thin air" do not or Unreal FRefCountBase do not)
         */
        explicit RefCountPtr(ElementType* in, bool bAddRef = true) noexcept;

        /**
         * Copy constructor. Increments the reference automatically.
         * @param in Object to handle reference count.
         */
        RefCountPtr(const RefCountPtr& in);

        /**
         * Copy constructor. Constructs a RefCountPtr from another RefCountPtr<BaseType> (whose element can be converted to ours).
         * @param in Object to handle reference count.
         */
        template <typename BaseType, std::enable_if_t<std::is_convertible_v<BaseType*, ElementType*>, int> = 0>
        RefCountPtr(const RefCountPtr<BaseType>& in);

        /**
         * Move constructor. Increments the reference automatically.
         * @param in Object to handle reference count.
         */
        RefCountPtr(RefCountPtr&& in);

        /**
         * Move constructor. Takes ownership on element from other RefCountPtr<BaseType> (whose element can be converted to ours).
         * @param in Object to handle reference count.
         */
        template <typename BaseType, std::enable_if_t<std::is_convertible_v<BaseType*, ElementType*>, int> = 0>
        RefCountPtr(RefCountPtr<BaseType>&& in);

        /**
         * Destructor. Calls Release automatically.
         */
        ~RefCountPtr();

        /**
         * Copy operator. Releases any object the wrapper contains and Increments the reference automatically on the new object.
         * @param in Object to handle reference count.
         */
        RefCountPtr& operator=(const RefCountPtr& in);

        /**
         * Copy operator. Makes a copy from another RefCountPtr<BaseType> (whose element can be converted to ours).
         * @param in Object to handle reference count.
         */
        template <typename BaseType, std::enable_if_t<std::is_convertible_v<BaseType*, ElementType*>, int> = 0>
        RefCountPtr& operator=(const RefCountPtr<BaseType>& in);

        /**
         * Move operator. Increments the reference automatically.
         * @param in Object to handle reference count.
         */
        RefCountPtr& operator=(RefCountPtr&& in);

        /**
         * Move operator. Takes ownership on element from other RefCountPtr<BaseType> (whose element can be converted to ours).
         * @param in Object to handle reference count.
         */
        template <typename BaseType, std::enable_if_t<std::is_convertible_v<BaseType*, ElementType*>, int> = 0>
        RefCountPtr& operator=(RefCountPtr<BaseType>&& in);

        /**
         * Add reference counted object.  Releases any object the wrapper contains. Assumes object has already had the reference count incremented.
         * @param in Object to handle reference count.
         */
        RefCountPtr& operator=(ElementType* in);

        /**
         * Returns pointer to the object without calling AddRef.
         * @return pointer to the object.
         */
        ElementType* Get() const noexcept;

        /**
         * Externally re-directing the internal pointer without modifying the reference
         * @return pointer to element pointer
         */
        ElementType** GetInitReference() noexcept;

        /**
         * Clear the container's reference and returns pointer to the object without calling AddRef.
         * @return pointer to the object.
         */
        ElementType* Free() noexcept;

        /**
         * Returns reference to the object.
         * @return reference to the object.
         */
        ElementType& operator*() const noexcept;

        /**
         * Returns pointer to the object. AddRef is not called.
         * @return pointer to the object.
         */
        ElementType* operator->() const noexcept;

        /**
         * Returns if the wrapper is empty or not.
         * @return True if wrapper is not empty.
         */
        explicit operator bool() const noexcept;

        /**
         * Swap the object that each container holds.
         * @param in The container to swap with.
         */
        void Swap(RefCountPtr& in) noexcept;

        /**
         * Compare if pointers in the objects are the same.
         * @param in object to compare.
         * @return True if same object.
         */
        bool operator==(const RefCountPtr& in) const noexcept;

        /**
         * Compare if pointers in the objects are the same.
         * @param in object to compare.
         * @return True if same object.
         */
        bool operator==(ElementType* in) const noexcept;

    private:
        ElementType* _element = nullptr;
    };

    template <typename ElementType>
    constexpr RefCountPtr<ElementType>::RefCountPtr(std::nullptr_t) noexcept
        : _element(nullptr)
    {
    }

    template <typename ElementType>
    inline RefCountPtr<ElementType>::RefCountPtr(ElementType* in, bool bAddRef) noexcept
        : _element(in)
    {
        if (_element && bAddRef)
        {
            _element->AddRef();
        }
    }

    template <typename ElementType>
    RefCountPtr<ElementType>::RefCountPtr(const RefCountPtr& in)
        : _element(in._element)
    {
        if (_element)
        {
            _element->AddRef();
        }
    }

    template <typename ElementType>
    template <typename BaseType, std::enable_if_t<std::is_convertible_v<BaseType*, ElementType*>, int>>
    RefCountPtr<ElementType>::RefCountPtr(const RefCountPtr<BaseType>& in)
        : _element(in._element)
    {
        if (_element)
        {
            _element->AddRef();
        }
    }

    template <typename ElementType>
    inline RefCountPtr<ElementType>::RefCountPtr(RefCountPtr&& in)
        : _element(in._element)
    {
        in._element = nullptr;
    }

    template <typename ElementType>
    template <typename BaseType, std::enable_if_t<std::is_convertible_v<BaseType*, ElementType*>, int>>
    inline RefCountPtr<ElementType>::RefCountPtr(RefCountPtr<BaseType>&& in)
        : _element(in._element)
    {
        in._element = nullptr;
    }

    template <typename ElementType>
    RefCountPtr<ElementType>::~RefCountPtr()
    {
        if (_element)
        {
            _element->Release();
        }
    }

    template <typename ElementType>
    RefCountPtr<ElementType>& RefCountPtr<ElementType>::operator=(const RefCountPtr& in)
    {
        if (this == &in || in._element == _element)
        {
            return *this;
        }

        ElementType* oldElement = _element;
        _element = in._element;

        if (_element)
        {
            _element->AddRef();
        }

        if (oldElement)
        {
            oldElement->Release();
        }

        return *this;
    }

    template <typename ElementType>
    template <typename BaseType, std::enable_if_t<std::is_convertible_v<BaseType*, ElementType*>, int>>
    RefCountPtr<ElementType>& RefCountPtr<ElementType>::operator=(const RefCountPtr<BaseType>& in)
    {
        if (in._element != _element)
        {
            ElementType* oldElement = _element;
            _element = in._element;

            if (_element)
            {
                _element->AddRef();
            }

            if (oldElement)
            {
                oldElement->Release();
            }
        }
        return *this;
    }

    template <typename ElementType>
    RefCountPtr<ElementType>& RefCountPtr<ElementType>::operator=(RefCountPtr&& in)
    {
        if (this == &in || in._element == _element)
        {
            return *this;
        }

        ElementType* oldElement = _element;
        _element = in._element;
        in._element = nullptr;

        if (oldElement)
        {
            oldElement->Release();
        }

        return *this;
    }

    template <typename ElementType>
    template <typename BaseType, std::enable_if_t<std::is_convertible_v<BaseType*, ElementType*>, int>>
    RefCountPtr<ElementType>& RefCountPtr<ElementType>::operator=(RefCountPtr<BaseType>&& in)
    {
        if (in._element != _element)
        {
            ElementType* oldElement = _element;
            _element = in._element;
            in._element = nullptr;

            if (oldElement)
            {
                oldElement->Release();
            }
        }
        return *this;
    }

    template <typename ElementType>
    RefCountPtr<ElementType>& RefCountPtr<ElementType>::operator=(ElementType* in)
    {
        if (_element != in)
        {
            ElementType* oldElement = _element;
            _element = in;

            if (_element)
            {
                _element->AddRef();
            }

            if (oldElement)
            {
                oldElement->Release();
            }
        }
        return *this;
    }

    template <typename ElementType>
    inline void RefCountPtr<ElementType>::Swap(RefCountPtr<ElementType>& in) noexcept
    {
        ElementType* oldElement = _element;
        _element = in._element;
        in._element = oldElement;
    }

    template <typename ElementType>
    inline ElementType* RefCountPtr<ElementType>::Free() noexcept
    {
        ElementType* element = _element;
        _element = nullptr;
        return element;
    }

    template <typename ElementType>
    inline ElementType* RefCountPtr<ElementType>::Get() const noexcept
    {
        return _element;
    }

    template <typename ElementType>
    inline ElementType** RefCountPtr<ElementType>::GetInitReference() noexcept
    {
        *this = nullptr;
        return &_element;
    }

    template <typename ElementType>
    inline ElementType& RefCountPtr<ElementType>::operator*() const noexcept
    {
        return *_element;
    }

    template <typename ElementType>
    inline ElementType* RefCountPtr<ElementType>::operator->() const noexcept
    {
        return _element;
    }

    template <typename ElementType>
    inline RefCountPtr<ElementType>::operator bool() const noexcept
    {
        return _element != nullptr;
    }

    template <typename ElementType>
    inline bool RefCountPtr<ElementType>::operator==(const RefCountPtr& in) const noexcept
    {
        return _element == in._element;
    }

    template <typename ElementType>
    inline bool RefCountPtr<ElementType>::operator==(ElementType* in) const noexcept
    {
        return _element == in;
    }

    // Must be size of single pointer only to allow ComPtr like initialization
    static_assert(sizeof(RefCountPtr<char>) == 8);

    /**
     * Helper Deleter class for handling calling Release on destruction.
     */
    template <typename ElementType>
    class RefCountDeleter
    {
    public:
        void operator()(ElementType* ptr) const
        {
            if (ptr)
            {
                ptr->Release();
            }
        };
    };

    template <typename ElementType, typename... Args>
    inline RefCountPtr<ElementType> MakeRefCountPtr(Args&&... args)
    {
        return RefCountPtr<ElementType>(new ElementType(std::forward<Args>(args)...));
    }

}  // namespace EpicRtc

namespace std
{
    /**
     * Swap the contents of the containers.
     * @param lhs The left hand side container to swap with.
     * @param rhs The right hand side container to swap with.
     */
    template <typename ElementType>
    inline void swap(EpicRtc::RefCountPtr<ElementType>& lhs, EpicRtc::RefCountPtr<ElementType>& rhs) noexcept  // NOLINT(readability-identifier-naming)
    {
        lhs.Swap(rhs);
    }
}  // namespace std
