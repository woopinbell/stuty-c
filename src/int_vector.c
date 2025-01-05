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

enum int_vector_status int_vector_init(struct int_vector *v, const struct int_vector_allocator *allocator) {
    if (v == NULL)
        return INT_VECTOR_ERR_NULL_ARG;
    
    // allocator를 넘겼다면 alloc 함수 포인터는 필수, 없으면 후속 push에서 크래시
    if (allocator != NULL && allocator->alloc == NULL)
        return INT_VECTOR_ERR_NULL_ARG;

    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
    v->allocator = (allocator != NULL) ? *allocator : int_vector_default_allocator;

    return INT_VECTOR_OK;
}

void int_vector_destroy(struct int_vector *v) {
    if (v == NULL)
        return;
    
    if (v->data != NULL)
        v->allocator.alloc(v->allocator.ctx, v->data, 0);
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
}
