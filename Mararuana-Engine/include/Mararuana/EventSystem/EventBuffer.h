#pragma once

#include "Mararuana/Core/Core.h"
#include "Mararuana/EventSystem/EventBase.h"

#include <array>
#include <atomic>
#include <cstring>
#include <utility>

// SIMD headers — incluídos somente quando o compilador já definiu as
// macros nativamente, o que só acontece em x86/x86-64. MAR_ENABLE_SSE2/MAR_ENABLE_AVX2
// nunca são forçadas manualmente aqui: quem liga isso de fato é a flag de
// compilador (-msse2/-mavx2, /arch:SSE2//AVX2), configurada centralmente
// em CMakeLists.txt via MAR_SIMD_LEVEL. Como essas macros só existem
// nativamente em compiladores x86, este guard já é, por construção,
// específico de x86 — não precisa checar a arquitetura à parte, e
// cross-compile para ARM/WASM/etc. simplesmente cai no fallback memcpy
// em todos os helpers abaixo.
#if defined(MAR_ENABLE_SSE2)
#   include <immintrin.h>
#endif

namespace Mar {

    inline constexpr size_t CACHELINE = 64u;
    inline constexpr size_t SLOT_SIZE = 64u;                          // uma cache line
    inline constexpr size_t BUFFER_SIZE = 64u * 1024u;
    inline constexpr size_t CAPACITY = BUFFER_SIZE / SLOT_SIZE;     // 1024

    // ── Layout dual de slot ──────────────────────────────────────────────────
    //
    //  HEAD slot — primeiro slot de cada evento
    //  ┌─────────────────────────────────────────────────────────────────┐
    //  │ type:u16 │ chunk_size:u16 │ category:u32                        │  0‥7
    //  │ total_size:u32 │ chunk_index:u8 │ total_chunks:u8 │ _pad:u16   │  8‥15
    //  │ payload[48]   (alinhado em 16 bytes)                            │ 16‥63
    //  └─────────────────────────────────────────────────────────────────┘
    //
    //  CONTINUATION slot — slots subsequentes de eventos multi-slot
    //  ┌─────────────────────────────────────────────────────────────────┐
    //  │ 0xFFFF:u16 │ chunk_size:u16                                     │  0‥3
    //  │ payload[60]   (4-byte-aligned, início em byte 4)               │  4‥63
    //  └─────────────────────────────────────────────────────────────────┘
    //
    //  Reduzir o header de continuation de 16 B → 4 B dá +12 B de payload
    //  por slot. Para um evento de 4096 B isso reduz o número de slots de
    //  86 → 69 (−19,8 %).
    //
    //  O consumer identifica slots de continuation pelo marker 0xFFFF e lê
    //  o payload a partir do byte 4 ao invés do byte 16.
    // ────────────────────────────────────────────────────────────────────────
    inline constexpr size_t   SLOT_HEADER = 16u;
    inline constexpr size_t   PAYLOAD_SIZE = SLOT_SIZE - SLOT_HEADER;   // 48 B
    inline constexpr size_t   CONT_HEADER = 4u;
    inline constexpr size_t   CONT_PAYLOAD_SIZE = SLOT_SIZE - CONT_HEADER;   // 60 B

    // Sentinel no campo type de todo slot de continuation.
    // Não pode colidir com nenhum valor válido de EventType.
    inline constexpr uint16_t CONTINUATION_MARKER = 0xFFFFu;

    inline constexpr size_t MAX_LARGE_EVENT_BYTES = 4096u;

    // Contagem máxima de slots para o pior caso:
    //   1 head (48 B) + ceil((4096 − 48) / 60) continuations = 1 + 68 = 69
    inline constexpr uint8_t MAX_CHUNK_COUNT =
        1u + static_cast<uint8_t>(
            (MAX_LARGE_EVENT_BYTES - PAYLOAD_SIZE + CONT_PAYLOAD_SIZE - 1u) /
            CONT_PAYLOAD_SIZE);

    static_assert((CAPACITY& (CAPACITY - 1u)) == 0u,
        "CAPACITY deve ser potência de dois (bitmask wrap).");
    static_assert(SLOT_SIZE >= SLOT_HEADER + 1u);
    static_assert(CONT_PAYLOAD_SIZE > PAYLOAD_SIZE,
        "Continuation slots devem carregar mais payload que head slots.");
    static_assert(MAX_CHUNK_COUNT <= 255u);

    // ═══════════════════════════════════════════════════════════════════════════
    // Slot
    // ═══════════════════════════════════════════════════════════════════════════
    struct alignas(CACHELINE) Slot {
        // ── Campos do head slot (válidos quando type != CONTINUATION_MARKER) ──
        uint16_t type = 0;
        uint16_t chunk_size = 0;
        uint32_t category = 0;
        uint32_t total_size = 0;
        uint8_t  chunk_index = 0;
        uint8_t  total_chunks = 0;
        uint8_t  _hdr_pad[2] = {};
        alignas(16) std::byte payload[PAYLOAD_SIZE]{};  // 48 B no offset 16

        // ── Acessor do payload de continuation slot ──────────────────────────
        // Válido apenas quando type == CONTINUATION_MARKER.
        // Retorna ponteiro para os 60 B de payload que começam no byte 4.
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
        "Slot deve ocupar exatamente uma cache line.");
    static_assert(offsetof(Slot, payload) == SLOT_HEADER,
        "payload deve estar no offset 16.");

    // ═══════════════════════════════════════════════════════════════════════════
    // Profiling stubs
    // ═══════════════════════════════════════════════════════════════════════════
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

        // ═══════════════════════════════════════════════════════════════════════
        // Helpers SIMD — producer side (escrita no buffer)
        //
        // Todos os paths não-SIMD fazem fallback para std::memcpy, que o
        // compilador auto-vetoriza conforme o target.
        //
        // Nota: SSE2 é obrigatório em x86-64; os guards #ifdef MAR_ENABLE_SSE2 /
        // MAR_ENABLE_AVX2 tornam o código portável para outros arches (ARM, WASM, etc.)
        // sem alterar o comportamento em x86-64.
        // ═══════════════════════════════════════════════════════════════════════

        // Escreve exatamente 48 bytes (PAYLOAD_SIZE) para o payload do head slot.
        // dst deve estar alinhado em 16 bytes.
        MAR_FORCEINLINE void store_48(std::byte* MAR_RESTRICT dst,
            const void* MAR_RESTRICT src) noexcept
        {
#if defined(MAR_ENABLE_SSE2)
            const auto* s = reinterpret_cast<const __m128i*>(src);
            auto* d = reinterpret_cast<__m128i*>(dst);
            _mm_store_si128(d, _mm_loadu_si128(s));
            _mm_store_si128(d + 1, _mm_loadu_si128(s + 1));
            _mm_store_si128(d + 2, _mm_loadu_si128(s + 2));
#else
            std::memcpy(dst, src, PAYLOAD_SIZE);
#endif
        }

        // Escreve Count bytes (32 < Count ≤ 48) para o payload do head slot.
        // dst alinhado em 16 bytes. Zera o tail de 16 bytes quando Count < 48.
        template<size_t Count>
        MAR_FORCEINLINE void store_head_payload(std::byte* MAR_RESTRICT dst,
            const void* MAR_RESTRICT src) noexcept
        {
            static_assert(Count > 32u && Count <= PAYLOAD_SIZE,
                "store_head_payload<Count>: Count deve estar em (32, 48].");
#if defined(MAR_ENABLE_SSE2)
            const auto* s = reinterpret_cast<const std::byte*>(src);
            auto* d = reinterpret_cast<__m128i*>(dst);
            _mm_store_si128(d, _mm_loadu_si128(reinterpret_cast<const __m128i*>(s)));
            _mm_store_si128(d + 1, _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 16u)));
            if constexpr (Count == PAYLOAD_SIZE) {
                _mm_store_si128(d + 2, _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 32u)));
            }
            else {
                // Constrói o último chunk de 16 bytes sem ler além do objeto.
                alignas(16) std::byte tail[16]{};
                std::memcpy(tail, s + 32u, Count - 32u);
                _mm_store_si128(d + 2, *reinterpret_cast<const __m128i*>(tail));
            }
#else
            std::memcpy(dst, src, Count);
            // Zera o tail para evitar stale bytes no slot.
            if constexpr (Count < PAYLOAD_SIZE)
                std::memset(dst + Count, 0, PAYLOAD_SIZE - Count);
#endif
        }

        // Escreve exatamente CONT_PAYLOAD_SIZE (60) bytes para o payload de
        // um continuation slot. dst está no byte 4 do slot (alinhado em 4 B).
        // Dois overlapping stores de 256 bits cobrem [0..31] e [28..59].
        MAR_FORCEINLINE void avx_write_60(std::byte* MAR_RESTRICT dst,
            const std::byte* MAR_RESTRICT src) noexcept
        {
#if defined(MAR_ENABLE_AVX2)
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst),
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src)));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + 28u),
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + 28u)));
#else
            std::memcpy(dst, src, CONT_PAYLOAD_SIZE);
#endif
        }

        // ═══════════════════════════════════════════════════════════════════════
        // Helpers SIMD — consumer side (leitura do buffer para montagem)
        // ═══════════════════════════════════════════════════════════════════════

        // Copia exatamente 48 bytes do payload do head slot (src alinhado em 16 B).
        MAR_FORCEINLINE void load_48(void* MAR_RESTRICT dst,
            const void* MAR_RESTRICT src) noexcept
        {
#if defined(MAR_ENABLE_AVX2)
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

        // Copia exatamente 60 bytes do payload de um continuation slot (src 4-byte-aligned).
        MAR_FORCEINLINE void load_60(void* MAR_RESTRICT dst,
            const void* MAR_RESTRICT src) noexcept
        {
#if defined(MAR_ENABLE_AVX2)
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

    // ═══════════════════════════════════════════════════════════════════════════
    // FastEventBuffer — SPSC ring buffer
    //
    // Invariantes de design:
    //   • Single producer, single consumer — sem locking.
    //   • Estado do producer e consumer em cache lines separadas.
    //   • Eventos multi-slot são commitados em um único w.store, garantindo que
    //     o consumer nunca veja um evento chunked parcialmente escrito.
    //   • PushRaw / PullRaw não devem ser misturados com eventos tipados no
    //     mesmo buffer.
    //
    // Layout de memória (offsets dos membros):
    //   offset   0 : w (producer state)   — cache line 0
    //   offset  64 : r (consumer state)   — cache line 1
    //   offset 128 : buffer[]             — cache lines 2+
    // ═══════════════════════════════════════════════════════════════════════════
    class FastEventBuffer {
    public:
        static constexpr uint32_t MASK = static_cast<uint32_t>(CAPACITY) - 1u;
        static constexpr size_t   USABLE = CAPACITY - 1u;

        // ── Estado do producer — cache line 0 ──────────────────────────────────
        alignas(CACHELINE) std::atomic<uint32_t> w{ 0 };
        uint32_t cached_r{ 0 };
        char _pad_w[CACHELINE
            - sizeof(std::atomic<uint32_t>)
            - sizeof(uint32_t)]{};

        // ── Estado do consumer — cache line 1 ──────────────────────────────────
        alignas(CACHELINE) std::atomic<uint32_t> r{ 0 };
        uint32_t cached_w{ 0 };
        char _pad_r[CACHELINE
            - sizeof(std::atomic<uint32_t>)
            - sizeof(uint32_t)]{};

        // ── Ring storage — cache line 2+ ──────────────────────────────────────
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

        // Push — roteia para single-slot ou chunked com base em sizeof(T).
        template<typename T>
        [[nodiscard]] MAR_FORCEINLINE bool Push(const T& ev,
            EventType t,
            uint32_t  category) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>,
                "O payload do evento deve ser trivially copyable (memcpy no slot).");
            static_assert(sizeof(T) <= MAX_LARGE_EVENT_BYTES,
                "Evento excede MAX_LARGE_EVENT_BYTES. "
                "Aumente a constante ou divida em tipos menores.");
            if constexpr (sizeof(T) <= PAYLOAD_SIZE)
                return PushSingle(ev, t, category);
            else
                return PushChunked(ev, t, category);
        }

        // PushRaw — insere um valor uint32_t bruto sem tipo.
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

        // PullRaw — extrai um valor uint32_t bruto.
        [[nodiscard]] MAR_FORCEINLINE bool PullRaw(uint32_t& out) noexcept {
            const uint32_t cur_r = r.load(std::memory_order_relaxed);

            if (MAR_UNLIKELY(cur_r == cached_w)) {
                if (PullIsEmpty(cur_r)) return false;
            }

            const Slot* s = MAR_ASSUME_ALIGNED(&buffer[cur_r], CACHELINE);
            MAR_CORE_ASSERT(s->type == 0u,
                "PullRaw: slot com type != 0. "
                "Não misture PullRaw() com eventos tipados no mesmo FastEventBuffer.");
            if (MAR_UNLIKELY(s->type != 0u)) {
                r.store((cur_r + 1u) & MASK, std::memory_order_release);
                return false;
            }
            std::memcpy(&out, s->payload, sizeof(out));
            r.store((cur_r + 1u) & MASK, std::memory_order_release);
            return true;
        }

    private:
        // ── Path de slot único (sizeof(T) ≤ 48) ──────────────────────────────
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

            // Para payloads grandes (> 32 B) usamos stores SSE2 alinhados;
            // para payloads menores std::memcpy é igualmente eficiente.
            if constexpr (sizeof(T) > 32u)
                Detail::store_head_payload<sizeof(T)>(s->payload, &ev);
            else
                std::memcpy(s->payload, &ev, sizeof(T));

            // memory_order_release já garante ordering de stores normais —
            // nenhum sfence adicional é necessário.
            w.store(next_w, std::memory_order_release);
            Detail::RecordEmit(t);
            return true;
        }

        // ── Path chunked (sizeof(T) > 48) ────────────────────────────────────
        template<typename T>
        MAR_COLD_FUNC bool PushChunked(const T& ev,
            EventType t,
            uint32_t  category) noexcept
        {
            static_assert(sizeof(T) > PAYLOAD_SIZE,
                "PushChunked chamado para tipo que cabe em slot único.");
            static_assert(sizeof(T) <= MAX_LARGE_EVENT_BYTES,
                "Evento grande demais para PushChunked. Aumente MAX_LARGE_EVENT_BYTES.");

            constexpr uint32_t TOTAL = static_cast<uint32_t>(sizeof(T));
            constexpr uint8_t  N_CONT = static_cast<uint8_t>(
                (TOTAL - PAYLOAD_SIZE + CONT_PAYLOAD_SIZE - 1u) / CONT_PAYLOAD_SIZE);
            constexpr uint8_t  N = 1u + N_CONT;

            static_assert(N_CONT >= 1u,
                "PushChunked requer ao menos um continuation slot.");
            static_assert(N <= MAX_CHUNK_COUNT,
                "Chunk count excede MAX_CHUNK_COUNT. Verifique MAX_LARGE_EVENT_BYTES.");

            const uint32_t cur_w = w.load(std::memory_order_relaxed);

            // ── Verificação de capacidade ─────────────────────────────────────
            {
                const uint32_t used = (cur_w - cached_r) & MASK;
                if (MAR_UNLIKELY(USABLE - used < N)) {
                    cached_r = r.load(std::memory_order_acquire);
                    if (USABLE - ((cur_w - cached_r) & MASK) < N)
                        return false;
                }
            }

            const std::byte* src = reinterpret_cast<const std::byte*>(&ev);

            // ── Head slot ─────────────────────────────────────────────────────
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

            // ── Continuation slots ────────────────────────────────────────────
            // ── Continuation slots (Loop Peeling / Tail Unroll) ───────────────
            uint32_t src_offset = PAYLOAD_SIZE;
            const uint8_t n_full_conts = N_CONT - 1u; // Todos, exceto o último

            // 1. Caminho Quente: Processa todos os chunks de 60 bytes sem NENHUM if
            for (uint8_t ci = 0u; ci < n_full_conts; ++ci) {
                Slot* cs = MAR_ASSUME_ALIGNED(&buffer[(cur_w + 1u + ci) & MASK], CACHELINE);
                cs->type = CONTINUATION_MARKER;
                cs->chunk_size = static_cast<uint16_t>(CONT_PAYLOAD_SIZE);
                Detail::avx_write_60(cs->cont_data(), src + src_offset);
                src_offset += CONT_PAYLOAD_SIZE;
            }

            // 2. Caminho Frio: Resolve apenas o último chunk (que pode ser <= 60 bytes)
            {
                Slot* tail_cs = MAR_ASSUME_ALIGNED(&buffer[(cur_w + 1u + n_full_conts) & MASK], CACHELINE);
                const uint32_t tail_size = TOTAL - src_offset;
                tail_cs->type = CONTINUATION_MARKER;
                tail_cs->chunk_size = static_cast<uint16_t>(tail_size);

                // O compilador otimiza isso aqui perfeitamente sabendo que é executado 1x
                if (tail_size == static_cast<uint32_t>(CONT_PAYLOAD_SIZE)) {
                    Detail::avx_write_60(tail_cs->cont_data(), src + src_offset);
                }
                else {
                    std::memcpy(tail_cs->cont_data(), src + src_offset, tail_size);
                }
            }

            // Publica todos os N slots em um único store atômico.
            w.store((cur_w + N) & MASK, std::memory_order_release);
            Detail::RecordEmit(t);
            return true;
        }
    };

    // ── Asserções de layout ────────────────────────────────────────────────────
    static_assert(offsetof(FastEventBuffer, r) == CACHELINE,
        "r do consumer deve começar na segunda cache line (offset 64).");
    static_assert(offsetof(FastEventBuffer, buffer) == 2u * CACHELINE,
        "Os slots do ring buffer devem começar na terceira cache line (offset 128).");

} // namespace Mar