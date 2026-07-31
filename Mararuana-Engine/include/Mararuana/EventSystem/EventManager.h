// EventManager.h
#pragma once

#include "Mararuana/Core/Core.h"
#include "Mararuana/EventSystem/EventBuffer.h"

#include <new>
#include <array>
#include <numeric>
#include <cstdio>
#include <cstdlib>

namespace Mar {

    // Small callable wrapper with an inline storage buffer (SBO).
    // Avoids heap allocation for most lambdas. Functors that exceed SBO_SIZE
    // or that need more than 8-byte alignment will not compile, by design.
    //
    // Destroy and Relocate are merged into a single ManageFn selected by an
    // op tag. Neither runs on the dispatch hot path (only on construction,
    // move, and destruction of the listener entry itself), so the extra
    // branch inside is free where it matters — and it buys back one whole
    // function-pointer slot (8 B) of inline storage with zero net change in
    // Delegate's total size, which keeps ListenerEntry exactly one cache
    // line (see static_assert below) while letting captures be ~25% larger.
    class Delegate {
    public:
        static constexpr size_t SBO_SIZE = 40u;

        enum class ManageOp : uint8_t { Destroy, Relocate };

        using InvokeFn = void (*)(void* storage, void* event) noexcept;
        // dst is unused for Destroy (src is destroyed in place).
        using ManageFn = void (*)(ManageOp op, void* dst, void* src) noexcept;

        alignas(8) std::byte _storage[SBO_SIZE]{};
        InvokeFn _invoke = nullptr;
        ManageFn _manage = nullptr;

        Delegate() = default;

        ~Delegate() {
            if (_manage) _manage(ManageOp::Destroy, nullptr, _storage);
        }

        Delegate(const Delegate&) = delete;
        Delegate& operator=(const Delegate&) = delete;

        Delegate(Delegate&& o) noexcept
            : _invoke(o._invoke), _manage(o._manage)
        {
            if (_manage) _manage(ManageOp::Relocate, _storage, o._storage);
            else         std::memcpy(_storage, o._storage, SBO_SIZE);
            o._invoke = nullptr; o._manage = nullptr;
        }

        Delegate& operator=(Delegate&& o) noexcept {
            if (this != &o) {
                if (_manage) _manage(ManageOp::Destroy, nullptr, _storage);
                _invoke = o._invoke;
                _manage = o._manage;
                if (_manage) _manage(ManageOp::Relocate, _storage, o._storage);
                else         std::memcpy(_storage, o._storage, SBO_SIZE);
                o._invoke = nullptr; o._manage = nullptr;
            }
            return *this;
        }

        // Constructs a Delegate from any callable F.
        // Static asserts catch oversized or throw-happy functors early.
        template<typename F>
        [[nodiscard]] static Delegate Make(F&& f) noexcept {
            using FD = std::decay_t<F>;
            static_assert(sizeof(FD) <= SBO_SIZE,
                "Functor exceeds Delegate::SBO_SIZE (40 bytes).");
            static_assert(alignof(FD) <= 8u,
                "Functor alignment exceeds the SBO maximum of 8 bytes.");
            static_assert(std::is_nothrow_constructible_v<FD, F&&>,
                "Functor must be nothrow-constructible from F&&.");
            static_assert(std::is_nothrow_move_constructible_v<FD>,
                "Functor must be nothrow-move-constructible.");

            constexpr bool trivialDtor = std::is_trivially_destructible_v<FD>;
            constexpr bool trivialCopy = std::is_trivially_copyable_v<FD>;
            // trivialCopy implies trivialDtor by definition, so this is just
            // "needs any non-trivial handling at all".
            constexpr bool needsManage = !trivialDtor || !trivialCopy;

            Delegate d;
            new (d._storage) FD(std::forward<F>(f));
            d._invoke = [](void* s, void* ev) noexcept {
                (*static_cast<FD*>(s))(ev);
                };
            if constexpr (needsManage) {
                d._manage = [](ManageOp op, void* dst, void* src) noexcept {
                    if (op == ManageOp::Destroy) {
                        if constexpr (!trivialDtor)
                            static_cast<FD*>(src)->~FD();
                    }
                    else { // Relocate
                        if constexpr (!trivialCopy) {
                            new (dst) FD(std::move(*static_cast<FD*>(src)));
                            static_cast<FD*>(src)->~FD();
                        }
                    }
                    };
            }
            return d;
        }

        MAR_FORCEINLINE void Invoke(void* ev) noexcept {
            MAR_CORE_ASSERT(_invoke != nullptr, "Delegate::Invoke called on an empty delegate.");
            _invoke(_storage, ev);
        }

        [[nodiscard]] bool Valid() const noexcept { return _invoke != nullptr; }
    };

    // Handle returned by Subscribe. Pass it to Unsubscribe (or give it to a
    // ScopedToken) to remove the listener later.
    struct SubscriptionToken {
        EventType type = EventType::None;
        bool      isWildcard = false;
        uint8_t   _pad = 0u;
        uint32_t  id = 0u;

        [[nodiscard]] bool Valid() const noexcept { return id != 0u; }
    };

    // Type-erased view of an event, used by wildcard / category listeners.
    struct EventView {
        EventType   type;
        uint32_t    category;
        const void* payload;
        uint32_t    size;

        // Casts the payload back to the concrete event type.
        // Caller is responsible for using the correct type.
        template<typename T>
        [[nodiscard]] const T& As() const noexcept {
            return *static_cast<const T*>(payload);
        }
    };

    inline constexpr size_t MAX_LISTENERS_PER_EVENT = 16u;

    // One entry in a listener bucket: priority, unique id, and the callback.
    // The high bit of id is set for "handled" listeners so the bucket can track
    // whether any handled listener is present without a separate flag scan.
    struct ListenerEntry {
        int      priority = 0;
        uint32_t id = 0u;
        Delegate delegate;

        [[nodiscard]] bool     IsHandled() const noexcept { return (id >> 31u) != 0u; }
        [[nodiscard]] uint32_t RawId()     const noexcept { return id & 0x7FFFFFFFu; }
    };
    static_assert(sizeof(ListenerEntry) == 64u,
        "ListenerEntry size changed, reverify bucket layout.");

    // Holds all listeners for one specific event type.
    // Entries are kept sorted by priority descending so dispatch walks them
    // from highest to lowest priority.
    struct alignas(CACHELINE) Bucket {
        uint32_t count = 0u;
        bool     hasHandledListeners = false;
        uint8_t  _bucket_pad[3]{};
        alignas(alignof(ListenerEntry))
            std::byte raw[MAX_LISTENERS_PER_EVENT * sizeof(ListenerEntry)]{};

        Bucket() = default;
        Bucket(const Bucket&) = delete;
        Bucket& operator=(const Bucket&) = delete;

        ~Bucket() {
            ListenerEntry* p = data();
            for (uint32_t i = 0u; i < count; ++i) p[i].~ListenerEntry();
        }

        ListenerEntry* data() noexcept {
            return std::launder(reinterpret_cast<ListenerEntry*>(raw));
        }
        const ListenerEntry* data() const noexcept {
            return std::launder(reinterpret_cast<const ListenerEntry*>(raw));
        }

        // Inserts in descending priority order.
        // Walks backward from the end, which is cheap when inserting at or near
        // the back (equal or lower priority than existing entries).
        bool Insert(ListenerEntry&& entry) noexcept {
            if (MAR_UNLIKELY(count >= MAX_LISTENERS_PER_EVENT)) return false;
            ListenerEntry* p = data();

            uint32_t pos = count;
            while (pos > 0u && p[pos - 1u].priority < entry.priority)
                --pos;

            if (pos == count) {
                new (p + count) ListenerEntry(std::move(entry));
            }
            else {
                new (p + count) ListenerEntry(std::move(p[count - 1u]));
                for (uint32_t i = count - 1u; i > pos; --i)
                    p[i] = std::move(p[i - 1u]);
                p[pos] = std::move(entry);
            }
            ++count;
            return true;
        }

        bool Remove(uint32_t rawTargetId) noexcept {
            ListenerEntry* p = data();
            for (uint32_t i = 0u; i < count; ++i) {
                if (p[i].RawId() != rawTargetId) continue;

                const bool wasHandled = p[i].IsHandled();
                p[i].~ListenerEntry();
                for (uint32_t j = i; j + 1u < count; ++j) {
                    new (p + j) ListenerEntry(std::move(p[j + 1u]));
                    p[j + 1u].~ListenerEntry();
                }
                --count;

                // If the removed listener was a handled one, rescan to see
                // whether any handled listeners remain.
                if (wasHandled) {
                    bool any = false;
                    for (uint32_t k = 0u; k < count; ++k) any |= p[k].IsHandled();
                    hasHandledListeners = any;
                }
                return true;
            }
            return false;
        }
    };

    inline constexpr size_t MAX_WILDCARD_LISTENERS = 16u;

    // A category-based listener that receives any event whose category flags
    // overlap with categoryMask.
    struct WildcardEntry {
        uint32_t categoryMask = 0u;
        uint32_t id = 0u;
        int      priority = 0;
        uint8_t  _pad[4]{};
        Delegate delegate;
    };

    class EventManager {
    private:
        // Converts an EventType to a zero-based bucket index.
        // Uses a precomputed table so the conversion is branchless.
        [[nodiscard]] static constexpr size_t TypeToIndex(EventType t) noexcept {
            constexpr auto table = []() constexpr {
                std::array<size_t, static_cast<size_t>(EventType::EndTypeValue)> tbl{};
                for (size_t i = 0; i < static_cast<size_t>(EventType::EndTypeValue); ++i)
                    tbl[i] = (i == 0u) ? TotalEventTypes : i - 1u;
                return tbl;
                }();
            return table[static_cast<size_t>(t)];
        }

        // Runs wildcard listeners first, then the typed listeners for this event.
        // One-listener fast path skips the loop and the handled check entirely.
        MAR_FORCEINLINE void DispatchPayload(void* ev,
            EventType evType,
            uint32_t  category,
            uint32_t  totalSize,
            uint32_t  wildcardCount) noexcept
        {
            if (MAR_UNLIKELY(evType == EventType::None)) return;
            const size_t index = TypeToIndex(evType);
            if (MAR_UNLIKELY(index >= TotalEventTypes)) return;

            // Wildcard listeners only get called when there are any registered
            // and the event category overlaps the combined wildcard mask.
            if (MAR_UNLIKELY(wildcardCount > 0u) && (category & m_GlobalWildcardMask)) {
                EventView view{ evType, category, ev, totalSize };
                for (uint32_t wi = 0u; wi < wildcardCount; ++wi) {
                    if (m_Wildcards[wi].categoryMask & category)
                        m_Wildcards[wi].delegate.Invoke(&view);
                }
            }

            Bucket& bucket = *MAR_ASSUME_ALIGNED(&m_Listeners[index], CACHELINE);
            const uint32_t n = bucket.count;
            if (MAR_UNLIKELY(n == 0u)) return;

            ListenerEntry* p = bucket.data();
            Detail::RecordDispatch(index);

            // Fast path: only one listener, no need to check Handled at all.
            if (MAR_LIKELY(n == 1u)) {
                p[0u].delegate.Invoke(ev);
                return;
            }

            // If any listener can set Handled, check after each call and stop early.
            if (MAR_UNLIKELY(bucket.hasHandledListeners)) {
                for (uint32_t i = 0u; i < n; ++i) {
                    p[i].delegate.Invoke(ev);
                    if (MAR_UNLIKELY(*static_cast<const bool*>(ev))) return;
                }
                return;
            }

            // Normal path: no handled listeners, just invoke all of them.
            for (uint32_t i = 0u; i < n; ++i)
                p[i].delegate.Invoke(ev);
        }

        // Dispatches a single-slot event directly from the slot's payload.
        MAR_FORCEINLINE void DispatchSlot(const Slot& slot,
            uint32_t    wildcardCount) noexcept
        {
            const Slot* s = MAR_ASSUME_ALIGNED(&slot, CACHELINE);
            const auto evType = static_cast<EventType>(s->type);
            if (MAR_UNLIKELY(evType == EventType::None)) return;
            DispatchPayload(
                const_cast<void*>(static_cast<const void*>(s->payload)),
                evType, s->category, s->total_size, wildcardCount);
        }

        // Reassembles a multi-slot event into m_AssemblyBuf before dispatching.
        // Two-slot fast path avoids the loop for the common large-event case.
        MAR_FORCEINLINE void* AssembleChunked(const Slot& head,
            uint32_t    si,
            uint8_t     tc,
            FastEventBuffer& buf) noexcept
        {
            const uint32_t total = head.total_size;

            // Fast path: head + exactly one continuation slot.
            if (MAR_LIKELY(tc == 2u)) {
                Detail::load_48(m_AssemblyBuf, head.payload);

                const Slot& cs = *MAR_ASSUME_ALIGNED(&buf.buffer[(si + 1u) & FastEventBuffer::MASK], CACHELINE);
                MAR_CORE_ASSERT(cs.type == CONTINUATION_MARKER,
                    "Dispatch: expected CONTINUATION_MARKER in chunked slot.");

                const uint32_t remaining = total - PAYLOAD_SIZE;
                const uint32_t this_chunk =
                    (remaining >= static_cast<uint32_t>(CONT_PAYLOAD_SIZE))
                    ? static_cast<uint32_t>(CONT_PAYLOAD_SIZE)
                    : remaining;

                if (MAR_LIKELY(this_chunk == static_cast<uint32_t>(CONT_PAYLOAD_SIZE)))
                    Detail::load_60(m_AssemblyBuf + PAYLOAD_SIZE, cs.cont_data());
                else
                    std::memcpy(m_AssemblyBuf + PAYLOAD_SIZE, cs.cont_data(), this_chunk);

                return m_AssemblyBuf;
            }

            // General path: three or more slots.
            Detail::load_48(m_AssemblyBuf, head.payload);
            uint32_t dst_off = PAYLOAD_SIZE;

            for (uint8_t ci = 1u; ci < tc; ++ci) {
                const Slot& cs = *MAR_ASSUME_ALIGNED(&buf.buffer[(si + ci) & FastEventBuffer::MASK], CACHELINE);
                MAR_CORE_ASSERT(cs.type == CONTINUATION_MARKER,
                    "Dispatch: expected CONTINUATION_MARKER in chunked slot.");

                const uint32_t remaining = total - dst_off;
                const uint32_t this_chunk =
                    (remaining >= static_cast<uint32_t>(CONT_PAYLOAD_SIZE))
                    ? static_cast<uint32_t>(CONT_PAYLOAD_SIZE)
                    : remaining;

                if (MAR_LIKELY(this_chunk == static_cast<uint32_t>(CONT_PAYLOAD_SIZE)))
                    Detail::load_60(m_AssemblyBuf + dst_off, cs.cont_data());
                else
                    std::memcpy(m_AssemblyBuf + dst_off, cs.cont_data(), this_chunk);

                dst_off += this_chunk;
            }

            return m_AssemblyBuf;
        }

        // Rebuilds the combined category mask from all active wildcard entries.
        // Called whenever a wildcard listener is added or removed.
        void UpdateGlobalWildcardMask() noexcept {
            m_GlobalWildcardMask = 0u;
            for (uint32_t i = 0u; i < m_WildcardCount; ++i)
                m_GlobalWildcardMask |= m_Wildcards[i].categoryMask;
        }

        // Inserts a wildcard entry in descending priority order using binary search.
        // The binary search handles the 0- and 1-element cases correctly without
        // any special-case code.
        MAR_COLD_FUNC void WildcardInsert(WildcardEntry&& entry) noexcept {
            uint32_t lo = 0u, hi = m_WildcardCount;
            while (lo < hi) {
                const uint32_t mid = (lo + hi) >> 1u;
                if (m_Wildcards[mid].priority >= entry.priority) lo = mid + 1u;
                else                                              hi = mid;
            }
            const uint32_t ins = lo;
            for (uint32_t i = m_WildcardCount; i > ins; --i)
                m_Wildcards[i] = std::move(m_Wildcards[i - 1u]);
            m_Wildcards[ins] = std::move(entry);
            ++m_WildcardCount;
        }

        // Returns the next unique subscription id.
        // Skips 0 on wrap since 0 is reserved for invalid tokens.
        [[nodiscard]] MAR_FORCEINLINE uint32_t AllocSubscriptionId() noexcept {
            const uint32_t id = m_NextSubscriptionId++;
            if (MAR_UNLIKELY(m_NextSubscriptionId == 0u))
                m_NextSubscriptionId = 1u;
            return id;
        }

        // Removes a listener immediately. If called during dispatch this should
        // not be used directly; Unsubscribe defers it instead.
        bool UnsubscribeImmediate(SubscriptionToken& token) noexcept {
            if (!token.Valid()) return false;
            bool removed = false;
            if (token.isWildcard) {
                for (uint32_t i = 0u; i < m_WildcardCount; ++i) {
                    if (m_Wildcards[i].id != token.id) continue;
                    for (uint32_t j = i; j + 1u < m_WildcardCount; ++j)
                        m_Wildcards[j] = std::move(m_Wildcards[j + 1u]);
                    --m_WildcardCount;
                    removed = true;
                    UpdateGlobalWildcardMask();
                    break;
                }
            }
            else {
                const size_t index = TypeToIndex(token.type);
                if (index < TotalEventTypes)
                    removed = m_Listeners[index].Remove(token.id);
            }
            if (removed) token = {};
            return removed;
        }

        // Processes all subscriptions and unsubscriptions that were deferred
        // because they arrived during a Dispatch call.
        MAR_COLD_FUNC void FlushDeferredSubscribes() noexcept {
            const uint32_t subCount = m_DeferredSubCount;
            m_DeferredSubCount = 0u;
            for (uint32_t i = 0u; i < subCount; ++i) {
                DeferredSubEntry& d = m_DeferredSubs[i];
                const bool ok = m_Listeners[d.bucketIndex].Insert(std::move(d.entry));
                MAR_CORE_ASSERT(ok, "MAX_LISTENERS_PER_EVENT reached while flushing deferred subscribes.");
                if (ok && d.isHandled)
                    m_Listeners[d.bucketIndex].hasHandledListeners = true;
            }
            const uint32_t wildCount = m_DeferredWildcardSubCount;
            m_DeferredWildcardSubCount = 0u;
            for (uint32_t i = 0u; i < wildCount; ++i) {
                MAR_CORE_ASSERT(m_WildcardCount < MAX_WILDCARD_LISTENERS,
                    "MAX_WILDCARD_LISTENERS reached while flushing wildcard subscribes.");
                if (MAR_LIKELY(m_WildcardCount < MAX_WILDCARD_LISTENERS))
                    WildcardInsert(std::move(m_DeferredWildcardSubs[i]));
            }
            if (wildCount > 0u)
                UpdateGlobalWildcardMask();
        }

        // Shared implementation for Subscribe and SubscribeHandled.
        // isHandled=true sets the high bit in the entry id so the bucket knows
        // at least one listener can stop event propagation.
        template<typename TEvent, typename F>
        [[nodiscard]] SubscriptionToken SubscribeImpl(F&& func,
            int  priority,
            bool isHandled)
        {
            const size_t   index = TypeToIndex(TEvent::GetStaticType());
            if (MAR_UNLIKELY(index >= TotalEventTypes)) return {};
            const uint32_t rawId = AllocSubscriptionId();

            ListenerEntry entry;
            entry.priority = priority;
            entry.id = isHandled ? (rawId | 0x80000000u) : rawId;
            entry.delegate = Delegate::Make(
                [fn = std::forward<F>(func)](void* raw) mutable noexcept {
                    fn(*static_cast<TEvent*>(raw));
                });

            // If called during dispatch, queue the subscription for later.
            if (MAR_UNLIKELY(m_DispatchDepth > 0u)) {
                if (MAR_UNLIKELY(m_DeferredSubCount >= MAX_DEFERRED)) {
                    MAR_CORE_ASSERT(false, "Deferred subscribe queue is full.");
                    return {};
                }
                DeferredSubEntry& d = m_DeferredSubs[m_DeferredSubCount++];
                d.bucketIndex = index;
                d.isHandled = isHandled;
                d.entry = std::move(entry);
                return SubscriptionToken{ TEvent::GetStaticType(), false, 0u, rawId };
            }

            const bool ok = m_Listeners[index].Insert(std::move(entry));
            MAR_CORE_ASSERT(ok, "MAX_LISTENERS_PER_EVENT reached.");
            if (ok && isHandled)
                m_Listeners[index].hasHandledListeners = true;
            return ok
                ? SubscriptionToken{ TEvent::GetStaticType(), false, 0u, rawId }
            : SubscriptionToken{};
        }

        // Data members.
        FastEventBuffer                     m_Buffer;
        std::array<Bucket, TotalEventTypes> m_Listeners{};

        // Scratch buffer used to reassemble multi-slot events before dispatch.
        alignas(CACHELINE) std::byte m_AssemblyBuf[MAX_LARGE_EVENT_BYTES]{};

        uint32_t m_NextSubscriptionId = 1u;

        WildcardEntry m_Wildcards[MAX_WILDCARD_LISTENERS]{};
        uint32_t      m_WildcardCount = 0u;
        uint32_t      m_GlobalWildcardMask = 0u;

        // Max deferred slots covers both typed and wildcard listeners.
        static constexpr uint32_t MAX_DEFERRED =
            static_cast<uint32_t>(MAX_LISTENERS_PER_EVENT + MAX_WILDCARD_LISTENERS);

        // Tracks re-entrant Dispatch calls and queued unsubscribes.
        uint32_t          m_DispatchDepth = 0u;
        uint32_t          m_DeferredCount = 0u;
        SubscriptionToken m_Deferred[MAX_DEFERRED]{};

        // Queued subscriptions that arrived while a dispatch was in progress.
        struct DeferredSubEntry {
            size_t        bucketIndex = 0u;
            bool          isHandled = false;
            uint8_t       _pad[7]{};
            ListenerEntry entry;
        };

        DeferredSubEntry m_DeferredSubs[MAX_DEFERRED]{};
        uint32_t         m_DeferredSubCount = 0u;

        WildcardEntry m_DeferredWildcardSubs[MAX_WILDCARD_LISTENERS]{};
        uint32_t      m_DeferredWildcardSubCount = 0u;
        static inline EventManager* s_Instance = nullptr;
    public:
        // Adicione ou modifique o construtor para registrar a instância ativa
        EventManager() noexcept {
            MAR_CORE_ASSERT(s_Instance == nullptr, "EventManager já foi instanciado em outro lugar!");
            s_Instance = this;
        }

        // Garanta que o ponteiro seja limpo quando o gerenciador for destruído
        ~EventManager() {
            if (s_Instance == this) {
                s_Instance = nullptr;
            }
        }

        // Método público estático para acessar o Singleton
        [[nodiscard]] static EventManager* GetInstance() noexcept {
            return s_Instance;
        }

        // Like GetInstance(), but fails loudly instead of returning nullptr.
        // Used by MAR_SUBSCRIBE so a missing/destroyed EventManager produces
        // a clear diagnostic (file/line) and a controlled abort instead of a
        // silent null-pointer dereference (segfault with zero context).
        //
        // This check is intentionally NOT behind MAR_CORE_ASSERT: that macro
        // compiles to nothing under NDEBUG, which is exactly the build where
        // this kind of initialization-order bug tends to surface for the
        // first time. Subscribe() is a cold path (setup/teardown, never the
        // per-frame Dispatch loop), so keeping this check unconditional costs
        // nothing where it matters.
        [[nodiscard]] static EventManager& RequireInstance(
            const char* file, int line) noexcept
        {
            if (MAR_UNLIKELY(s_Instance == nullptr)) {
                std::fprintf(stderr,
                    "Mar::EventManager::RequireInstance: no EventManager exists "
                    "(used before construction or after destruction) at %s:%d\n",
                    file, line);
                std::abort();
            }
            return *s_Instance;
        }

        [[nodiscard]] FastEventBuffer& GetBuffer()       noexcept { return m_Buffer; }
        [[nodiscard]] const FastEventBuffer& GetBuffer() const noexcept { return m_Buffer; }

        // Pushes a typed event onto the buffer.
        // Compile-time checks ensure the event type has both required macros.
        template<typename TEvent>
        MAR_FORCEINLINE bool Push(const TEvent& ev) noexcept {
            static_assert(std::is_trivially_copyable_v<TEvent>);
            static_assert(has_event_class_type_v<TEvent>,
                "TEvent is missing EVENT_CLASS_TYPE(...).");
            static_assert(has_event_class_category_v<TEvent>,
                "TEvent is missing EVENT_CLASS_CATEGORY(...).");
            return m_Buffer.Push(ev,
                TEvent::GetStaticType(),
                TEvent::GetStaticCategoryFlags());
        }

        template<typename T>
        [[nodiscard]] MAR_FORCEINLINE bool Push(const T& ev,
            EventType t,
            uint32_t  category) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>);
            static_assert(sizeof(T) <= MAX_LARGE_EVENT_BYTES);
            return m_Buffer.Push(ev, t, category);
        }

        // Drains all available slots and fires the matching listeners.
        // Re-entrancy is tracked by m_DispatchDepth. Any subscribes or
        // unsubscribes that happen inside a listener are deferred and applied
        // after the outermost Dispatch returns.
        void Dispatch() noexcept {
            const uint32_t wildcardCount = m_WildcardCount;
            auto& buf = m_Buffer;

            const uint32_t cur_r = buf.r.load(std::memory_order_relaxed);
            buf.cached_w = buf.w.load(std::memory_order_acquire);

            const uint32_t available =
                (buf.cached_w - cur_r) & FastEventBuffer::MASK;
            if (MAR_UNLIKELY(available == 0u)) return;

            uint32_t consumed = 0u;
            ++m_DispatchDepth;

            while (consumed < available) {
                const uint32_t si =
                    (cur_r + consumed) & FastEventBuffer::MASK;

                const Slot& s = *MAR_ASSUME_ALIGNED(&buf.buffer[si], CACHELINE);

                // Hint the next slot into L1 while we decode/dispatch this one.
                // Pure stride-1 access is normally caught by the hardware
                // prefetcher on its own, but the intervening touch of
                // m_Listeners[index] below (a completely different region)
                // can knock the ring buffer's stride out of the HW
                // prefetcher's tracking window between iterations — this
                // hint costs one instruction and re-asserts it explicitly.
                if (MAR_LIKELY(consumed + 1u < available)) {
                    const uint32_t next_si = (si + 1u) & FastEventBuffer::MASK;
                    MAR_PREFETCH_T0(&buf.buffer[next_si]);
                }

                const uint8_t tc = (s.type != 0u &&
                    s.type != CONTINUATION_MARKER)
                    ? s.total_chunks
                    : 1u;

                if (MAR_LIKELY(tc == 1u)) {
                    DispatchSlot(s, wildcardCount);
                    ++consumed;
                    continue;
                }

                // Skip the event if not all its slots have arrived yet.
                if (MAR_UNLIKELY(consumed + static_cast<uint32_t>(tc) > available))
                    break;

                MAR_CORE_ASSERT(s.total_size <= MAX_LARGE_EVENT_BYTES,
                    "Large event total_size exceeds MAX_LARGE_EVENT_BYTES.");
                if (MAR_UNLIKELY(s.total_size > MAX_LARGE_EVENT_BYTES)) {
                    consumed += tc;
                    continue;
                }

                void* assembled = AssembleChunked(s, si, tc, buf);
                DispatchPayload(
                    assembled,
                    static_cast<EventType>(s.type),
                    s.category,
                    s.total_size,
                    wildcardCount);

                consumed += tc;
            }

            --m_DispatchDepth;

            buf.r.store((cur_r + consumed) & FastEventBuffer::MASK,
                std::memory_order_release);

            // Flush deferred operations once we are fully out of all dispatch calls.
            if (m_DispatchDepth == 0u) {
                if (MAR_UNLIKELY(m_DeferredSubCount > 0u ||
                    m_DeferredWildcardSubCount > 0u))
                    FlushDeferredSubscribes();

                const uint32_t dcount = m_DeferredCount;
                m_DeferredCount = 0u;
                for (uint32_t i = 0u; i < dcount; ++i)
                    UnsubscribeImmediate(m_Deferred[i]);
            }
        }

        // Subscribes func to receive events of type TEvent.
        // The callback cannot mark the event as handled.
        template<typename TEvent, typename F>
        [[nodiscard]] SubscriptionToken Subscribe(F&& func, int priority = 0) {
            static_assert(std::is_trivially_copyable_v<TEvent>);
            static_assert(has_event_class_type_v<TEvent>);
            static_assert(has_event_class_category_v<TEvent>);
            static_assert(std::is_nothrow_invocable_v<std::decay_t<F>&, TEvent&>,
                "Subscribe callback must be noexcept.");
            return SubscribeImpl<TEvent>(std::forward<F>(func), priority, false);
        }

        // Same as Subscribe but the callback can set event.Handled = true to
        // stop delivery to lower-priority listeners.
        // Requires TEvent to derive from Event<TEvent> with Handled at offset 0.
        template<typename TEvent, typename F>
        [[nodiscard]] SubscriptionToken SubscribeHandled(F&& func, int priority = 0) {
            static_assert(std::is_trivially_copyable_v<TEvent>);
            static_assert(has_event_class_type_v<TEvent>);
            static_assert(has_event_class_category_v<TEvent>);
            static_assert(std::is_base_of_v<Event<TEvent>, TEvent>,
                "SubscribeHandled requires TEvent to derive from Event<TEvent>.");
            static_assert(offsetof(TEvent, Handled) == 0u,
                "bool Handled must be at byte offset 0.");
            static_assert(std::is_nothrow_invocable_v<std::decay_t<F>&, TEvent&>,
                "SubscribeHandled callback must be noexcept.");
            return SubscribeImpl<TEvent>(std::forward<F>(func), priority, true);
        }

        // Subscribes func to receive any event whose category flags overlap categoryMask.
        // The callback receives a type-erased EventView instead of a concrete event type.
        template<typename F>
        [[nodiscard]] SubscriptionToken SubscribeCategory(uint32_t categoryMask,
            F&& func,
            int      priority = 0)
        {
            static_assert(std::is_nothrow_invocable_v<std::decay_t<F>&, const EventView&>,
                "SubscribeCategory callback must be noexcept.");
            const uint32_t totalWildcards = m_WildcardCount + m_DeferredWildcardSubCount;
            if (MAR_UNLIKELY(totalWildcards >= MAX_WILDCARD_LISTENERS)) return {};
            const uint32_t rawId = AllocSubscriptionId();

            WildcardEntry entry;
            entry.categoryMask = categoryMask;
            entry.id = rawId;
            entry.priority = priority;
            entry.delegate = Delegate::Make(
                [fn = std::forward<F>(func)](void* raw) mutable noexcept {
                    fn(*static_cast<const EventView*>(raw));
                });

            if (MAR_UNLIKELY(m_DispatchDepth > 0u)) {
                m_DeferredWildcardSubs[m_DeferredWildcardSubCount++] = std::move(entry);
                return SubscriptionToken{ EventType::None, true, 0u, rawId };
            }
            WildcardInsert(std::move(entry));
            UpdateGlobalWildcardMask();
            return SubscriptionToken{ EventType::None, true, 0u, rawId };
        }

        // Removes the listener identified by token.
        // If called during dispatch, the removal is deferred until dispatch finishes.
        bool Unsubscribe(SubscriptionToken& token) noexcept {
            if (!token.Valid()) return false;
            if (MAR_UNLIKELY(m_DispatchDepth > 0u)) {
                MAR_CORE_ASSERT(m_DeferredCount < MAX_DEFERRED,
                    "Deferred unsubscribe queue is full.");
                if (MAR_LIKELY(m_DeferredCount < MAX_DEFERRED)) {
                    m_Deferred[m_DeferredCount++] = token;
                    token = {};
                    return true;
                }
                return false;
            }
            return UnsubscribeImmediate(token);
        }

        // Clears the buffer and all listeners. Must not be called from within Dispatch.
        void Reset() noexcept {
            MAR_CORE_ASSERT(m_DispatchDepth == 0u,
                "EventManager::Reset() called inside Dispatch().");
            m_Buffer.reset();
            for (auto& bucket : m_Listeners) {
                bucket.~Bucket();
                new (&bucket) Bucket();
            }
            for (uint32_t i = 0u; i < m_WildcardCount; ++i)
                m_Wildcards[i] = WildcardEntry{};
            m_WildcardCount = 0u;
            m_DeferredCount = 0u;
            m_DeferredSubCount = 0u;
            m_DeferredWildcardSubCount = 0u;
        }
    };

} // namespace Mar

// Convenção para assinar um evento sem precisar repetir o tipo dele e usando o Singleton global
#define MAR_SUBSCRIBE(EventType, ...) \
    (void)::Mar::EventManager::RequireInstance(__FILE__, __LINE__).Subscribe<EventType>([this](EventType& e) noexcept __VA_ARGS__)