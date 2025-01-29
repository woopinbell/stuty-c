# 스레드, 동기화, 시간

## 스레드 수명

스레드는 `pthread_create`로 시작해서 함수가 반환하거나 `pthread_exit`를 호출하는 순간 끝난다. 하지만 스레드가 "끝났다"는 것과 그 자원이 시스템에서 회수되는 것은 별개다. 스레드가 종료돼도 다른 누군가 `pthread_join`으로 결과를 거둬가기 전까지는 커널이 종료 상태(exit status)와 스택 일부를 계속 붙들고 있다. 이걸 좀비 스레드라고 부르기도 한다.

이 구분이 중요한 이유는, 스레드를 계속 만들기만 하고 join도 detach도 하지 않으면 실제로는 이미 죽은 스레드인데도 리소스가 누적돼 결국 `pthread_create`가 `EAGAIN`으로 실패하기 시작하기 때문이다. 스레드를 만들 때는 항상 "이 스레드를 누가, 언제 거둬갈 것인가"를 먼저 정해야 한다.

```c
pthread_t worker;
int rc = pthread_create(&worker, NULL, worker_main, &args);
if (rc != 0) {
    fprintf(stderr, "pthread_create 실패: %s\n", strerror(rc));
    return -1;
}
```

흔한 실수는 스레드 함수에 넘긴 인자(`&args`)가 스택 지역 변수인데, 그 변수가 스코프를 벗어나 소멸된 뒤에도 새 스레드가 계속 그걸 읽는 경우다. 인자는 힙에 두거나, 넘기는 쪽이 스레드가 인자를 다 읽었다는 신호를 받을 때까지 살아있게 보장해야 한다.

## join과 detach

모든 joinable 스레드는 반드시 `pthread_join` 또는 `pthread_detach` 둘 중 하나로 마무리해야 한다. `join`은 호출한 스레드를 블록시키고 대상 스레드가 끝날 때까지 기다렸다가 종료값을 받아온다. `detach`는 "이 스레드가 끝나면 자원을 즉시 알아서 반환해라, 누구도 결과를 기다리지 않는다"고 커널에 알리는 것이다.

```c
void *result;
if (pthread_join(worker, &result) != 0) {
    perror("pthread_join");
}
```

```c
pthread_detach(worker); /* 이후 이 handle로 join하면 정의되지 않은 동작 */
```

한번 detach한 스레드는 절대 join할 수 없고, 한번 join한 스레드를 다시 join하는 것도 정의되지 않은 동작이다. 어느 쪽을 쓸지는 "그 스레드의 완료 여부나 결과값이 프로그램 로직에 필요한가"로 정하면 된다. 필요하면 join, 순수하게 백그라운드로 던지고 잊어도 되면 detach다. 프로그램이 `main`을 빠져나가기 전에 아직 join도 detach도 안 된 스레드가 남아있으면 그 자체가 버그 신호로 봐야 한다.

## mutex 수명

`pthread_mutex_t`는 사용하기 전에 `pthread_mutex_init`(또는 정적 초기화자 `PTHREAD_MUTEX_INITIALIZER`)으로 초기화해야 하고, 다 쓴 뒤에는 `pthread_mutex_destroy`로 정리해야 한다. mutex의 수명은 반드시 그것이 보호하는 데이터의 수명을 완전히 감싸야 한다. 데이터가 아직 살아있는데 mutex를 먼저 파괴하거나, mutex를 파괴한 뒤에도 누군가 그걸 lock하려 시도하면 정의되지 않은 동작이다.

```c
typedef struct {
    pthread_mutex_t lock;
    int balance;
} account_t;

void account_init(account_t *a, int initial) {
    pthread_mutex_init(&a->lock, NULL);
    a->balance = initial;
}

void account_destroy(account_t *a) {
    pthread_mutex_destroy(&a->lock); /* 이 시점에 아무도 lock 중이면 안 된다 */
}
```

특히 위험한 패턴은 mutex를 스택에 할당한 구조체 안에 두고, 그 함수가 리턴한 뒤에도 다른 스레드가 그 mutex를 참조하도록 넘기는 경우다. mutex와 데이터는 같은 수명 관리 단위(같은 구조체, 같은 힙 할당) 안에 두는 게 원칙이다.

## mutex가 보호하는 상태를 명시하기

mutex 하나를 잠근다는 것 자체는 아무 의미가 없다. mutex는 "이 mutex를 잡고 있는 동안에만 접근해도 되는 변수들의 집합"과 짝을 이룰 때만 의미가 있다. 코드를 읽는 사람(미래의 자신 포함)이 어떤 변수가 어떤 mutex로 보호되는지 알 수 없으면, 그 mutex는 사실상 장식에 불과하다.

관례적으로는 구조체 정의에 주석으로 "이 필드는 `lock`이 보호한다"를 명시하거나, 아예 보호받는 필드들을 별도의 내부 구조체로 묶어서 이름 자체로 드러낸다.

```c
typedef struct {
    pthread_mutex_t lock;
    /* 아래 필드들은 lock을 잡은 상태에서만 읽고 쓴다 */
    int queue_len;
    struct item *head;
} queue_t;
```

흔한 실수는 구조체 안에 mutex로 보호되는 필드와 보호되지 않는(스레드가 하나뿐일 때만 쓰이거나, 불변인) 필드가 섞여 있는데 구분이 안 돼 있어서, 나중에 코드를 수정하는 사람이 보호되지 않는 필드도 lock 없이 여러 스레드에서 건드리기 시작하는 경우다.

## 무엇을 하나의 임계 구역으로 묶을지

임계 구역(critical section)은 "한 번에 한 스레드만 실행해야 하는 코드 범위"다. 이 범위를 정할 때 기준은 코드 줄 수가 아니라 **불변식(invariant)이 일시적으로 깨졌다가 다시 성립하는 구간 전체**다. 예를 들어 큐에서 값을 꺼내면서 길이 카운터를 감소시키는 두 연산은, 둘 사이에 다른 스레드가 끼어들면 길이와 실제 원소 수가 어긋나는 순간이 생기므로 반드시 같은 임계 구역에 있어야 한다.

```c
pthread_mutex_lock(&q->lock);
item = q->head;
q->head = item->next;
q->queue_len--;             /* head 갱신과 이 감소는 분리하면 안 된다 */
pthread_mutex_unlock(&q->lock);
```

반대로 임계 구역을 필요 이상으로 넓게 잡으면(예를 들어 락을 잡은 채로 디스크 I/O나 로그 출력을 하면) 다른 스레드들이 불필요하게 오래 대기하게 된다. 원칙은 "불변식이 깨지는 최소 구간"만 감싸는 것이고, 락을 잡은 상태에서 블로킹 콜(I/O, sleep, 다른 락 대기)을 하는 건 최대한 피한다.

## 잠금 순서와 교착 상태

스레드 A가 락 1을 잡고 락 2를 기다리는 동안, 스레드 B가 락 2를 잡고 락 1을 기다리면 둘 다 영원히 멈춘다. 이게 교착 상태(deadlock)다. 두 개 이상의 락을 동시에 잡아야 하는 코드에서는 모든 스레드가 **항상 같은 순서**로 락을 획득하도록 강제하면 이 형태의 순환 대기를 원천 차단할 수 있다.

순서를 정하는 방법은 보통 두 가지다: (1) 락이 속한 객체에 고유하고 비교 가능한 ID(주소, 계좌번호 등)를 부여하고 그 값으로 정렬해서 항상 작은 쪽부터 잠근다, (2) 애초에 여러 락을 동시에 두 개 이상 들고 있는 코드를 최소화한다.

```c
account_t *first  = (a->id < b->id) ? a : b;
account_t *second = (a->id < b->id) ? b : a;

pthread_mutex_lock(&first->lock);
pthread_mutex_lock(&second->lock);
/* ... */
pthread_mutex_unlock(&second->lock);
pthread_mutex_unlock(&first->lock);
```

`pthread_mutex_trylock`으로 "잡을 수 있으면 잡고, 안 되면 이미 잡은 락을 다 풀고 재시도"하는 방식도 있지만, 이건 순서를 정하기 어려운 동적인 상황(락 집합이 실행 중에 결정되는 경우)에서 쓰는 차선책이고, 가능하면 정적인 순서 규칙을 우선한다.

## 같은 객체를 두 번 받은 경우

두 개의 핸들(포인터, ID)을 받아서 각각 락을 잡는 함수는, 그 두 핸들이 실제로는 같은 객체를 가리키는 경우를 별도로 처리해야 한다. 같은 `pthread_mutex_t`를 재귀적으로(re-entrant하지 않게) 두 번 lock하면 대부분의 구현에서 그 자리에서 자기 자신과 교착 상태에 빠진다.

```c
void transfer(account_t *from, account_t *to, int amount) {
    if (from == to) {
        return; /* 자기 자신에게 이체 - 아무 일도 안 해도 됨 */
    }
    /* ... 순서 정해서 잠그기 ... */
}
```

이 검사를 빼먹기 쉬운 이유는 "같은 계좌로 이체"가 논리적으로는 무의미한 입력처럼 보이기 때문이다. 하지만 사용자 입력이나 상위 계층의 버그로 `from`과 `to`가 같은 값으로 들어오는 건 충분히 일어날 수 있는 일이고, 이걸 별도 분기로 처리하지 않으면 락 순서 정렬 로직(`id`가 같으니 `first == second`)까지 무너뜨릴 수 있다.

## 같은 ID를 가진 서로 다른 객체

반대의 위험도 있다: 포인터 값은 다른데 논리적 ID(계좌번호 등)가 우연히 같은 두 객체를 서로 다른 것으로 착각하고 각각 락을 잡는 경우다. 이런 상황은 보통 캐시나 조회 테이블에 중복 항목이 생겼을 때, 또는 ID 생성 로직에 버그가 있을 때 나타난다.

락 순서를 ID로 정렬하는 코드(`a->id < b->id`)는 두 객체가 서로 다른 메모리 주소를 가진 것을 전제로 한다. 만약 시스템 어딘가에 같은 ID를 가진 별개의 `account_t` 인스턴스가 두 개 존재한다면, 그건 애초에 "계좌 하나 = 객체 하나"라는 불변식이 깨진 것이고 락 순서를 아무리 잘 짜도 두 인스턴스가 서로 다른 메모리를 봐서 데이터가 갈라지는 문제는 해결되지 않는다.

이 문제의 근본 대응은 락 자체가 아니라 "ID당 객체 인스턴스는 정확히 하나"를 보장하는 조회/생성 계층(팩토리, 캐시)에 있다. 스레드 동기화 코드를 작성할 때는 이 전제가 실제로 보장되는지 반드시 확인하고 시작해야 한다.

## 실패 시 잠금 해제

락을 잡은 뒤 실행하는 코드 중간에서 에러가 나서 함수를 일찍 빠져나가야 하는 경우, 그 모든 탈출 경로에서 락을 풀어야 한다. C에는 예외가 없으므로 이건 스코프를 벗어나면 자동으로 해제되는 장치(C++의 RAII 같은 것)가 없고, `goto`로 정리 구간에 모으는 패턴이 표준적인 대응이다.

```c
int do_work(account_t *a) {
    int rc = -1;
    pthread_mutex_lock(&a->lock);

    if (a->balance <= 0) {
        goto out; /* 락을 잡은 채로 그냥 return하면 안 됨 */
    }
    if (do_something(a) != 0) {
        goto out;
    }
    rc = 0;

out:
    pthread_mutex_unlock(&a->lock);
    return rc;
}
```

이 패턴을 빼먹으면 발생하는 버그는 즉시 죽지 않고, 그 특정 에러 경로가 실행될 때만 락이 영원히 잠긴 채로 남아 이후 그 락을 기다리는 모든 스레드가 멈춘다. 재현이 어렵고 원인 파악에 오래 걸리는 종류의 버그다. 함수에 조건 분기가 여러 개일수록 락을 잡은 뒤의 모든 경로를 표로 만들어 확인해보는 습관이 필요하다.

## 이체 예시

지금까지의 원칙(순서 정하기, 자기 자신 처리, 실패 시 해제)을 하나로 합친 계좌 이체 함수는 다음과 같은 모양이 된다.

```c
int transfer(account_t *from, account_t *to, int amount) {
    if (from == to) {
        return 0;
    }

    account_t *first  = (from->id < to->id) ? from : to;
    account_t *second = (from->id < to->id) ? to : from;

    pthread_mutex_lock(&first->lock);
    pthread_mutex_lock(&second->lock);

    int rc = -1;
    if (from->balance < amount) {
        goto out; /* 잔액 부족 */
    }
    from->balance -= amount;
    to->balance   += amount;
    rc = 0;

out:
    pthread_mutex_unlock(&second->lock);
    pthread_mutex_unlock(&first->lock);
    return rc;
}
```

여기서 잔액 검사(`from->balance < amount`)를 락을 잡기 전에 미리 해두면 안 된다는 점이 중요하다. 검사와 실제 차감 사이에 다른 스레드가 끼어들 수 있는 구간이 생기기 때문이다(다음 항목에서 더 다룬다). 두 락을 다 잡은 뒤에 검사하고, 검사와 갱신이 같은 임계 구역 안에 있어야 한다.

## 일관된 한 시점의 값

여러 필드를 함께 읽어서 판단을 내려야 하는 코드는, 그 필드들을 "같은 순간의 값들"로 읽어야 한다. 락을 잡지 않고 필드 A를 읽은 다음 필드 B를 읽으면, 그 사이에 다른 스레드가 두 필드를 모두 갱신해버려서 A는 갱신 전 값, B는 갱신 후 값이라는 있을 수 없는 조합을 관찰하게 될 수 있다.

```c
/* 잘못됨: balance와 last_updated가 같은 순간의 값이라는 보장이 없음 */
int b = a->balance;
time_t t = a->last_updated;

/* 올바름: 같은 임계 구역 안에서 함께 읽는다 */
pthread_mutex_lock(&a->lock);
int b = a->balance;
time_t t = a->last_updated;
pthread_mutex_unlock(&a->lock);
```

이 문제는 필드 하나만 읽을 때는 잘 안 보이다가, "잔액이 음수가 아니면서 동시에 최근 갱신 시각이 유효해야 한다"처럼 두 값의 **관계**에 의존하는 로직이 생기는 순간 드러난다. 여러 필드를 조합해서 판단하는 코드를 볼 때마다 "이 값들이 정말 같은 락 구간 안에서 나온 게 맞는가"를 확인하는 습관이 필요하다.

## 출력 매개변수 갱신 시점

함수가 포인터로 결과를 돌려주는(out-parameter) 형태일 때, 그 포인터가 가리키는 메모리를 언제 갱신하느냐도 임계 구역의 경계에 들어간다. 함수가 아직 실패할 수도 있는 중간 단계에서 출력 매개변수를 먼저 써버리면, 실패한 호출인데도 호출자가 부분적으로 갱신된 값을 보게 될 수 있다.

```c
int get_balance(account_t *a, int *out) {
    pthread_mutex_lock(&a->lock);
    if (a->closed) {
        pthread_mutex_unlock(&a->lock);
        return -1; /* out은 건드리지 않는다 */
    }
    *out = a->balance; /* 성공이 확정된 뒤에만 쓴다 */
    pthread_mutex_unlock(&a->lock);
    return 0;
}
```

원칙은 "함수가 성공을 반환하는 경우에만 출력 매개변수가 유효한 값을 갖는다"를 지키는 것이고, 이 값 쓰기는 락을 여전히 잡고 있는 상태에서(읽은 값이 다른 스레드에 의해 바뀌기 전에) 이루어져야 한다.

## 조건 변수

mutex만으로는 "조건이 만족될 때까지 기다렸다가 깨어나기"를 효율적으로 표현할 수 없다. 락을 계속 잡았다 풀었다 하면서 조건을 반복 확인(busy-wait)하면 CPU를 낭비한다. `pthread_cond_t`는 스레드를 실제로 재우고, 다른 스레드가 조건이 바뀌었다고 신호를 보낼 때만 깨우는 장치다. 조건 변수는 항상 그 조건이 참조하는 상태를 보호하는 mutex와 함께 쌍으로 사용한다.

```c
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cond = PTHREAD_COND_INITIALIZER;
int ready = 0;
```

`pthread_cond_wait`은 내부적으로 (1) 넘겨받은 mutex를 원자적으로 풀고 (2) 신호를 기다리다가 (3) 깨어나면 그 mutex를 다시 잠근 뒤 리턴하는 동작을 한다. 이 "풀기와 기다리기 사이에 다른 스레드가 끼어들 틈이 없어야 한다"는 원자성이 조건 변수의 핵심이며, 이걸 직접 mutex unlock 다음에 별도로 wait을 호출하는 식으로 흉내 내면 그 사이 틈에서 신호를 놓치는 경합이 생긴다.

## 조건 변수와 predicate

조건 변수 자체는 "무엇을" 기다리는지 모른다. 그냥 신호가 오면 깨어날 뿐이다. 실제로 기다리는 조건(predicate)은 mutex로 보호되는 별도의 변수(`ready`, `queue_len > 0` 등)로 표현하고, `cond_wait`은 항상 그 predicate를 검사하는 반복문과 함께 쓴다.

```c
pthread_mutex_lock(&lock);
while (!ready) {
    pthread_cond_wait(&cond, &lock);
}
/* 여기 도달하면 ready == 1이 lock을 잡은 상태에서 보장됨 */
pthread_mutex_unlock(&lock);
```

predicate 없이 조건 변수만으로 "누가 신호를 보냈으니 일이 끝났다"고 가정하면 안 된다. 신호와 실제로 기다리던 상태 변화가 항상 일대일로 대응한다는 보장이 없기 때문이다(다음 항목에서 이어서 다룬다).

## 왜 `while`로 다시 검사하는가

`pthread_cond_wait`에서 깨어났다고 해서 predicate가 반드시 참이 된 것은 아니다. 두 가지 이유가 있다: (1) **허위 기상(spurious wakeup)**, 표준이 구현체에게 아무 신호 없이도 깨울 수 있는 여지를 허용한다. (2) **가로채기**, 신호를 보낸 스레드가 원했던 조건이 참이 됐더라도, 깨어난 여러 스레드 중 자신보다 먼저 락을 잡은 다른 스레드가 그 사이에 조건을 다시 거짓으로 만들어버릴 수 있다(예: 큐에서 마지막 항목을 다른 스레드가 먼저 가져감).

`if (!ready) wait();`처럼 한 번만 검사하면 이 두 경우 모두에서 거짓 조건을 참으로 착각하고 진행하게 된다. `while (!ready) wait();`는 깨어날 때마다 다시 원점에서 검사하므로, 예외 없이 항상 "wait에서 리턴한 시점에는 predicate가 실제로 참"이라는 결과를 보장한다.

## `signal`과 `broadcast`

`pthread_cond_signal`은 대기 중인 스레드 중 (구현에 따라 임의로 선택된) 하나만 깨우고, `pthread_cond_broadcast`는 대기 중인 스레드 전부를 깨운다. 어느 쪽을 쓸지는 "조건이 바뀌었을 때 그걸로 만족할 수 있는 대기자가 정확히 한 명뿐인가, 여러 명일 수 있는가"로 정한다.

큐에서 항목 하나를 꺼내는 대기자들처럼 "깨어난 스레드 중 하나만 실제로 일을 할 수 있고 나머지는 다시 잠들어야 하는" 상황에서는 `signal`로 충분하고 비용도 더 싸다. 반면 "상태 하나가 바뀌었는데 그걸 기다리던 스레드가 여러 종류의 서로 다른 조건으로 대기 중이라 누가 깨어나야 할지 신호를 보내는 쪽에서 알 수 없는" 경우(예: 여러 종류의 predicate가 같은 조건 변수를 공유), 또는 "shutdown처럼 모두가 동시에 깨어나야 하는" 경우에는 `broadcast`를 쓴다.

`signal`을 썼는데 실제로는 여러 대기자가 서로 다른 조건을 기다리고 있었다면, 조건이 맞지 않는 스레드가 깨어나고 정작 깨어나야 할 스레드는 계속 잠들어 있는 상태(lost wakeup과 비슷한 결과)가 될 수 있다. 확신이 없으면 `broadcast`가 더 안전한 기본값이고, `while` predicate 검사가 제대로 돼 있다면 broadcast로 인한 불필요한 재검사 비용은 감내할 만하다.

## 조건 변수 수명

mutex와 마찬가지로 `pthread_cond_t`도 사용 전 초기화, 사용 후 `pthread_cond_destroy`가 필요하고, destroy 시점에 그 조건 변수에 대해 대기 중인 스레드가 하나도 없어야 한다. 대기자가 남아있는 채로 조건 변수를 파괴하면 정의되지 않은 동작이다.

이 문제는 특히 "소유자 객체를 없애는 타이밍"과 얽혀서 나타난다. 예를 들어 작업 큐 객체를 파괴하려는데 워커 스레드가 아직 그 큐의 조건 변수에서 대기 중이면, 파괴하기 전에 반드시 모든 워커에게 broadcast로 깨어나라고 신호를 보내고, 워커들이 실제로 대기 상태를 빠져나왔다는 걸 확인(예: join)한 뒤에 destroy를 호출해야 한다.

## 시작 시점 맞추기

메인 스레드가 워커 스레드를 만들고 나서 바로 어떤 공유 상태에 의존하는 작업을 시작하면, 워커가 아직 초기화를 끝내지 않았을 수 있다는 문제가 생긴다. "스레드를 만들었다"는 사실 자체는 그 스레드가 어디까지 실행됐는지에 대해 아무것도 보장하지 않는다.

이걸 해결하는 표준적인 방법이 조건 변수다: 워커가 초기화를 끝내면 `ready` 같은 플래그를 세우고 신호를 보내고, 메인 스레드는 그 플래그가 설 때까지 기다린다.

```c
pthread_mutex_lock(&lock);
while (!worker_ready) {
    pthread_cond_wait(&cond, &lock);
}
pthread_mutex_unlock(&lock);
/* 이제 워커의 초기화 결과에 의존해도 안전하다 */
```

동시에 여러 스레드를 만들고 전부가 준비될 때까지 기다려야 하는 경우에는 카운터(몇 명이 준비됐는지)와 predicate를 함께 쓰거나, POSIX 배리어(`pthread_barrier_t`)를 쓰는 게 더 적합하다.

## 시간 측정

시간을 다루는 함수는 크게 두 종류의 목적을 가진다: "지금이 몇 시인가"를 아는 것과 "얼마나 시간이 지났는가"를 재는 것이다. 이 둘을 같은 시계로 재면 안 되는 경우가 있다는 게 시간 관련 버그의 큰 원인이다.

`CLOCK_REALTIME`은 실제 벽시계 시각이고, 시스템 관리자나 NTP 동기화에 의해 갑자기 앞뒤로 튈 수 있다. `CLOCK_MONOTONIC`은 임의의 기준점부터 단조 증가만 하는 시계로, 시스템 시각 조정의 영향을 받지 않는다. 경과 시간(타임아웃, 성능 측정)을 잴 때는 `CLOCK_MONOTONIC`을 쓰고, 실제 달력상의 시각이 필요할 때만 `CLOCK_REALTIME`을 쓴다.

```c
struct timespec ts;
clock_gettime(CLOCK_MONOTONIC, &ts);
```

`CLOCK_REALTIME`으로 "지금부터 5초 뒤"를 계산해 타임아웃으로 쓰면, 그 사이 시스템 시각이 앞으로 조정될 경우 실제로는 5초가 지나지 않았는데도 타임아웃이 발생하거나, 뒤로 조정되면 5초가 훨씬 지나도 타임아웃이 안 걸리는 문제가 생길 수 있다.

## `timespec` 계산

`struct timespec`은 초(`tv_sec`)와 나노초(`tv_nsec`, 0 이상 999,999,999 이하)로 시각이나 시간 간격을 표현한다. 여기에 시간을 더하거나 뺄 때 나노초 필드가 범위를 벗어나면 반드시 초 단위로 올림/내림(정규화)해줘야 한다. 안 그러면 이후 이 값을 비교하거나 커널에 넘기는 함수들이 잘못된 결과를 낸다.

```c
static void timespec_add_ms(struct timespec *ts, long ms) {
    ts->tv_sec  += ms / 1000;
    ts->tv_nsec += (ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_nsec -= 1000000000L;
        ts->tv_sec  += 1;
    }
}
```

뺄셈에서는 반대 방향의 정규화(나노초가 음수가 되면 초에서 빌려오기)가 필요하다. 이 정규화를 빼먹은 코드는 보통 평소에는 잘 동작하다가 나노초 필드가 우연히 경계값 근처일 때만 간헐적으로 오동작해서, 원인 추적이 오래 걸리는 버그가 된다.

## 조건 변수 timeout과 clock

`pthread_cond_wait`에는 타임아웃이 없지만, `pthread_cond_timedwait`은 "이 절대 시각까지만 기다리고, 그때까지 신호가 없으면 `ETIMEDOUT`을 반환하며 리턴"하는 버전이다. 여기서 넘기는 `timespec`은 상대 시간(몇 초 뒤)이 아니라 **절대 시각**이라는 점이 실수하기 쉬운 부분이다.

```c
struct timespec deadline;
clock_gettime(CLOCK_MONOTONIC, &deadline);
timespec_add_ms(&deadline, 500); /* 지금부터 500ms 뒤 */

pthread_mutex_lock(&lock);
int rc = 0;
while (!ready && rc == 0) {
    rc = pthread_cond_timedwait(&cond, &lock, &deadline);
}
if (rc == ETIMEDOUT) {
    /* 타임아웃 - ready는 여전히 0일 수 있다 */
}
pthread_mutex_unlock(&lock);
```

기본적으로 조건 변수는 `CLOCK_REALTIME` 기준으로 만들어지므로, `CLOCK_MONOTONIC` 기준의 `deadline`을 넘기려면 `pthread_condattr_setclock`으로 조건 변수를 생성할 때 명시적으로 `CLOCK_MONOTONIC`을 지정해야 한다. 이 설정 없이 monotonic 시계로 만든 deadline을 realtime 조건 변수에 넘기면, 두 시계 기준이 어긋나서 타임아웃이 원하는 시점보다 훨씬 일찍 걸리거나 영원히 안 걸릴 수 있다.

## Sleep 정확도

`sleep`, `usleep`, `nanosleep` 같은 함수가 "정확히 N만큼 쉰다"는 보장은 생각보다 약하다. 이 함수들이 보장하는 건 "최소 N만큼은 쉰다"는 하한선이지 상한선이 아니다. 실제로 깨어나는 시점은 스케줄러가 그 시점에 이 스레드를 얼마나 빨리 다시 실행시켜주느냐에 달려 있고, 시스템이 바쁘면 요청한 시간보다 상당히 더 오래 지나서 깨어날 수 있다.

이 때문에 "정확히 100ms마다 한 번씩" 같은 주기적 작업을 `sleep(100ms)`을 반복 호출하는 식으로 구현하면, 매 반복마다 스케줄링 지연이 누적돼 실제 주기가 점점 벌어진다. 정확한 주기가 필요하면 매번 "다음 절대 목표 시각 = 이전 목표 시각 + 주기"를 계산해서 그 절대 시각까지 자는 방식(절대 deadline 방식, 다음 항목에서 다룬다)을 써야 누적 오차가 없다.

## `nanosleep`과 `EINTR`

`nanosleep`이 자는 도중 시그널을 받으면, 남은 시간을 다 채우지 못하고 `-1`을 반환하며 `errno`를 `EINTR`로 설정한 채 조기 리턴한다. 이때 두 번째 인자로 남은 시간을 채워주므로, 정말 요청한 시간 전체를 채워서 자야 하는 코드라면 이 남은 시간으로 다시 `nanosleep`을 호출하는 재시도 루프가 필요하다.

```c
int sleep_full(long ms) {
    struct timespec req = { .tv_sec = ms / 1000,
                             .tv_nsec = (ms % 1000) * 1000000L };
    struct timespec rem;
    while (nanosleep(&req, &rem) == -1) {
        if (errno != EINTR) {
            return -1; /* 진짜 에러 */
        }
        req = rem; /* 남은 시간만큼 다시 잔다 */
    }
    return 0;
}
```

이 재시도 루프를 빼먹으면, 시그널을 자주 받는 프로세스(예: 타이머나 자식 프로세스 종료 알림을 쓰는 서버)에서 `sleep`이 예상보다 훨씬 짧게 끝나는 현상이 간헐적으로 나타난다.

## 절대 deadline 사용하기

주기적인 작업이나 전체 작업 시간에 제한이 있는 로직은, "다음에 얼마나 잘 것인가"를 상대 시간으로 반복 계산하지 말고 처음에 절대 종료 시각(deadline)을 한 번 정해두고 그 시각까지 남은 시간을 매번 다시 계산하는 방식을 쓴다.

```c
struct timespec deadline;
clock_gettime(CLOCK_MONOTONIC, &deadline);
timespec_add_ms(&deadline, 5000); /* 전체 작업은 5초 안에 끝나야 함 */

while (work_remaining()) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > deadline.tv_sec ||
        (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
        return -1; /* 전체 데드라인 초과 */
    }
    do_one_step();
}
```

이 방식이 "매번 남은 시간을 새로 500ms로 잡고 자기"와 다른 점은, 중간에 스케줄링 지연이나 재시도(`EINTR` 등)로 시간을 좀 더 썼더라도 다음 대기 시간이 자동으로 그만큼 줄어든다는 것이다. 상대 시간을 매번 고정값으로 다시 재는 방식은 그 지연들이 계속 누적되어 전체 데드라인을 넘기기 쉽다.

## timeout 테스트

타임아웃 로직은 "정상적으로 제 시간 안에 끝나는 경우"만 테스트하면 절반만 검증한 것이다. 최소한 다음 세 가지 경로를 각각 재현해서 확인해야 한다: (1) 조건이 타임아웃 전에 충족되는 정상 경로, (2) 조건이 끝까지 충족되지 않아 `ETIMEDOUT`으로 빠지는 경로, (3) 타임아웃 직전에 아슬아슬하게 신호가 오는 경계 경로.

이걸 재현 가능하게 테스트하려면 실제 벽시계 시간에 의존하는 대신, 테스트 환경에서만 짧은 타임아웃 값(수 밀리초)을 쓰거나, 시간을 모킹할 수 있는 구조(시계 조회 함수를 함수 포인터로 주입)로 짜두는 게 좋다. 특히 (2)번 경로(실제로 `ETIMEDOUT`이 반환됐을 때 호출자가 그 반환값을 제대로 확인하고 처리하는지)는 정상 경로 테스트만으로는 절대 드러나지 않는다.

## 데이터 경합과 논리 오류

데이터 경합(data race)은 "동기화 없이 같은 메모리를 두 스레드 이상이 동시에 접근하는데 그중 하나 이상이 쓰기인 경우"를 가리키는 좁은 기술적 정의이고, 이건 C 표준상 정의되지 않은 동작이다. 반면 논리 오류는 락을 다 제대로 잡았는데도(즉 데이터 경합은 없는데도) 임계 구역을 잘못 나눠서 프로그램이 틀린 결과를 내는 경우다. 예를 들어 잔액 검사와 차감을 서로 다른 두 개의 임계 구역으로 나눠놓으면, 데이터 경합은 없지만 두 임계 구역 사이에 다른 이체가 끼어들어 잔액이 음수가 될 수 있다.

이 구분이 중요한 이유는 도구가 다르기 때문이다. 데이터 경합은 ThreadSanitizer 같은 도구로 비교적 잘 잡히지만, 논리 오류는 락을 다 정상적으로 잡고 있으므로 그런 도구가 아무 문제도 보고하지 않는다. 이건 설계 리뷰와 불변식 검토로만 잡아낼 수 있다.

## 불변식 테스트

동시성 코드의 불변식(예: "모든 이체가 끝난 뒤 전체 계좌 잔액의 합은 변하지 않는다", "큐 길이는 항상 0 이상이며 실제 원소 수와 같다")은, 그 자체를 테스트 코드로 명시적으로 검증하는 게 개별 함수 단위 테스트보다 동시성 버그를 훨씬 잘 잡아낸다.

전형적인 패턴은: 여러 스레드를 동시에 띄워 같은 공유 객체에 대해 무작위 순서로 연산(이체, enqueue/dequeue 등)을 반복시키고, 모든 스레드가 끝난 뒤 불변식이 여전히 성립하는지 확인하는 것이다. 이런 테스트는 스레드 수, 반복 횟수, 실행 환경에 따라 실패가 재현될 확률이 달라지므로(특정 타이밍에서만 드러나는 경합은 어쩌다 한 번만 실패할 수 있다) 같은 테스트를 여러 번, 가능하면 CI에서 스레드 수를 다르게 바꿔가며 반복 실행하는 게 중요하다.

## 초기화되지 않은 객체

`pthread_mutex_lock`이나 `pthread_cond_wait`을 초기화되지 않은(또는 이미 destroy된) 객체에 대해 호출하면 정의되지 않은 동작이다. 정적으로 할당된 mutex/조건 변수는 `PTHREAD_MUTEX_INITIALIZER`/`PTHREAD_COND_INITIALIZER`로 선언과 동시에 초기화되므로 비교적 안전하지만, 동적으로 할당한 구조체 안에 들어있는 mutex는 `malloc` 직후 별도로 `pthread_mutex_init`을 호출하기 전까지는 쓰레기 값이 들어있는 상태다.

```c
account_t *a = malloc(sizeof(account_t));
/* 여기서 바로 pthread_mutex_lock(&a->lock)을 부르면 미정의 동작 */
pthread_mutex_init(&a->lock, NULL);
```

이 문제는 생성자 역할을 하는 `_init` 함수를 반드시 통해서만 객체를 만들도록 강제하고, `malloc` 결과를 직접 여기저기서 사용하지 않게 인터페이스를 설계하면 예방할 수 있다. `calloc`으로 0으로 채운다고 해서 안전해지는 것도 아니다. 0으로 채워진 `pthread_mutex_t`가 유효한 초기 상태라는 보장은 구현체에 따라 다르므로, 항상 명시적으로 `_init`을 호출해야 한다.

## 테스트할 내용

이 장에서 다룬 내용을 실제로 검증하려면 최소한 다음을 코드로 확인해봐야 한다:

- `pthread_join`으로 워커 스레드의 반환값이 실제로 넘긴 인자에 대응하는 결과인지
- `transfer(a, a, amount)`: 같은 계좌로의 이체가 교착 상태 없이 즉시 리턴하는지
- 여러 스레드가 동시에 무작위 순서로 이체를 반복한 뒤, 전체 계좌 잔액의 합이 시작 시점과 동일한지
- `pthread_cond_timedwait`이 조건이 절대 충족되지 않는 상황에서 정확히 지정한 시간 근처에 `ETIMEDOUT`으로 리턴하는지 (너무 빠르지도, 너무 늦지도 않게)
- `nanosleep`을 시그널로 중단시켰을 때, 재시도 루프가 있는 버전과 없는 버전의 실제 총 대기 시간 차이
- ThreadSanitizer(`-fsanitize=thread`)로 빌드했을 때 지금까지의 모든 테스트가 데이터 경합 없이 통과하는지