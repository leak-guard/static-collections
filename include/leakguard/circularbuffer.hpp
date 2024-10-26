#pragma once
#include <array>
#include <cstddef>
#include <cstdlib>
#include <iterator>

namespace lg {

/**
 * @brief A default implementation of CircularBuffer mutex that does nothing
 *
 * It contains an implicitly defined default constructor, as well as two
 * inline member functions that contain no instructions.
 *
 * This struct satisfies MutexImpl concept.
 */
struct NopMutexImpl {
    /**
     * @brief Locks the critical section
     *
     * This implementation is a no-op and does nothing.
     */
    void Lock() { }

    /**
     * @brief Unlocks the critical section
     *
     * This implementation is a no-op and does nothing.
     */
    void Unlock() { }
};

/**
 * @brief An implementation of a generic, constant sized circular buffer
 *        (a.k.a. ring buffer), which is an efficient implementation of FIFO
 *        queue
 *
 * Only the types that are trivial, or have at least a non-deleted trivial
 * destructor, should be used with this implementation of circular buffer.
 * For performance reasons, elements will never be deleted from the ring
 * buffer. Only overwriting will ever occur. That may cause a severe delay
 * between 'popping' an element from the container and calling its destructor.
 *
 * All mutex implementations should be copy-constructible
 * or move-constructible.
 *
 * @sa RingBuffer
 *
 * @tparam T the type of elements being stored, should be default constructible
 * @tparam size buffer capacity
 * @tparam mutexImpl the chosen mutex implementation
 */
template <typename T, std::size_t size, typename MutexImpl_t = NopMutexImpl>
class CircularBuffer {
public:
    static_assert(size > 0, "ring buffer size must be greater than 0");

    /**
     * @brief The type Peek() will return, either T or const volatile T&
     *
     * If T is a scalar type, it will be returned by value, otherwise
     * by a const volatile reference
     */
    using PeekType = std::conditional_t<
        std::is_scalar<T>::value,
        T,
        const volatile T&>;

    /**
     * @brief A const iterator that can be used to iterate over
     *        current buffer contents.
     *
     */
    class ConstIterator {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using iterator_category = std::input_iterator_tag;
        using pointer = T*;
        using reference = T&;

        friend class CircularBuffer;

        /**
         * @brief Dereferences the iterator and gets the value it is currently
         *        pointing at
         *
         * @return an element from the buffer this iterator is currently
         *         pointing at
         */
        const T& operator*() const
        {
            return *m_current;
        }

        /**
         * @brief Increments the iterator and returns reference to self
         *
         * @return reference to self
         */
        ConstIterator& operator++()
        {
            ++m_current;
            if (m_current >= m_end) {
                m_current = m_begin;
            }

            return *this;
        }

        /**
         * @brief Increments the iterator and returns a copy of itself
         *        before incrementation
         *
         * @return a copy of itself before incrementation
         */
        ConstIterator operator++(int)
        {
            auto tmp = *this;
            ++*this;
            return tmp;
        }

        /**
         * @brief Check if two instances of the iterator are equal
         *        (i.e. are pointing at the same element)
         *
         * @param other the second iterator for comparison
         * @return true, if they point at the same element,
         * @return false otherwise
         */
        bool operator==(const ConstIterator& other) const
        {
            return m_current == other.m_current;
        }

        /**
         * @brief Check if two instances of the iterator are inequal
         *        (i.e. are pointing at the same element)
         *
         * @param other the second iterator for comparison
         * @return true, if they point at different elements,
         * @return false otherwise
         */
        bool operator!=(const ConstIterator& other) const
        {
            return !(*this == other);
        }

    private:
        ConstIterator(
            const CircularBuffer<T, size, MutexImpl_t>& instance, const T* current)
            : m_current(current)
            , m_begin(instance.m_buffer.begin())
            , m_end(instance.m_buffer.end())
        {
        }

        const T* m_current;
        const T* m_begin;
        const T* m_end;
    };

    /**
     * @name Constructors and destructors
     */
    ///@{

    /**
     * @brief Default constructor
     *
     * You can provide an initialized mutex implementation object if it lacks
     * the default constructor.
     *
     * @param mutexImpl an initialized mutex implementation class instance
     *                  that will be moved or copied and used internally.
     *                  By default, the zero-parameter constructor will be
     *                  called.
     */
    CircularBuffer(MutexImpl_t mutexImpl = MutexImpl_t()) noexcept(
        std::is_nothrow_constructible<MutexImpl_t, const MutexImpl_t&>::value)
        : m_mutexImpl(std::move(mutexImpl))
    {
    }

    ///@}

    /**
     * @name Operators
     */
    ///@{

    /**
     * @brief Pushes a single element into the circular buffer,
     *        increasing its size
     *
     * If you need to ensure that no error occured during this operation,
     * use PushOne() function. Also, it is generally more performant to
     * run PushMany() once, than simply chain this operator calls.
     *
     * @sa PushOne()
     *
     * @param value the value to be placed into the buffer
     * @return reference to self
     */
    CircularBuffer& operator<<(const T& value) noexcept
    {
        PushOne(value);
        return *this;
    }

    /**
     * @brief Retrieves the next element in the circular buffer.
     *
     * If you need to ensure that no error occured during this operation,
     * use PeekAndPop() function.
     *
     * @sa PeekAndPop()
     *
     * @param value a reference to the value that will be populated by the
     *              next element
     *
     * @return reference to self
     */
    CircularBuffer& operator>>(T& value) noexcept
    {
        PeekAndPop(value);
        return *this;
    }

    ///@}

    /**
     * @name Size getters
     */
    ///@{

    /**
     * @brief Gets the total buffer capacity
     *
     * @return the total capacity, in elements
     */
    [[nodiscard]] constexpr size_t GetCapacity() const noexcept
    {
        return size;
    }

    /**
     * @brief Gets the total buffer capacity
     *
     * @return the total capacity, in bytes
     */
    [[nodiscard]] constexpr size_t GetCapacityBytes() const noexcept
    {
        return size * sizeof(T);
    }

    /**
     * @brief Gets the number of elements that are currently stored
     *        in the buffer
     *
     * @return current buffer length, in elements
     */
    [[nodiscard]] size_t GetCurrentSize() const noexcept
    {
        return m_currentSize;
    }

    /**
     * @brief Checks, if the buffer is empty
     *
     * @return true, if it is empty,
     * @return false otherwise
     */
    [[nodiscard]] bool IsEmpty() const noexcept
    {
        return m_currentSize == 0;
    }

    ///@}

    /**
     * @name Operations
     */
    ///@{

    /**
     * @brief Pushes multiple elements into the circular buffer,
     *        increasing its size
     *
     * @tparam It iterator type, must support preincrementation and dereferencing
     * @param begin an iterator to the beginning
     * @param end an iterator to the end
     * @return the number of elements pushed, which can be less than
     *         the number of elements provided if the buffer is overrun
     */
    template <typename It>
    size_t PushMany(It begin, It end) noexcept
    {
        static_assert(std::is_same<std::decay_t<decltype(*begin)>, std::decay_t<T>>::value,
            "The provided iterator does not dereference to type T");

        size_t currentSize = m_currentSize;
        size_t pushed = 0;

        while (!(begin == end) && currentSize < size) {
            m_buffer[m_writePtr] = *begin;

            // incrementation of volatile is deprecated
            m_writePtr = m_writePtr + 1;

            ++begin;
            ++currentSize;
            ++pushed;

            if (m_writePtr > size) {
                m_writePtr = 0;
            }
        }

        if (pushed > 0) {
            auto lock = AcquireMutex();
            m_currentSize = m_currentSize + pushed;
        }

        return pushed;
    }

    /**
     * @brief Pushes a single element into the circular buffer,
     *        increasing its size
     *
     * @sa operator<<()
     *
     * @param value the value to be placed into the buffer
     * @return true, if the operation succeeded (buffer wasn't full),
     * @return false otherwise
     */
    bool PushOne(const T& value) noexcept
    {
        const T* iterator = &value;
        return PushMany(iterator, iterator + 1) > 0;
    }

    /**
     * @brief Returns the next element in the circular buffer
     *
     * If the buffer is currently empty, some garbage data may be returned,
     * but no exception will ever occur.
     *
     * @sa PeekType
     *
     * @return the next element in the buffer
     */
    PeekType Peek() const noexcept
    {
        return m_buffer[m_readPtr];
    }

    /**
     * @brief Pops the next element in the circular buffer, i.e. removes it
     *
     * If you need to also retrieve the element, you may consider calling
     * PeekAndPop().
     *
     * @sa PeekAndPop()
     *
     * @return true, if the operation succeeded (there was at least one element
               in the buffer),
     * @return false otherwise
     */
    bool Pop() noexcept
    {
        if (IsEmpty())
            return false;

        // incrementation of volatile is deprecated
        size_t newReadPtr = m_readPtr + 1;

        if (newReadPtr > size) {
            newReadPtr = 0;
        }

        m_readPtr = newReadPtr;

        auto lock = AcquireMutex();
        m_currentSize = m_currentSize - 1;
        return true;
    }

    /**
     * @brief Pops the next n elements in the circular buffer, i.e. removes them
     *
     * @param count the number of elements to remove
     * @return the number of elements actually removed
     */
    size_t PopMany(size_t count) noexcept
    {
        if (count > GetCurrentSize()) {
            count = GetCurrentSize();
        }

        size_t newReadPtr = m_readPtr + count;
        if (newReadPtr > size) {
            newReadPtr -= size;
        }

        m_readPtr = newReadPtr;

        auto lock = AcquireMutex();
        m_currentSize = m_currentSize - count;
        return count;
    }

    /**
     * @brief Retrieves the next element in the circular buffer.
     *
     * If the buffer is empty, the reference provided as an argument
     * stays untouched.
     *
     * If you need to only drop the value, use Pop().
     *
     * @sa Pop()
     * @sa operator>>()
     *
     * @param out a reference to the value that will be populated by the
     *            next element
     *
     * @return true, if the operation succeeded
     * @return false otherwise
     */
    bool PeekAndPop(T& out) noexcept
    {
        if (!IsEmpty()) {
            out = m_buffer[m_readPtr];
            return Pop();
        }

        return false;
    }

    /**
     * @brief Clears the circular buffer
     *
     */
    void Clear() noexcept
    {
        auto lock = AcquireMutex();
        m_currentSize = 0;
        m_readPtr = m_writePtr;
    }

    /**
     * @brief Moves as many elements from this buffer to the provided one
     *        as it is currently possible
     *
     * @tparam size2 capacity of the target buffer
     * @tparam MutexImpl_t2 mutex implementation of the target buffer
     * @param target the circular buffer that will receive elements
     * @return the number of moved elements
     */
    template <std::size_t size2, typename MutexImpl_t2>
    size_t MoveTo(CircularBuffer<T, size2, MutexImpl_t2>& target) noexcept
    {
        auto beginIt = begin();
        auto endIt = end();

        size_t count = target.PushMany(beginIt, endIt);
        return PopMany(count);
    }

    /**
     * @brief Returns an iterator to the beginning
     *
     * @return iterator to the beginning
     */
    ConstIterator begin() const noexcept
    {
        return ConstIterator(*this, &m_buffer[m_readPtr]);
    }

    /**
     * @brief Returns an iterator to the end
     *
     * @return iterator to the end
     */
    ConstIterator end() const noexcept
    {
        return ConstIterator(*this, &m_buffer[m_writePtr]);
    }

    ///@}

private:
    std::array<T, size + 1> m_buffer {};
    volatile std::size_t m_readPtr { 0 };
    volatile std::size_t m_writePtr { 0 };
    volatile std::size_t m_currentSize { 0 };
    mutable MutexImpl_t m_mutexImpl;

    /**
     * @brief A RAII wrapper over MutexImpl object
     */
    class MutexHolder {
    public:
        MutexHolder(MutexImpl_t& mutex)
            : m_mutex(mutex)
        {
            m_mutex.Lock();
        }

        ~MutexHolder()
        {
            m_mutex.Unlock();
        }

        MutexHolder(const MutexHolder&) = delete;
        MutexHolder(MutexHolder&&) = default;
        MutexHolder& operator=(const MutexHolder&) = delete;
        MutexHolder& operator=(MutexHolder&&) = default;

    private:
        // NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
        MutexImpl_t& m_mutex;
        // NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
    };

    MutexHolder AcquireMutex() const
    {
        return MutexHolder(m_mutexImpl);
    }
};

/**
 * @brief A shorter, easier to write type alias for CircularBuffer
 *
 * @sa CircularBuffer
 *
 * @tparam T the type of elements being stored, should be default constructible
 * @tparam size buffer capacity
 * @tparam mutexImpl the chosen mutex implementation
 */
template <typename T, std::size_t size, typename MutexImpl_t = NopMutexImpl>
using RingBuffer = CircularBuffer<T, size, MutexImpl_t>;

};
