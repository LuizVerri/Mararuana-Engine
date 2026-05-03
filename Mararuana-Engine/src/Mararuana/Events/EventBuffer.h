#pragma once

#include "Mararuana/Core.h"
#include "Mararuana/Events/EventBase.h"

#include <array>
#include <atomic>
#include <cstring>
#include <utility>

// SIMD headers - include only when supported.
#if defined(__SSE2__)
#   include <immintrin.h>
#endif

namespace Mar {

    inline constexpr size_t CACHELINE = 64u;
    inline constexpr size_t SLOT_SIZE = 64u;                          // one cache line
    inline constexpr size_t BUFFER_SIZE = 64u * 1024u;
    inline constexpr size_t CAPACITY = BUFFER_SIZE / SLOT_SIZE;     // 1024

    // Slot layout - head and continuation
    //
    // Head slot
    // type:u16 chunk_size:u16 category:u32
    // total_size:u32 chunk_index:u8 total_chunks:u8 pad:u16
    // payload[48] aligned at 16 bytes
    //
    // Continuation slot
    // marker:u16 chunk_size:u16
    // payload[60] starts at byte 4
    //
    // Continuation slots use a smaller header to increase payload per slot.
    inline constexpr size_t   SLOT_HEADER = 16u;
    inline constexpr size_t   PAYLOAD_SIZE = SLOT_SIZE - SLOT_HEADER;   // 48 B
    inline constexpr size_t   CONT_HEADER = 4u;
    inline constexpr size_t   CONT_PAYLOAD_SIZE = SLOT_SIZE - CONT_HEADER;   // 60 B

    // Marker used by continuation slots.
    inline constexpr uint16_t CONTINUATION_MARKER = 0xFFFFu;

    inline constexpr size_t MAX_LARGE_EVENT_BYTES = 4096u;

    // Max chunks for worst case.
    inline constexpr uint8_t MAX_CHUNK_COUNT =
        1u + static_cast<uint8_t>(
            (MAX_LARGE_EVENT_BYTES - PAYLOAD_SIZE + CONT_PAYLOAD_SIZE - 1u) /
            CONT_PAYLOAD_SIZE);

    static_assert((CAPACITY& (CAPACITY - 1u)) == 0u,
        "CAPACITY must be power of two.");
    static_assert(SLOT_SIZE >= SLOT_HEADER + 1u);
    static_assert(CONT_PAYLOAD_SIZE > PAYLOAD_SIZE,
        "Continuation slots must carry more payload.");
    static_assert(MAX_CHUNK_COUNT <= 255u);

    // Slot
    struct alignas(CACHELINE) Slot {
        uint16_t type = 0;
        uint16_t chunk_size = 0;
        uint32_t category = 0;
        uint32_t total_size = 0;
        uint8_t  chunk_index = 0;
        uint8_t  total_chunks = 0;
        uint8_t  _hdr_pad[2] = {};
        alignas(16) std::byte payload[PAYLOAD_SIZE]{};

        // Continuation data access.
        [[nodiscard]] MAR_FORCEINLINE
            std::byte* cont_data() noexcept {
            return reinterpret_cast<std::byte*>(this) + CONT_HEADER;
        }
        [[nodiscard]] MAR_FORCEINLINE
            const std::byte* cont_data() const noexcept {
            return reinterpret_cast<const std::byte*>(this) + CONT_HEADER;
        }
    };

    static_assert(sizeof(Slot) == SLOT_SIZE,
        "Slot must be exactly one cache line.");
    static_assert(offsetof(Slot, payload) == SLOT_HEADER,
        "payload must start at offset 16.");

    // Profiling stubs
    namespace Detail {

#ifdef MAR_EVENT_PROFILING
        inline std::atomic<uint64_t> g_EmitCount[TotalEventTypes]{};
        inline std::atomic<uint64_t> g_DispatchCount[TotalEventTypes]{};

        MAR_FORCEINLINE void RecordEmit(EventType t) noexcept {
            const size_t v = static_cast<size_t>(t);
            if (MAR_UNLIKELY(v == 0u)) return;
            const size_t i = v - 1u;
            if (MAR_LIKELY(i < TotalEventTypes))
                g_EmitCount[i].fetch_add(1u, std::memory_order_relaxed);
        }
        MAR_FORCEINLINE void RecordDispatch(size_t index) noexcept {
            if (MAR_LIKELY(index < TotalEventTypes))
                g_DispatchCount[index].fetch_add(1u, std::memory_order_relaxed);
        }
#else
        MAR_FORCEINLINE void RecordEmit(EventType)  noexcept {}
        MAR_FORCEINLINE void RecordDispatch(size_t) noexcept {}
#endif

        // Producer helpers

        // Write exactly 48 bytes to the head payload.
        MAR_FORCEINLINE void store_48(std::byte* MAR_RESTRICT dst,
            const void* MAR_RESTRICT src) noexcept
        {
#if defined(__SSE2__)
            const auto* s = reinterpret_cast<const __m128i*>(src);
            auto* d = reinterpret_cast<__m128i*>(dst);
            _mm_store_si128(d, _mm_loadu_si128(s));
            _mm_store_si128(d + 1, _mm_loadu_si128(s + 1));
            _mm_store_si128(d + 2, _mm_loadu_si128(s + 2));
#else
            std::memcpy(dst, src, PAYLOAD_SIZE);
#endif
        }

        // Write Count bytes to the head payload.
        template<size_t Count>
        MAR_FORCEINLINE void store_head_payload(std::byte* MAR_RESTRICT dst,
            const void* MAR_RESTRICT src) noexcept
        {
            static_assert(Count > 32u && Count <= PAYLOAD_SIZE,
                "store_head_payload<Count>: Count must be in (32, 48].");
#if defined(__SSE2__)
            const auto* s = reinterpret_cast<const std::byte*>(src);
            auto* d = reinterpret_cast<__m128i*>(dst);
            _mm_store_si128(d, _mm_loadu_si128(reinterpret_cast<const __m128i*>(s)));
            _mm_store_si128(d + 1, _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 16u)));
            if constexpr (Count == PAYLOAD_SIZE) {
                _mm_store_si128(d + 2, _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 32u)));
            }
            else {
                alignas(16) std::byte tail[16]{};
                std::memcpy(tail, s + 32u, Count - 32u);
                _mm_store_si128(d + 2, *reinterpret_cast<const __m128i*>(tail));
            }
#else
            std::memcpy(dst, src, Count);
            if constexpr (Count < PAYLOAD_SIZE)
                std::memset(dst + Count, 0, PAYLOAD_SIZE - Count);
#endif
        }

        // Write exactly 60 bytes to a continuation payload.
        MAR_FORCEINLINE void avx_write_60(std::byte* MAR_RESTRICT dst,
            const std::byte* MAR_RESTRICT src) noexcept
        {
#if defined(__AVX2__)
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst),
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src)));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + 28u),
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + 28u)));
#else
            std::memcpy(dst, src, CONT_PAYLOAD_SIZE);
#endif
        }

        // Consumer helpers

        // Copy exactly 48 bytes from head payload.
        MAR_FORCEINLINE void load_48(void* MAR_RESTRICT dst,
            const void* MAR_RESTRICT src) noexcept
        {
#if defined(__AVX2__)
            const auto* s = reinterpret_cast<const std::byte*>(src);
            auto* d = reinterpret_cast<std::byte*>(dst);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d),
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s)));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 32u),
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 32u)));
#else
            std::memcpy(dst, src, PAYLOAD_SIZE);
#endif
        }

        // Copy exactly 60 bytes from continuation payload.
        MAR_FORCEINLINE void load_60(void* MAR_RESTRICT dst,
            const void* MAR_RESTRICT src) noexcept
        {
#if defined(__AVX2__)
            const auto* s = reinterpret_cast<const std::byte*>(src);
            auto* d = reinterpret_cast<std::byte*>(dst);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d),
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s)));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 28u),
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 28u)));
#else
            std::memcpy(dst, src, CONT_PAYLOAD_SIZE);
#endif
        }

    } // namespace Detail

    // FastEventBuffer - SPSC ring buffer
    //
    // Invariants:
    //   - Single producer, single consumer.
    //   - Producer and consumer state are in separate cache lines.
    //   - Multi slot events are published with one w.store.
    //   - Do not mix PushRaw and typed events in the same buffer.
    class FastEventBuffer {
    public:
        static constexpr uint32_t MASK = static_cast<uint32_t>(CAPACITY) - 1u;
        static constexpr size_t   USABLE = CAPACITY - 1u;

        // Producer state
        alignas(CACHELINE) std::atomic<uint32_t> w{ 0 };
        uint32_t cached_r{ 0 };
        char _pad_w[CACHELINE
            - sizeof(std::atomic<uint32_t>)
            - sizeof(uint32_t)]{};

        // Consumer state
        alignas(CACHELINE) std::atomic<uint32_t> r{ 0 };
        uint32_t cached_w{ 0 };
        char _pad_r[CACHELINE
            - sizeof(std::atomic<uint32_t>)
            - sizeof(uint32_t)]{};

        // Ring storage
        alignas(CACHELINE) Slot buffer[CAPACITY]{};

        void reset() noexcept {
            w.store(0, std::memory_order_relaxed);
            r.store(0, std::memory_order_relaxed);
            cached_r = 0;
            cached_w = 0;
        }

        [[nodiscard]] bool Empty() const noexcept {
            return w.load(std::memory_order_relaxed) ==
                r.load(std::memory_order_relaxed);
        }

        MAR_COLD_FUNC bool PushIsFull(uint32_t next_w) noexcept {
            cached_r = r.load(std::memory_order_acquire);
            return next_w == cached_r;
        }

        MAR_COLD_FUNC bool PullIsEmpty(uint32_t cur_r) noexcept {
            cached_w = w.load(std::memory_order_acquire);
            return cur_r == cached_w;
        }

        // Push typed event.
        template<typename T>
        [[nodiscard]] MAR_FORCEINLINE bool Push(const T& ev,
            EventType t,
            uint32_t  category) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>,
                "Event payload must be trivially copyable.");
            static_assert(sizeof(T) <= MAX_LARGE_EVENT_BYTES,
                "Event exceeds MAX_LARGE_EVENT_BYTES.");
            if constexpr (sizeof(T) <= PAYLOAD_SIZE)
                return PushSingle(ev, t, category);
            else
                return PushChunked(ev, t, category);
        }

        // Push raw uint32_t.
        [[nodiscard]] MAR_FORCEINLINE bool PushRaw(uint32_t value) noexcept {
            const uint32_t cur_w = w.load(std::memory_order_relaxed);
            const uint32_t next_w = (cur_w + 1u) & MASK;

            if (MAR_UNLIKELY(next_w == cached_r)) {
                if (PushIsFull(next_w)) return false;
            }

            Slot* s = MAR_ASSUME_ALIGNED(&buffer[cur_w], CACHELINE);
            s->type = 0u;
            s->chunk_size = static_cast<uint16_t>(sizeof(value));
            s->category = 0u;
            s->total_size = static_cast<uint32_t>(sizeof(value));
            s->chunk_index = 0u;
            s->total_chunks = 1u;
            std::memcpy(s->payload, &value, sizeof(value));
            w.store(next_w, std::memory_order_release);
            return true;
        }

        // Pull raw uint32_t.
        [[nodiscard]] MAR_FORCEINLINE bool PullRaw(uint32_t& out) noexcept {
            const uint32_t cur_r = r.load(std::memory_order_relaxed);

            if (MAR_UNLIKELY(cur_r == cached_w)) {
                if (PullIsEmpty(cur_r)) return false;
            }

            const Slot* s = MAR_ASSUME_ALIGNED(&buffer[cur_r], CACHELINE);
            MAR_CORE_ASSERT(s->type == 0u,
                "PullRaw: slot type must be 0.");
            if (MAR_UNLIKELY(s->type != 0u)) {
                r.store((cur_r + 1u) & MASK, std::memory_order_release);
                return false;
            }
            std::memcpy(&out, s->payload, sizeof(out));
            r.store((cur_r + 1u) & MASK, std::memory_order_release);
            return true;
        }

    private:
        // Single slot path.
        template<typename T>
        MAR_FORCEINLINE bool PushSingle(const T& ev,
            EventType t,
            uint32_t  category) noexcept
        {
            const uint32_t cur_w = w.load(std::memory_order_relaxed);
            const uint32_t next_w = (cur_w + 1u) & MASK;

            if (MAR_UNLIKELY(next_w == cached_r)) {
                if (PushIsFull(next_w)) return false;
            }

            Slot* s = MAR_ASSUME_ALIGNED(&buffer[cur_w], CACHELINE);
            s->type = static_cast<uint16_t>(t);
            s->chunk_size = static_cast<uint16_t>(sizeof(T));
            s->category = category;
            s->total_size = static_cast<uint32_t>(sizeof(T));
            s->chunk_index = 0u;
            s->total_chunks = 1u;

            if constexpr (sizeof(T) > 32u)
                Detail::store_head_payload<sizeof(T)>(s->payload, &ev);
            else
                std::memcpy(s->payload, &ev, sizeof(T));

            w.store(next_w, std::memory_order_release);
            Detail::RecordEmit(t);
            return true;
        }

        // Chunked path.
        template<typename T>
        MAR_COLD_FUNC bool PushChunked(const T& ev,
            EventType t,
            uint32_t  category) noexcept
        {
            static_assert(sizeof(T) > PAYLOAD_SIZE,
                "PushChunked called for a type that fits in one slot.");
            static_assert(sizeof(T) <= MAX_LARGE_EVENT_BYTES,
                "Event too large for PushChunked.");

            constexpr uint32_t TOTAL = static_cast<uint32_t>(sizeof(T));
            constexpr uint8_t  N_CONT = static_cast<uint8_t>(
                (TOTAL - PAYLOAD_SIZE + CONT_PAYLOAD_SIZE - 1u) / CONT_PAYLOAD_SIZE);
            constexpr uint8_t  N = 1u + N_CONT;

            static_assert(N_CONT >= 1u,
                "PushChunked needs at least one continuation slot.");
            static_assert(N <= MAX_CHUNK_COUNT,
                "Chunk count exceeds MAX_CHUNK_COUNT.");

            const uint32_t cur_w = w.load(std::memory_order_relaxed);

            // Capacity check
            {
                const uint32_t used = (cur_w - cached_r) & MASK;
                if (MAR_UNLIKELY(USABLE - used < N)) {
                    cached_r = r.load(std::memory_order_acquire);
                    if (USABLE - ((cur_w - cached_r) & MASK) < N)
                        return false;
                }
            }

            const std::byte* src = reinterpret_cast<const std::byte*>(&ev);

            // Head slot
            {
                Slot* head = MAR_ASSUME_ALIGNED(&buffer[cur_w & MASK], CACHELINE);
                head->type = static_cast<uint16_t>(t);
                head->chunk_size = static_cast<uint16_t>(PAYLOAD_SIZE);
                head->category = category;
                head->total_size = TOTAL;
                head->chunk_index = 0u;
                head->total_chunks = N;
                Detail::store_48(head->payload, src);
            }

            // Continuation slots
            uint32_t src_offset = PAYLOAD_SIZE;
            const uint8_t n_full_conts = N_CONT - 1u;

            for (uint8_t ci = 0u; ci < n_full_conts; ++ci) {
                Slot* cs = MAR_ASSUME_ALIGNED(&buffer[(cur_w + 1u + ci) & MASK], CACHELINE);
                cs->type = CONTINUATION_MARKER;
                cs->chunk_size = static_cast<uint16_t>(CONT_PAYLOAD_SIZE);
                Detail::avx_write_60(cs->cont_data(), src + src_offset);
                src_offset += CONT_PAYLOAD_SIZE;
            }

            // Tail chunk
            {
                Slot* tail_cs = MAR_ASSUME_ALIGNED(&buffer[(cur_w + 1u + n_full_conts) & MASK], CACHELINE);
                const uint32_t tail_size = TOTAL - src_offset;
                tail_cs->type = CONTINUATION_MARKER;
                tail_cs->chunk_size = static_cast<uint16_t>(tail_size);

                if (tail_size == static_cast<uint32_t>(CONT_PAYLOAD_SIZE)) {
                    Detail::avx_write_60(tail_cs->cont_data(), src + src_offset);
                }
                else {
                    std::memcpy(tail_cs->cont_data(), src + src_offset, tail_size);
                }
            }

            w.store((cur_w + N) & MASK, std::memory_order_release);
            Detail::RecordEmit(t);
            return true;
        }
    };

    // Layout checks
    static_assert(offsetof(FastEventBuffer, r) == CACHELINE,
        "r must start at offset 64.");
    static_assert(offsetof(FastEventBuffer, buffer) == 2u * CACHELINE,
        "buffer must start at offset 128.");

} // namespace Mar