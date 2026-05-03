#pragma once

#include "Mararuana/Core.h"
#include "Mararuana/Events/EventBuffer.h"

#include <new>
#include <array>
#include <numeric>

namespace Mar {


    // Delegate - small callable wrapper with SBO
    class Delegate {
    public:
        static constexpr size_t SBO_SIZE = 32u;

        using InvokeFn = void (*)(void* storage, void* event) noexcept;
        using DestroyFn = void (*)(void* storage)              noexcept;
        using RelocateFn = void (*)(void* dst, void* src)   noexcept;

        alignas(8) std::byte _storage[SBO_SIZE]{};
        InvokeFn   _invoke = nullptr;
        DestroyFn  _destroy = nullptr;
        RelocateFn _relocate = nullptr;

        Delegate() = default;

        ~Delegate() {
            if (_destroy) _destroy(_storage);
        }

        Delegate(const Delegate&) = delete;
        Delegate& operator=(const Delegate&) = delete;

        Delegate(Delegate&& o) noexcept
            : _invoke(o._invoke), _destroy(o._destroy), _relocate(o._relocate)
        {
            if (_relocate) _relocate(_storage, o._storage);
            else           std::memcpy(_storage, o._storage, SBO_SIZE);
            o._invoke = nullptr; o._destroy = nullptr; o._relocate = nullptr;
        }

        Delegate& operator=(Delegate&& o) noexcept {
            if (this != &o) {
                if (_destroy) _destroy(_storage);
                _invoke = o._invoke;
                _destroy = o._destroy;
                _relocate = o._relocate;
                if (_relocate) _relocate(_storage, o._storage);
                else           std::memcpy(_storage, o._storage, SBO_SIZE);
                o._invoke = nullptr; o._destroy = nullptr; o._relocate = nullptr;
            }
            return *this;
        }

        template<typename F>
        [[nodiscard]] static Delegate Make(F&& f) noexcept {
            using FD = std::decay_t<F>;
            static_assert(sizeof(FD) <= SBO_SIZE,
                "Functor exceeds Delegate::SBO_SIZE (32 B).");
            static_assert(alignof(FD) <= 8u,
                "Functor alignment exceeds SBO limit.");
            static_assert(std::is_nothrow_constructible_v<FD, F&&>,
                "Functor must be nothrow constructible from F&&.");
            static_assert(std::is_nothrow_move_constructible_v<FD>,
                "Functor must be nothrow move constructible.");

            Delegate d;
            new (d._storage) FD(std::forward<F>(f));
            d._invoke = [](void* s, void* ev) noexcept {
                (*static_cast<FD*>(s))(ev);
                };
            if constexpr (!std::is_trivially_destructible_v<FD>)
                d._destroy = [](void* s) noexcept { static_cast<FD*>(s)->~FD(); };
            if constexpr (!std::is_trivially_copyable_v<FD>)
                d._relocate = [](void* dst, void* src) noexcept {
                new (dst) FD(std::move(*static_cast<FD*>(src)));
                static_cast<FD*>(src)->~FD();
                };
            return d;
        }

        MAR_FORCEINLINE void Invoke(void* ev) noexcept {
            MAR_CORE_ASSERT(_invoke != nullptr, "Delegate::Invoke called on empty delegate.");
            _invoke(_storage, ev);
        }

        [[nodiscard]] bool Valid() const noexcept { return _invoke != nullptr; }
    };



    struct SubscriptionToken {
        EventType type = EventType::None;
        bool      isWildcard = false;
        uint8_t   _pad = 0u;
        uint32_t  id = 0u;

        [[nodiscard]] bool Valid() const noexcept { return id != 0u; }
    };

    struct EventView {
        EventType   type;
        uint32_t    category;
        const void* payload;
        uint32_t    size;

        template<typename T>
        [[nodiscard]] const T& As() const noexcept {
            return *static_cast<const T*>(payload);
        }
    };

    inline constexpr size_t MAX_LISTENERS_PER_EVENT = 16u;

    struct ListenerEntry {
        int      priority = 0;
        uint32_t id = 0u;
        Delegate delegate;

        [[nodiscard]] bool     IsHandled() const noexcept { return (id >> 31u) != 0u; }
        [[nodiscard]] uint32_t RawId()     const noexcept { return id & 0x7FFFFFFFu; }
    };
    static_assert(sizeof(ListenerEntry) == 64u,
        "ListenerEntry size changed - recheck bucket layout.");

    // Bucket - listener container for one event type.
    //
    // Listeners are stored in descending priority order.
    // Dispatch walks them from highest to lowest.
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

        // Insert in descending priority.
        // Back to front scan keeps common inserts cheap.
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

    // Wildcard support

    inline constexpr size_t MAX_WILDCARD_LISTENERS = 16u;

    struct WildcardEntry {
        uint32_t categoryMask = 0u;
        uint32_t id = 0u;
        int      priority = 0;
        uint8_t  _pad[4]{};
        Delegate delegate;
    };

    // EventManager
    class EventManager {
    private:
        // Type to index map
        [[nodiscard]] static constexpr size_t TypeToIndex(EventType t) noexcept {
            constexpr auto table = []() constexpr {
                std::array<size_t, static_cast<size_t>(EventType::EndTypeValue)> tbl{};
                for (size_t i = 0; i < static_cast<size_t>(EventType::EndTypeValue); ++i)
                    tbl[i] = (i == 0u) ? TotalEventTypes : i - 1u;
                return tbl;
                }();
            return table[static_cast<size_t>(t)];
        }

        // Dispatch payload
        // Runs wildcards first, then type listeners.
        // Single listener fast path avoids loop overhead.
        MAR_FORCEINLINE void DispatchPayload(void* ev,
            EventType evType,
            uint32_t  category,
            uint32_t  totalSize,
            uint32_t  wildcardCount) noexcept
        {
            if (MAR_UNLIKELY(evType == EventType::None)) return;
            const size_t index = TypeToIndex(evType);
            if (MAR_UNLIKELY(index >= TotalEventTypes)) return;

            // Wildcards
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

            // Fast path for one listener.
            if (MAR_LIKELY(n == 1u)) {
                p[0u].delegate.Invoke(ev);
                return;
            }

            // Handled listeners can stop the dispatch early.
            if (MAR_UNLIKELY(bucket.hasHandledListeners)) {
                for (uint32_t i = 0u; i < n; ++i) {
                    p[i].delegate.Invoke(ev);
                    if (MAR_UNLIKELY(*static_cast<const bool*>(ev))) return;
                }
                return;
            }

            for (uint32_t i = 0u; i < n; ++i)
                p[i].delegate.Invoke(ev);
        }

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

        // Assemble chunked event into m_AssemblyBuf
        MAR_FORCEINLINE void* AssembleChunked(const Slot& head,
            uint32_t    si,
            uint8_t     tc,
            FastEventBuffer& buf) noexcept
        {
            const uint32_t total = head.total_size;

            // Two slot case
            if (MAR_LIKELY(tc == 2u)) {
                Detail::load_48(m_AssemblyBuf, head.payload);

                const Slot& cs = buf.buffer[(si + 1u) & FastEventBuffer::MASK];
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

            // General case
            Detail::load_48(m_AssemblyBuf, head.payload);
            uint32_t dst_off = PAYLOAD_SIZE;

            for (uint8_t ci = 1u; ci < tc; ++ci) {
                const Slot& cs = buf.buffer[(si + ci) & FastEventBuffer::MASK];
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

        // Wildcard helpers

        void UpdateGlobalWildcardMask() noexcept {
            m_GlobalWildcardMask = 0u;
            for (uint32_t i = 0u; i < m_WildcardCount; ++i)
                m_GlobalWildcardMask |= m_Wildcards[i].categoryMask;
        }

        // Insert wildcard by descending priority.
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

        // Subscription ID

        [[nodiscard]] MAR_FORCEINLINE uint32_t AllocSubscriptionId() noexcept {
            const uint32_t id = m_NextSubscriptionId++;
            // 0 is reserved for invalid tokens.
            if (MAR_UNLIKELY(m_NextSubscriptionId == 0u))
                m_NextSubscriptionId = 1u;
            return id;
        }

        // Unsubscribe

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

        MAR_COLD_FUNC void FlushDeferredSubscribes() noexcept {
            const uint32_t subCount = m_DeferredSubCount;
            m_DeferredSubCount = 0u;
            for (uint32_t i = 0u; i < subCount; ++i) {
                DeferredSubEntry& d = m_DeferredSubs[i];
                const bool ok = m_Listeners[d.bucketIndex].Insert(std::move(d.entry));
                MAR_CORE_ASSERT(ok, "MAX_LISTENERS_PER_EVENT reached during deferred subscribe flush.");
                if (ok && d.isHandled)
                    m_Listeners[d.bucketIndex].hasHandledListeners = true;
            }
            const uint32_t wildCount = m_DeferredWildcardSubCount;
            m_DeferredWildcardSubCount = 0u;
            for (uint32_t i = 0u; i < wildCount; ++i) {
                MAR_CORE_ASSERT(m_WildcardCount < MAX_WILDCARD_LISTENERS,
                    "MAX_WILDCARD_LISTENERS reached during wildcard subscribe flush.");
                if (MAR_LIKELY(m_WildcardCount < MAX_WILDCARD_LISTENERS))
                    WildcardInsert(std::move(m_DeferredWildcardSubs[i]));
            }
            if (wildCount > 0u)
                UpdateGlobalWildcardMask();
        }

        // SubscribeImpl - shared subscribe logic
        //
        // isHandled = true  -> bit 31 is set in the entry id.
        // isHandled = false -> normal subscribe.
        template<typename TEvent, typename F>
        [[nodiscard]] SubscriptionToken SubscribeImpl(F&& func,
            int      priority,
            bool     isHandled)
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

            if (MAR_UNLIKELY(m_DispatchDepth > 0u)) {
                if (MAR_UNLIKELY(m_DeferredSubCount >= MAX_DEFERRED)) {
                    MAR_CORE_ASSERT(false, "Deferred subscribe queue full.");
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

        // Data members
        FastEventBuffer                     m_Buffer;
        std::array<Bucket, TotalEventTypes> m_Listeners{};

        alignas(CACHELINE) std::byte m_AssemblyBuf[MAX_LARGE_EVENT_BYTES]{};

        uint32_t m_NextSubscriptionId = 1u;

        WildcardEntry m_Wildcards[MAX_WILDCARD_LISTENERS]{};
        uint32_t      m_WildcardCount = 0u;
        uint32_t      m_GlobalWildcardMask = 0u;

        static constexpr uint32_t MAX_DEFERRED =
            static_cast<uint32_t>(MAX_LISTENERS_PER_EVENT + MAX_WILDCARD_LISTENERS);

        uint32_t          m_DispatchDepth = 0u;
        uint32_t          m_DeferredCount = 0u;
        SubscriptionToken m_Deferred[MAX_DEFERRED]{};

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

    public:
        // Buffer access
        [[nodiscard]] FastEventBuffer& GetBuffer()       noexcept { return m_Buffer; }
        [[nodiscard]] const FastEventBuffer& GetBuffer() const noexcept { return m_Buffer; }

        // Push

        template<typename TEvent>
        [[nodiscard]] MAR_FORCEINLINE bool Push(const TEvent& ev) noexcept {
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

        // Dispatch
        // Drains the buffer and runs listeners.
        // Reentrancy is controlled by m_DispatchDepth.
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

                const uint8_t tc = (s.type != 0u &&
                    s.type != CONTINUATION_MARKER)
                    ? s.total_chunks
                    : 1u;

                if (MAR_LIKELY(tc == 1u)) {
                    DispatchSlot(s, wildcardCount);
                    ++consumed;
                    continue;
                }

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

        // Subscribe
        // Registers func for event type TEvent.
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

        // SubscribeHandled
        // Registers func and allows it to stop later listeners.
        template<typename TEvent, typename F>
        [[nodiscard]] SubscriptionToken SubscribeHandled(F&& func, int priority = 0) {
            static_assert(std::is_trivially_copyable_v<TEvent>);
            static_assert(has_event_class_type_v<TEvent>);
            static_assert(has_event_class_category_v<TEvent>);
            static_assert(std::is_base_of_v<Event<TEvent>, TEvent>,
                "SubscribeHandled requires TEvent deriving from Event<TEvent>.");
            static_assert(offsetof(TEvent, Handled) == 0u,
                "bool Handled must be at byte offset 0.");
            static_assert(std::is_nothrow_invocable_v<std::decay_t<F>&, TEvent&>,
                "SubscribeHandled callback must be noexcept.");
            return SubscribeImpl<TEvent>(std::forward<F>(func), priority, true);
        }

        // SubscribeCategory
        // Registers func for any event whose category matches categoryMask.
        // The callback receives an EventView.
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

        // Unsubscribe
        bool Unsubscribe(SubscriptionToken& token) noexcept {
            if (!token.Valid()) return false;
            if (MAR_UNLIKELY(m_DispatchDepth > 0u)) {
                MAR_CORE_ASSERT(m_DeferredCount < MAX_DEFERRED,
                    "Deferred unsubscribe queue full.");
                if (MAR_LIKELY(m_DeferredCount < MAX_DEFERRED)) {
                    m_Deferred[m_DeferredCount++] = token;
                    token = {};
                    return true;
                }
                return false;
            }
            return UnsubscribeImmediate(token);
        }

        // Reset
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

#define MAR_SUBSCRIBE(mgr, EventType, ...) \
    (void)(mgr).Subscribe<EventType>([this](EventType& e) noexcept __VA_ARGS__)