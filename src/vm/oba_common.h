#ifndef oba_common_h
#define oba_common_h

#include <stdlib.h>

#ifdef DEBUG_MODE

// Assertions represent checks for bug in Oba's implementation.
//
// A failed assertion aborts execution immediately, so assertions should not be
// used to check for errors in the user-code being compiled.
//
// Assertions add significant overhead, so are only enabled in debug builds.
#define ASSERT(condition, message)                                             \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "[%s:%d] Assert failed in %s(): %s\n", __FILE__,         \
              __LINE__, __func__, message);                                    \
      abort();                                                                 \
    }                                                                          \
  } while (false)

#else

// clang-format off
#define ASSERT(condition, message) do {} while (false)
// clang-format on

#endif

#define GROW_CAPACITY(cap) ((cap) < 8 ? 8 : (cap)*2)

#define GROW_ARRAY(type, pointer, oldCount, newCount)                          \
  (type*)reallocate(pointer, sizeof(type) * (oldCount),                        \
                    sizeof(type) * (newCount))

#define FREE_ARRAY(type, pointer, oldCount)                                    \
  reallocate(pointer, sizeof(type) * oldCount, 0)

#define ALLOCATE(type, count) (type*)reallocate(NULL, 0, sizeof(type) * (count))

#define ALLOCATE_OBJ(vm, type, objectType)                                     \
  (type*)allocateObject(vm, sizeof(type), objectType)

#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)

// Reallocates [pointer] from [oldSize] to [newSize].
// If [newSize] is 0, [pointer] is freed.
void* reallocate(void* pointer, size_t oldSize, size_t newSize);

#endif
