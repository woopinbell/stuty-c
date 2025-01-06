#include "int_vector.h"
#include <stdlib.h>
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

// N번째 alloc 호출부터 실패를 흉내내는 테스트 전용 allocator
static int g_fail_after = -1;
static int g_call_count = 0;

static void *failing_alloc(void *ctx, void *ptr, size_t size) {
    (void)ctx;
    g_call_count++;
    if (g_fail_after >= 0 && g_call_count > g_fail_after) {
        return NULL; // size==0(free) 케이스는 실무에서 실패시키지 않는 게 정석이라 그대로 통과
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

static void test_alloc_failure_preserves_state(void) {
    struct int_vector v;
    struct int_vector_allocator alloc = { .alloc = failing_alloc, .ctx = NULL };
    int out = 0;

    g_call_count = 0;
    g_fail_after = 1; // 첫 확장(capacity 0->4)만 성공, 두 번째부터 실패

    int_vector_init(&v, &alloc);
    for (int i = 0; i < 4; i++) {
        assert(int_vector_push(&v, i) == INT_VECTOR_OK);
    }

    // 5번째 push는 재할당이 필요하지만 alloc이 NULL을 반환
    assert(int_vector_push(&v, 99) == INT_VECTOR_ERR_ALLOC);
    assert(v.size == 4 && v.capacity == 4); // 기존 상태 그대로

    // 기존 원소는 여전히 멀쩡해야 한다
    assert(int_vector_get(&v, 0, &out) == INT_VECTOR_OK && out == 0);

    int_vector_destroy(&v);
    g_fail_after = -1;
}

int main(void) {
    test_init_default();
    test_push_and_get();
    test_capacity_growth();
    test_get_out_of_range();
    test_destroy_is_idempotent();
    test_alloc_failure_preserves_state();
    printf("all tests passed\n");
    return 0;
}