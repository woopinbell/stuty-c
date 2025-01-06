#include "int_vector.h"
#include <assert.h>
#include <stdio.h>

static void test_init_default(void) {
    struct int_vector v;
    assert(int_vector_init(&v, NULL) == INT_VECTOR_OK);
    assert(v.data == NULL && v.size == 0 && v.capacity == 0);
    int_vector_destroy(&v);
}

static void test_push_and_get(void) {
    struct int_vector v;
    int out = 0;

    int_vector_init(&v, NULL);
    assert(int_vector_push(&v, 10) == INT_VECTOR_OK);
    assert(int_vector_push(&v, 20) == INT_VECTOR_OK);

    assert(int_vector_get(&v, 0, &out) == INT_VECTOR_OK && out == 10);
    assert(int_vector_get(&v, 1, &out) == INT_VECTOR_OK && out == 20);

    int_vector_destroy(&v);
}

static void test_capacity_growth(void) {
    struct int_vector v;
    int i;

    int_vector_init(&v, NULL);
    for (i = 0; i < 4; i++) {
        int_vector_push(&v, i);
    }
    assert(v.capacity == 4); // 초기 capacity

    int_vector_push(&v, 99); // 5번째 삽입 -> 확장 트리거
    assert(v.capacity == 8); // 2배 증가
    assert(v.size == 5);

    int_vector_destroy(&v);
}

static void test_get_out_of_range(void) {
    struct int_vector v;
    int out = -1;

    int_vector_init(&v, NULL);
    int_vector_push(&v, 42);

    assert(int_vector_get(&v, 5, &out) == INT_VECTOR_ERR_RANGE);
    assert(out == -1); // out_value 불변 확인

    int_vector_destroy(&v);
}

static void test_destroy_is_idempotent(void) {
    struct int_vector v;
    int_vector_init(&v, NULL);
    int_vector_push(&v, 1);
    int_vector_destroy(&v);
    int_vector_destroy(&v); // 두 번째 호출도 안전해야 함
}

int main(void) {
    test_init_default();
    test_push_and_get();
    test_capacity_growth();
    test_get_out_of_range();
    test_destroy_is_idempotent();
    printf("all tests passed\n");
    return 0;
}