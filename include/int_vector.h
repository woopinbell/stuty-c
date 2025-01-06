#ifndef INT_VECTOR_H
#define INT_VECTOR_H

#include <stddef.h>

// alloc(ctx, NULL, size) : malloc(size)와 동일
// alloc(ctx, ptr,  size) : realloc(ptr, size)와 동일 (size > 0)
// alloc(ctx, ptr,  0)    : free(ptr)와 동일, 반환값 사용 안 함
typedef void *(*int_vector_alloc_fn)(void *ctx, void *ptr, size_t size);

struct int_vector_allocator {
    int_vector_alloc_fn alloc;
    void *ctx;
};

struct int_vector {
    int *data;
    size_t size;
    size_t capacity;
    struct int_vector_allocator allocator;
};

enum int_vector_status {
    INT_VECTOR_OK           = 0,
    INT_VECTOR_ERR_NULL_ARG = -1,
    INT_VECTOR_ERR_RANGE    = -2,
    INT_VECTOR_ERR_OVERFLOW = -3,
    INT_VECTOR_ERR_ALLOC    = -4
};

enum int_vector_status int_vector_init(struct int_vector *v, const struct int_vector_allocator *allocator);
enum int_vector_status int_vector_push(struct int_vector *v, int value);
enum int_vector_status int_vector_get(struct int_vector *v, size_t index, int *out_value);
void int_vector_destroy(struct int_vector *v);

extern const struct int_vector_allocator int_vector_default_allocator;

#endif // INT_VECTOR_H