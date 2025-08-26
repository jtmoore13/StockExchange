#pragma once
#include <atomic>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/*
    This is a simple implementation of a lock-free, single producer
    single consumer queue. With a lot of comments :)

    Note: we can only use this class with trivial types 
    b/c std::memcpy is undefined behavior for non-trivial types.

    Also note: it's much easier to keep the definitions of templated
    functions/classes in the header file rather than the .cpp file. This 
    is because when templated fxns are instantiated, the compiler needs to see
    the definition at the call site. So in main.cpp, if you 
    use foo<int>(), (by including foo.h), but the definition of foo<int>()
    is NOT in foo.h (i.e, it's in foo.cpp), you will get a compiler
    error.
*/

template <typename T, size_t Capacity>
class RingBuffer 
{
public:
    RingBuffer() = default;

    /*
        Good to note that all of these memory orders simply make the 
        queue more efficient. If we didn't specify memory orders in any 
        of our load and stores, the default memory_order_seq_cst would
        work just fine functionally.

        I've included some mental notes about memory orders below because
        they're tricky and have taken me some time to wrap my head around.
        And writing them out has helped me understand them better. 
    */

    bool Enqueue(const T& item)
    {
        /*
            [std::memory_order_relaxed]

            This thread is the only thread to ever write to head_ (SPSC) so we
            can load it with no concerns about re-ordering. There are no memory 
            operations before or after this line that require a specific ordering 
            relative  to the load, so give the CPU/compiler free reign to do whatever.

            We are telling the CPU/compiler: "re-order anything you want to, just load
            head_ atomically for me."
        */
        int head = head_.load(std::memory_order_relaxed);
        int next_head = (head + 1) % internalCapacity_;
        /*
            [std::memory_order_acquire]

            The consumer thread is the one that writes to tail, so we need 
            the latest value of it before doing anything that might
            depend on it. memory_order_acquire prevents reads and writes 
            (atomic and non-atomic) from after this line from being re-ordered 
            before it.
            
            It's a barrier saying: “don't let anything below this line execute
            until tail_ is up-to-date.”
        */
        if (next_head == tail_.load(std::memory_order_acquire))
        {
            return false; // queue is full
        }

        std::memcpy(&buffer_[head], &item, sizeof(T));
        /*
            [std::memory_order_release]

            We use memory_order_release here to guarantee that all memory writes 
            (atomic and non-atomic) above this line cannot be re-ordered below it.
            Otherwise, there's a chance the consumer could load the new head_, 
            check buffer[head_], and see garbage/old values (if the buffer write hasn't
            flushed to L1 yet). A release-store forces the store buffer to be flushed
            to L1.
            
            It's a barrier saying: "before other threads can see the new value of head_,
            make sure all writes to memory above me has already happened."
        */
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    bool Dequeue(T& out) 
    {
        int tail = tail_.load(std::memory_order_relaxed);
        // make sure we load the latest head_ before proceeding
        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // queue is empty
        }

        std::memcpy(&out, &buffer_[tail], sizeof(T));
        // make sure we copy the contents at tail into the out param before updating tail
        tail_.store((tail + 1) % internalCapacity_, std::memory_order_release);
        return true;
    }

    size_t GetSize() const 
    {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return (head + internalCapacity_ - tail) % internalCapacity_;
    }

    size_t GetCapacity() const
    {
        return Capacity;
    }

private:
    // we want head_ and tail_ to be on separate cache lines to avoid false sharing
    alignas(64) std::atomic<int> head_ = 0;  // the next free index to write to
    alignas(64) std::atomic<int> tail_ = 0;  // index of the oldest element
    
    /*
        We allocate one extra slot so we can tell the difference between
        a full queue and an empty one:

        Empty: head_ == tail_
        Full:  head_ + 1 == tail_
    */
    static constexpr size_t internalCapacity_ = Capacity + 1;
    T buffer_[internalCapacity_]; 
};

