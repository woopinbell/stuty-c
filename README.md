# int-vector

![C](https://img.shields.io/badge/C-99%2B-blue?logo=c&logoColor=white)
![Build](https://img.shields.io/badge/build-make-lightgrey)

`int-vector`는 정수를 삽입 순서대로 저장하고 공간이 부족할 때 내부 배열을 확장하는 C 라이브러리입니다.

핵심 목표는 다음 두 가지입니다.

```text
정상 경로:
    원소를 삽입 순서대로 보존

할당 실패 경로:
    기존 배열과 기존 원소를 호출 전 상태 그대로 보존
```

## 제공 기능

- 기본 `realloc`/`free`와 사용자 정의 allocator callback
- 초기 capacity 4, 이후 두 배씩 증가
- 원소 개수와 바이트 수의 overflow 사전 검사
- 범위를 확인한 뒤에만 `out_value` 갱신
- 재할당 실패 뒤 `data`, `size`, `capacity`와 기존 원소 보존
- 반복 호출 가능한 정리 함수

allocator 인자에 기본 설정을 사용하면 일반 `realloc`과 `free`를 사용하고, 사용자 정의 callback을 전달하면 테스트에서 할당 성공/실패를 제어할 수 있습니다.

## 설치 및 빌드

```sh
make
```

정적 라이브러리는 `build/libint_vector.a`에 생성됩니다.

## 사용 예시

```c
#include "int_vector.h"

#include <stdio.h>

struct int_vector values;
int item;

int_vector_init(&values, NULL);

int_vector_push(&values, 10);
int_vector_push(&values, 20);

if (int_vector_get(&values, 1, &item) == 0) {
    printf("%d\n", item);
}

int_vector_destroy(&values);
```

두 번의 삽입 뒤 상태는 개념적으로 다음과 같습니다.

```text
data[0] = 10
data[1] = 20
size    = 2
capacity >= 2
```

`int_vector_get(&values, 1, &item)`은 두 번째 원소인 `20`을 `item`에 씁니다.

## 유지하는 상태

유효한 객체는 항상 다음 불변식을 만족합니다.

```text
size <= capacity

capacity == 0이면:
    data == NULL
    size == 0

capacity > 0이면:
    data != NULL

유효한 index:
    0 <= index < size
```

## 정리

`int_vector_destroy`는 내부 배열을 해제한 뒤 객체를 다시 빈 상태로 만듭니다. 같은 객체에 `destroy`를 반복 호출해도 안전합니다.

## 테스트

```sh
make test
make test-asan
```

다음 항목을 확인합니다.

- 빈 배열에서 첫 삽입
- 여러 번의 capacity 증가와 삽입 순서
- 첫 원소와 마지막 원소 조회
- 범위 밖 조회에서 출력값 보존
- 손상된 `size`/`capacity` 조합 거부
- 첫 할당과 다음 성장의 강제 실패
- 실패 후 포인터, 원소, `size`, `capacity` 보존
- 반복 정리

## 범위

다음 기능만 제공합니다.

```text
뒤쪽 삽입(push)
index 기반 조회(get)
```

다음 기능은 포함하지 않습니다.

- 중간 삽입, 삭제, capacity 축소
- iterator
- 여러 스레드의 동시 접근

여러 스레드가 같은 vector를 동시에 사용해야 한다면 호출자가 별도의 동기화 정책을 제공해야 합니다.