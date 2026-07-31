#pragma once

// ── Prefetch hints ───────────────────────────────────────────────────────────
// Antes eram stubs ((void)0) — ou seja, todo MAR_PREFETCH_* no codebase
// (inclusive os que vamos adicionar no Dispatch loop do EventManager) não
// fazia absolutamente nada. Isso é infraestrutura compartilhada por todo o
// Mar (FastIO, EventManager, qualquer SPSC futuro), então vale a pena deixar
// real em vez de decorativo.
//
//   T0  = traz para todos os níveis de cache (vai usar "logo").
//   T1  = traz para L2 (vai usar "em breve", não imediatamente).
//   NTA = non-temporal — traz mas evita poluir o cache para dados que só
//         serão lidos uma vez (ex.: payload grande sendo só copiado/descartado).
#ifndef MAR_PREFETCH_T0
#   if defined(__GNUC__) || defined(__clang__)
#       define MAR_PREFETCH_T0(ptr)  __builtin_prefetch((ptr), 0, 3)
#       define MAR_PREFETCH_T1(ptr)  __builtin_prefetch((ptr), 0, 2)
#       define MAR_PREFETCH_NTA(ptr) __builtin_prefetch((ptr), 0, 0)
#   elif defined(_MSC_VER)
#       include <intrin.h>
#       define MAR_PREFETCH_T0(ptr)  _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T0)
#       define MAR_PREFETCH_T1(ptr)  _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T1)
#       define MAR_PREFETCH_NTA(ptr) ((void)0)
#   else
#       define MAR_PREFETCH_T0(ptr)  ((void)0)
#       define MAR_PREFETCH_T1(ptr)  ((void)0)
#       define MAR_PREFETCH_NTA(ptr) ((void)0)
#   endif
#endif

// ── Pause / yield para spin-wait ─────────────────────────────────────────────
// Para uso em loops de retry de quem chama FastEventBuffer::Push/Pull quando
// o buffer está cheio/vazio (o buffer em si nunca bloqueia — quem decide a
// política de retry é o chamador). Sem isso, um spin-wait apertado disputa
// portas de execução com a outra thread lógica do mesmo core físico.
#ifndef MAR_CPU_PAUSE
#   if defined(__GNUC__) || defined(__clang__)
#       if defined(__x86_64__) || defined(__i386__)
#           include <immintrin.h>
#           define MAR_CPU_PAUSE() _mm_pause()
#       elif defined(__aarch64__) || defined(__arm__)
#           define MAR_CPU_PAUSE() asm volatile("yield")
#       else
#           define MAR_CPU_PAUSE() ((void)0)
#       endif
#   elif defined(_MSC_VER)
#       include <intrin.h>
#       define MAR_CPU_PAUSE() _mm_pause()
#   else
#       define MAR_CPU_PAUSE() ((void)0)
#   endif
#endif