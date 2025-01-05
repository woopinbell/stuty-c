#include "int_vector.h"
#include <stdlib.h> /* realloc, free */

// 표준 realloc/free 시맨틱을 alloc_fn 계약에 맞게 감싼 기본 구현.
// ctx는 사용하지 않으므로 (void)로 무시한다.
static void *int_vector_default_alloc(void *ctx, void *ptr, size_t size) {
    (void)ctx;
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

const struct int_vector_allocator int_vector_default_allocator = {
    .alloc = int_vector_default_alloc,
    .ctx   = NULL
};