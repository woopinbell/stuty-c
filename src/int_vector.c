#include "int_vector.h"
#include <stdlib.h> /* realloc, free */
#include <limits.h> /* SIZE_MAX */

// 불변식 위반 여부만 확인하는 헬퍼
static int int_vector_state_is_valid(const struct int_vector *v) {
    if (v->capacity == 0 && v->data != NULL)
        return 0;
    if (v->capacity > 0 && v->data == NULL)
        return 0;
    return v->size <= v->capacity;
}

enum int_vector_status int_vector_push(struct int_vector *v, int value) {
    size_t new_capacity;
    void *new_data;

    if (v == NULL)
        return INT_VECTOR_ERR_NULL_ARG;

    if (!int_vector_state_is_valid(v))
        return INT_VECTOR_ERR_RANGE;

    if (v->size < v->capacity) {
        // 여유 공간이 있으면 확장 없이 바로 삽입
        v->data[v->size] = value;
        v->size += 1;
        return INT_VECTOR_OK;
    }

    new_capacity = (v->capacity == 0) ? 4 : v->capacity * 2;

    // 원소 개수 오버플로: capacity 자체가 size_t 표현 범위를 넘는 경우
    if (v->capacity != 0 && new_capacity < v->capacity)
        return INT_VECTOR_ERR_OVERFLOW;
    
    new_data = v->allocator.alloc(v->allocator.ctx, v->data, new_capacity * sizeof(int));
    if (new_data == NULL)
        return INT_VECTOR_ERR_ALLOC;

    v->data = new_data;
    v->capacity = new_capacity;
    v->data[v->size] = value;
    v->size += 1;

    return INT_VECTOR_OK;
}

enum int_vector_status int_vector_get(const struct int_vector *v, size_t index, int *out_value) {
    if (v == NULL || out_value == NULL)
        return INT_VECTOR_ERR_NULL_ARG;

    if (!int_vector_state_is_valid(v))
        return INT_VECTOR_ERR_RANGE;

    if (index >= v->size)
        return INT_VECTOR_ERR_RANGE;

    *out_value = v->data[index];
    return INT_VECTOR_OK;
}

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
