#pragma once
#include "RingBuffer.h"
#include <array>

namespace ExchangeConfig {
    constexpr double TICK_SIZE = .01;
    constexpr uint16_t MAX_INSTRUMENTS_SUPPORTED = 5; // number of unique instruments
}

namespace OrderLimits {
    constexpr uint16_t MIN_ALLOWED_QTY = 0;
    constexpr uint16_t MAX_ALLOWED_QTY = 500;
    constexpr double MIN_ALLOWED_PRICE = ExchangeConfig::TICK_SIZE;
    constexpr double MAX_ALLOWED_PRICE = 100.00;
    constexpr uint8_t MAX_INSTRUMENT_LEN = 10;
}

template <size_t MaxSize>
struct FixedSizeMsg {
    std::array<std::byte, MaxSize> msg;
    uint32_t msgSize; // actual message size
};

namespace ExchangeIPC {

    static constexpr size_t SHARED_QUEUE_CAPACITY = 4096;
    static constexpr const char* SHM_FILENAME = "/shm_exchange";

    // We need each queue message to be the same, known size. Even if
    // its payload might differ in size. That way, we can instantiate
    // a std::array in shared memory to pass messages bet. No heap allocations.
    using QueueMsg = FixedSizeMsg<256>;

    using ExchangeToServerQueue = RingBuffer<QueueMsg, SHARED_QUEUE_CAPACITY>;
    using ServerToExchangeQueue = RingBuffer<QueueMsg, SHARED_QUEUE_CAPACITY>;
    using SupportedInstrumentsArray = std::array<char[OrderLimits::MAX_INSTRUMENT_LEN + 1], ExchangeConfig::MAX_INSTRUMENTS_SUPPORTED>;
    
    /* =========================================================== */
    struct Layout {
        alignas(64) ExchangeToServerQueue exchangeToServerQueue;
        alignas(64) ServerToExchangeQueue serverToExchangeQueue;

        SupportedInstrumentsArray supportedInstruments;
        size_t supportedInstrumentsSize = 0;

        std::atomic<bool> isOpen = false;
    };
    static_assert(std::is_trivially_copyable_v<Layout>);
    /* =========================================================== */

    static constexpr size_t SHM_SIZE = sizeof(Layout);
}