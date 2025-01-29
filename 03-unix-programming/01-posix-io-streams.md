# POSIX 입출력과 스트림 상태

## 파일 디스크립터

파일 디스크립터(file descriptor, fd)는 커널이 프로세스마다 유지하는 "열린 파일 테이블"의 인덱스일 뿐인 정수다. 파일 이름이나 경로가 아니라 이 정수 하나로 열린 파일, 파이프, 소켓, 디바이스를 구분한다. 프로세스가 시작되면 관례적으로 0(`stdin`), 1(`stdout`), 2(`stderr`)이 이미 열려 있고, 그 이후 `open`, `socket`, `pipe` 등이 반환하는 fd는 현재 비어 있는 가장 작은 정수부터 순서대로 배정된다.

왜 굳이 정수로 다루는가 하면, 실제 파일 오프셋, 플래그, inode 참조 같은 상태는 전부 커널 쪽 fd 테이블에 있고 프로세스는 그 테이블의 인덱스만 들고 있기 때문이다. 이 덕분에 `dup`/`dup2`로 같은 열린 파일을 가리키는 새 fd를 만들거나, `fork` 후 자식 프로세스가 같은 fd 번호로 같은 파일을 계속 쓸 수 있다.

```c
int fd = open("input.log", O_RDONLY);
if (fd == -1) {
    perror("open");
    exit(1);
}
// 사용 ...
close(fd);
```

흔한 실수는 두 가지다. 하나는 `open`이 실패해서 반환한 `-1`을 확인하지 않고 그대로 `read(fd, ...)`에 넘기는 것, `-1`은 유효한 fd가 아니라 "실패"라는 값이다. 다른 하나는 다 쓴 fd를 `close`하지 않아 프로세스가 오래 살아있는 동안 fd 테이블이 고갈되는 것(fd leak)이다.

## `read`

```c
ssize_t read(int fd, void *buf, size_t count);
```

반환값의 의미를 정확히 알아야 한다. 양수면 실제로 읽은 바이트 수, `0`이면 더 이상 읽을 데이터가 없다는 뜻(EOF), `-1`이면 에러이고 `errno`에 원인이 담긴다.

가장 자주 오해하는 지점은 "`count`를 요청하면 `count`만큼 읽힌다"는 가정이다. 실제로는 그보다 적게 읽고 반환하는 게 정상 동작이다. 파이프나 소켓처럼 상대편이 아직 그만큼 안 보냈을 수도 있고, 시그널에 의해 중단됐을 수도 있고, 파일 끝에 가까워 남은 바이트가 `count`보다 적을 수도 있다.

```c
char buf[4096];
ssize_t n = read(fd, buf, sizeof(buf));
if (n < 0) {
    perror("read");
} else if (n == 0) {
    // EOF
} else {
    // buf[0..n) 만 유효하다. buf[n]부터는 이전 내용이거나 쓰레기값.
}
```

## `write`

```c
ssize_t write(int fd, const void *buf, size_t count);
```

`read`와 대칭으로, `write`도 `count`바이트를 전부 쓴다는 보장이 없다. 소켓 송신 버퍼가 꽉 찼거나 파이프 반대편이 아직 안 읽었으면 일부만 쓰고 반환한다(짧은 쓰기, short write). `read`와 마찬가지로 `-1`이면 `errno`를 확인해야 한다.

```c
const char *msg = "hello";
size_t total = strlen(msg);
size_t sent = 0;
while (sent < total) {
    ssize_t n = write(fd, msg + sent, total - sent);
    if (n < 0) {
        if (errno == EINTR) continue;
        perror("write");
        break;
    }
    sent += (size_t)n;
}
```

흔한 실수는 `write(fd, msg, total)`의 반환값을 버리고 "다 보냈겠지"라고 가정하는 것이다. 특히 대용량 데이터를 소켓에 쓸 때는 위처럼 반환값을 누적해서 전체를 다 보낼 때까지 반복하는 루프가 필수다.

## `EINTR`

블로킹 상태의 `read`나 `write` 도중 프로세스에 시그널이 도착하면, 시그널 핸들러가 실행된 뒤 시스템 콜은 진행 중이던 작업을 완료하지 못한 채 `-1`을 반환하고 `errno`를 `EINTR`로 설정할 수 있다. 이건 진짜 에러가 아니라 "다시 시도해도 된다"는 신호다.

```c
ssize_t n;
do {
    n = read(fd, buf, sizeof(buf));
} while (n < 0 && errno == EINTR);

if (n < 0) {
    perror("read"); // EINTR이 아닌 진짜 에러
}
```

`sigaction`을 등록할 때 `SA_RESTART` 플래그를 주면 일부 시스템 콜은 커널이 자동으로 재시작해주지만, 모든 시스템 콜에 적용되는 건 아니고(`select`, `poll` 등은 플랫폼에 따라 다르게 동작한다) 이식성 있는 코드라면 `EINTR`을 직접 체크하고 재시도하는 루프를 두는 편이 안전하다.

## 바이트 수와 문자열 길이 구분하기

`read`는 바이트 스트림을 다루지 문자열을 다루지 않는다. 즉 읽어온 데이터 끝에 `'\0'`을 붙여주지 않는다. `read`가 반환한 `n`은 유효한 바이트의 개수이지, 그 안에 NUL 종료 문자열이 들어있다는 보장이 아니다.

```c
char buf[64];
ssize_t n = read(fd, buf, sizeof(buf));
if (n > 0) {
    printf("%.*s", (int)n, buf); // 안전: 정확히 n바이트만 출력
    // printf("%s", buf);  <- 위험: buf에 NUL이 없으면 버퍼 밖까지 읽는다
}
```

바이너리 데이터나 아직 파싱하지 않은 프로토콜 데이터에는 애초에 `strlen`을 쓸 수 없다. 데이터 중간에 `0x00` 바이트가 정상적으로 섞여 있을 수 있기 때문이다. 길이는 항상 `read`가 반환한 `n`(혹은 그걸 누적한 값)으로 추적해야 한다.

## 레코드와 `read` 호출은 일치하지 않습니다

애플리케이션이 다루는 논리적 단위(레코드), 예를 들어 한 줄의 텍스트, 길이-접두(length-prefixed) 메시지 하나는 `read` 시스템 콜 한 번의 경계와 아무 관계가 없다. 특히 소켓은 스트림 지향이라 커널이 "여기까지가 보낸 쪽의 한 메시지"라는 경계를 보존해주지 않는다.

```
보낸 쪽: write("AB\n") 두 번
받는 쪽: read() 한 번에 "AB\nAB\n" 전부 들어올 수도 있고
         read() 두 번에 "AB\n"와 "AB\n"로 나뉘어 올 수도 있고
         read() 세 번에 "AB", "\nAB", "\n"으로 잘려서 올 수도 있다
```

그래서 줄 단위 프로토콜을 파싱하려면 "한 줄이 완성될 때까지 여러 번 `read`해야 할 수도 있고, 한 번의 `read`에 여러 줄이 들어올 수도 있다"는 전제로 코드를 짜야 한다. 이 문제를 다루는 방법이 바로 다음 절의 "상태를 보관하는 읽기 객체"다.

## 상태를 보관하는 읽기 객체

`read` 호출 경계와 레코드 경계가 안 맞기 때문에, 이전 `read`에서 남은 미완성 데이터를 다음 `read` 결과와 이어붙여 파싱할 상태가 어딘가에 보관되어야 한다. 이 상태를 캡슐화한 것이 "읽기 객체"다.

```c
#define LR_BUF_SIZE 4096

typedef struct {
    int fd;
    char buf[LR_BUF_SIZE];
    size_t len;      // buf에 쌓여 있는, 아직 소비하지 않은 바이트 수
    int eof;         // fd에서 EOF를 이미 봤는지
} line_reader;

void line_reader_init(line_reader *lr, int fd) {
    lr->fd = fd;
    lr->len = 0;
    lr->eof = 0;
}
```

`buf`와 `len`이 이 객체의 핵심이다. 한 번의 `read`가 레코드 경계에서 딱 끝나지 않으면, 그 잘린 조각은 버려지지 않고 `buf`에 남아 다음 호출의 파싱 대상이 된다.

## 상태를 보관하는 읽기 객체의 처리 순서

읽기 객체를 굴리는 루프는 항상 같은 순서를 따른다.

1. **버퍼에 새 데이터 채우기**: 공간이 남아 있으면 `read(fd, buf + len, capacity - len)`으로 이어서 채운다.
2. **파싱 시도**: 지금까지 쌓인 `buf[0..len)` 안에서 완성된 레코드(예: `\n`)가 있는지 찾는다.
3. **완성된 레코드 꺼내기(pop)**: 찾았으면 그 구간을 호출자에게 반환하고, 남은 잔여 바이트를 버퍼 앞으로 당긴다(`memmove`).
4. **없으면 반복**: 완성된 레코드가 없고 EOF도 아니면 1번으로 돌아가 더 읽는다.

```c
// 완성된 줄이 있으면 포인터/길이를 채우고 1, 없으면 0
int line_reader_next(line_reader *lr, const char **line, size_t *line_len) {
    for (;;) {
        char *nl = memchr(lr->buf, '\n', lr->len);
        if (nl) {
            size_t n = (size_t)(nl - lr->buf);
            *line = lr->buf;
            *line_len = n; // '\n' 제외
            size_t consumed = n + 1;
            memmove(lr->buf, lr->buf + consumed, lr->len - consumed);
            lr->len -= consumed;
            return 1;
        }
        if (lr->eof) {
            if (lr->len > 0) {
                *line = lr->buf;
                *line_len = lr->len;
                lr->len = 0;
                return 1; // 종결자 없는 마지막 조각
            }
            return 0;
        }
        if (lr->len == LR_BUF_SIZE) {
            // 버퍼가 꽉 찼는데 개행이 없다. 비정상적으로 긴 레코드
            return -1;
        }
        ssize_t n = read(lr->fd, lr->buf + lr->len, LR_BUF_SIZE - lr->len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) { lr->eof = 1; continue; }
        lr->len += (size_t)n;
    }
}
```

## 빈 레코드와 마지막 조각

두 가지 경계 사례를 명시적으로 처리해야 한다.

**빈 레코드**: 구분자가 연속으로 오면(`"a\n\nb\n"`) 길이 0짜리 레코드가 생긴다. 위 `line_reader_next`는 `n == 0`인 경우도 정상적으로 `*line_len = 0`으로 반환한다. 이걸 "레코드가 없다"와 혼동해서 건너뛰면 빈 줄을 조용히 삼켜버리는 버그가 된다.

**마지막 조각**: 입력이 구분자로 끝나지 않고 EOF로 끝나는 경우(`"a\nb"`처럼 마지막 `b` 뒤에 `\n`이 없는 경우)다. 이 마지막 조각을 버릴지, 하나의 레코드로 취급할지는 프로토콜 스펙에 달려 있지만, 최소한 "조용히 버리는" 선택을 무의식중에 하면 안 된다. 위 구현은 `lr->eof && lr->len > 0`일 때 종결자 없이도 남은 걸 레코드로 반환하도록 명시적으로 처리했다.

## 출력 메모리 소유권

`line_reader_next`가 돌려주는 `*line` 포인터는 `line_reader`의 내부 버퍼 `buf`를 직접 가리킨다. 복사본이 아니다. 이건 성능상 이점이 있지만 대가가 있다: 호출자가 그 포인터를 오래 들고 있는 동안 `line_reader_next`를 다시 호출하면(`memmove`가 버퍼 내용을 옮기므로) 이전에 받은 포인터가 가리키는 내용이 바뀌거나 무의미해진다.

```c
const char *line; size_t len;
while (line_reader_next(&lr, &line, &len) == 1) {
    process(line, len);      // line을 이 안에서만 쓴다
    // char *saved = strndup(line, len);  <- 오래 보관하려면 반드시 복사
}
```

이 소유권 규칙("포인터는 다음 호출 전까지만 유효하다")은 함수 시그니처만 봐서는 드러나지 않으므로, 헤더 주석이나 문서에 명시적으로 적어둬야 한다. 오래 보관해야 한다면 호출자가 직접 복사(`memcpy`, `strndup`)해야 한다.

## 실패 후 상태

`read`가 `EINTR`이 아닌 다른 에러(`EBADF`, `EIO` 등)로 실패했을 때, 읽기 객체는 어떤 상태로 남아야 하는가를 미리 정해둬야 한다. 위 구현은 에러 시 `-1`을 반환하지만 `lr->len`에 쌓여 있던 데이터는 그대로 남아있다. 이걸 "복구 불가능한 상태"로 간주하고 호출자가 재시도하지 않도록 명확히 계약해야 한다.

```c
int rc = line_reader_next(&lr, &line, &len);
if (rc < 0) {
    // 이 시점 이후로 lr을 다시 line_reader_next에 넘기지 않으며
    // 필요하면 close(lr.fd) 하고 객체를 폐기한다.
}
```

에러 이후에도 같은 객체로 계속 호출을 이어가면, 이전에 절반쯤 쌓인 버퍼 상태와 뒤섞여 레코드 경계가 깨진 채로 파싱을 계속하는 조용한 데이터 손상이 생길 수 있다. "에러 이후 상태는 재시도 불가"를 기본값으로 삼고, 재시도 가능한 에러(`EAGAIN` 등)만 예외로 명시하는 편이 안전하다.

## 파일 디스크립터 소유자

fd 하나를 누가 `close`할 책임을 지는지는 코드 전체에서 딱 한 곳으로 정해져 있어야 한다. `line_reader`가 `fd`를 필드로 들고 있다고 해서 자동으로 그 객체가 `close`의 소유자가 되는 건 아니다. 예를 들어 `stdin`(fd 0)을 감싼 `line_reader`라면, 그 객체가 소멸된다고 해서 `close(0)`을 호출하면 안 된다.

```c
typedef struct {
    line_reader lr;
    int owns_fd; // 이 구조체가 close 책임을 지는지
} line_reader_owned;

void line_reader_owned_close(line_reader_owned *lro) {
    if (lro->owns_fd && lro->lr.fd != -1) {
        close(lro->lr.fd);
        lro->lr.fd = -1;
    }
}
```

`dup`으로 fd를 복제했다면 원본과 복제본은 같은 열린 파일 설명(open file description)을 공유하지만 서로 다른 fd 번호이므로, 어느 쪽을 닫아도 나머지 하나는 계속 유효하다. 이 경우 "복제본을 넘겨받은 쪽이 자기 fd만 닫는다"는 규칙이 자연스럽다. 소유권이 불분명하면 이중 `close`(두 번째 `close`는 이미 재사용된 엉뚱한 파일을 닫아버릴 위험이 있다)나 fd 누수 중 하나로 귀결된다.

## Blocking과 non-blocking

fd는 열릴 때(또는 `fcntl(fd, F_SETFL, O_NONBLOCK)`로 나중에) 블로킹 모드와 논블로킹 모드 중 하나로 설정된다. 두 모드는 "데이터가 아직 없을 때 `read`/`write`가 어떻게 반응하는가"에서 갈린다.

### Blocking FD

기본값이다. 읽을 데이터가 없으면 `read`는 데이터가 도착하거나 EOF가 될 때까지 호출한 스레드를 그대로 재운다. 위에서 본 `line_reader_next` 루프는 이 모드를 전제로 짜여 있다. `read`가 당장 돌아오지 않아도 그냥 기다리면 되기 때문이다.

### Non-blocking FD

```c
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

이 모드에서는 읽을 데이터가 없으면 `read`가 기다리지 않고 즉시 `-1`을 반환하며 `errno`를 `EAGAIN`(또는 `EWOULDBLOCK`, 플랫폼에 따라 같은 값)으로 설정한다. 이건 에러가 아니라 "지금은 데이터가 없다"는 정상 신호이므로, 블로킹 모드용으로 짠 재시도 루프에 이 조건을 그대로 넣으면 CPU를 100%로 태우며 스핀하게 된다.

```c
ssize_t n = read(fd, buf, sizeof(buf));
if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // 지금 읽을 게 없다. select/poll/epoll로 fd가 준비될 때까지 기다린 뒤 재시도
    } else if (errno == EINTR) {
        // 재시도
    } else {
        perror("read");
    }
}
```

논블로킹 fd는 보통 `select`/`poll`/`epoll` 같은 이벤트 통지 메커니즘과 함께 써서, "지금 읽어도 되는 fd만 골라 깨워주는" 방식으로 여러 fd를 하나의 스레드에서 다루는 데 쓰인다.

## 전체 상태 예시

지금까지의 요소(fd 소유권, 상태를 보관하는 읽기 객체, `EINTR` 재시도, 에러 후 상태, 논블로킹 처리)를 하나로 모은 예제다.

```c
typedef struct {
    int fd;
    int owns_fd;
    char buf[4096];
    size_t len;
    int eof;
    int failed; // 복구 불가능한 에러가 난 적이 있는지
} stream_reader;

void stream_reader_init(stream_reader *sr, int fd, int owns_fd) {
    sr->fd = fd;
    sr->owns_fd = owns_fd;
    sr->len = 0;
    sr->eof = 0;
    sr->failed = 0;
}

// 반환: 1=레코드 있음, 0=더 없음(EOF), -1=에러(sr->failed 확인)
int stream_reader_next_line(stream_reader *sr, const char **line, size_t *len) {
    if (sr->failed) return -1;

    for (;;) {
        char *nl = memchr(sr->buf, '\n', sr->len);
        if (nl) {
            size_t n = (size_t)(nl - sr->buf);
            *line = sr->buf;
            *len = n;
            size_t consumed = n + 1;
            memmove(sr->buf, sr->buf + consumed, sr->len - consumed);
            sr->len -= consumed;
            return 1;
        }
        if (sr->eof) {
            if (sr->len > 0) {
                *line = sr->buf; *len = sr->len; sr->len = 0;
                return 1;
            }
            return 0;
        }
        if (sr->len == sizeof(sr->buf)) { sr->failed = 1; return -1; }

        ssize_t n = read(sr->fd, sr->buf + sr->len, sizeof(sr->buf) - sr->len);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; // 지금은 없음
            sr->failed = 1;
            return -1;
        }
        if (n == 0) { sr->eof = 1; continue; }
        sr->len += (size_t)n;
    }
}

void stream_reader_close(stream_reader *sr) {
    if (sr->owns_fd && sr->fd != -1) close(sr->fd);
    sr->fd = -1;
}
```

## 테스트할 내용

이런 읽기 객체를 검증할 때 최소한 확인해야 할 시나리오들이다.

- 한 번의 `read`에 여러 줄이 한꺼번에 들어오는 경우 (`"a\nb\nc\n"`)
- 한 줄이 여러 번의 `read`에 걸쳐 조각나서 들어오는 경우
- 빈 줄이 연속으로 오는 경우 (`"a\n\n\nb\n"`)
- 종결자 없이 EOF로 끝나는 마지막 조각
- 완전히 빈 입력(첫 `read`부터 바로 EOF)
- 버퍼 용량보다 긴 단일 레코드(에러로 처리되는지)
- `EINTR`로 중단됐다가 재시도되는 경로 (시그널을 강제로 걸어 재현)
- 논블로킹 fd에서 `EAGAIN`이 났을 때 데이터 손실 없이 다음 호출에서 이어지는지
- 에러(`failed`) 발생 후 같은 객체를 다시 호출했을 때 항상 실패로 일관되게 반환하는지
- 짧은 `write`가 발생했을 때 나머지를 전부 보낼 때까지 재시도 루프가 정확히 이어붙이는지