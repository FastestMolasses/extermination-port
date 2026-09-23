/* WP-3 S6 unit test: the EM_FRAME_TRACE recorder (src/game/em_frame_trace.c).
 * Checks the exact JSONL line format (em_frame_trace.h), that every protocol
 * violation and I/O failure latches a {"error":...} line instead of being
 * dropped, the EM_FRAME_TRACE gate, and that the env hook has the worker
 * table's EmSceneTraceFn signature. The comparison against the original traces
 * is tools/compare_frame_order.py --self-test (which also replays every
 * original trace through this recorder).
 * Build: cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc
 *        tests/frame_trace_test.c src/game/em_frame_trace.c */
#define _POSIX_C_SOURCE 200809L /* fork, setenv, mkdtemp */
#define _DARWIN_C_SOURCE        /* mkdtemp on macOS */
#include "game/em_frame_trace.h"
#include "game/em_scene_workers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x)                                                                     \
    do {                                                                             \
        if (!(x)) {                                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x);    \
            exit(1);                                                                 \
        }                                                                            \
    } while (0)

static char dir[256];

static void path_for(char *out, size_t n, const char *name)
{
    snprintf(out, n, "%s/%s", dir, name);
}

static char *slurp(const char *path)
{
    FILE *fp = fopen(path, "rb");
    CHECK(fp);
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    CHECK(buf);
    CHECK(fread(buf, 1, (size_t)n, fp) == (size_t)n);
    buf[n] = '\0';
    fclose(fp);
    return buf;
}

static void test_format(void)
{
    char path[512];
    path_for(path, sizeof path, "format.jsonl");
    EmFrameTrace t;
    CHECK(em_frame_trace_open(&t, path) == 0);

    /* A cutscene-variant tick shaped like cut02 frame 0 (task +8..+C = 3,1,0,1,0,
     * selector 2, classifier 0; 001AE6B0 -> 001AFD70(1) and one node). */
    static const uint8_t task[5] = {3, 1, 0, 1, 0};
    em_frame_trace_tick_begin(&t, 2940);
    em_frame_trace_call(&t, 0x1ACEC0, 0x1AD250, 0, 0, 0, 0);
    em_frame_trace_frame_state(&t, task, 2, 0);
    em_frame_trace_call(&t, 0x1AE040, 0x1AE7E0, 0, 0, 0, 0);
    em_frame_trace_classifier(&t, 0);
    em_frame_trace_call(&t, 0x1AE6B0, 0x1AFD70, 1, 0, 0, 0xFFFFFFFFu);
    em_frame_trace_node(&t, 0x219550, 132, "deferred[g0.0]", "pickup \"a\"\\b\n");
    em_frame_trace_node(&t, 0x1C5930, 8, NULL, NULL);
    CHECK(em_frame_trace_tick_end(&t) == 0);

    /* A tick that never enters 0x1AE040. */
    em_frame_trace_tick_begin(&t, 2941);
    CHECK(em_frame_trace_tick_end(&t) == 0);
    CHECK(t.ticks == 2);
    CHECK(em_frame_trace_close(&t) == 0);

    char *text = slurp(path);
    const char *want =
        "{\"counter\":2940,\"task\":[3,1,0,1,0],\"classifier\":0,\"selector\":2,\"fade\":0,"
        "\"events\":["
        "{\"fn\":\"001ACEC0\",\"target\":\"001AD250\",\"args\":[0,0,0,0]},"
        "{\"fn\":\"001AE040\",\"target\":\"001AE7E0\",\"args\":[0,0,0,0]},"
        "{\"fn\":\"001AE6B0\",\"target\":\"001AFD70\",\"args\":[1,0,0,4294967295]},"
        "{\"fn\":\"001AFD70\",\"op\":\"jalr\",\"callback\":\"00219550\",\"class\":132,"
        "\"record\":\"deferred[g0.0]\",\"binding\":\"pickup \\\"a\\\"\\\\b\\u000a\"},"
        "{\"fn\":\"001AFD70\",\"op\":\"jalr\",\"callback\":\"001C5930\",\"class\":8,"
        "\"record\":null,\"binding\":null}]}\n"
        "{\"counter\":2941,\"task\":null,\"classifier\":null,\"selector\":null,\"fade\":null,"
        "\"events\":[]}\n";
    if (strcmp(text, want) != 0) {
        fprintf(stderr, "got:\n%s\nwant:\n%s\n", text, want);
        CHECK(0);
    }
    free(text);
}

/* Each violation must latch exactly one error line and stop recording. */
typedef void (*Violation)(EmFrameTrace *t);

static void v_call_outside(EmFrameTrace *t) { em_frame_trace_call(t, 0x1AE5E0, 0x15BCF0, 0, 0, 0, 0); }
static void v_node_outside(EmFrameTrace *t) { em_frame_trace_node(t, 0x1C5930, 8, NULL, NULL); }
static void v_classifier_outside(EmFrameTrace *t) { em_frame_trace_classifier(t, 0); }
static void v_end_outside(EmFrameTrace *t) { CHECK(em_frame_trace_tick_end(t) == -1); }
static void v_begin_twice(EmFrameTrace *t)
{
    em_frame_trace_tick_begin(t, 9);
    em_frame_trace_tick_begin(t, 10);
}
static void v_second_classifier(EmFrameTrace *t)
{
    em_frame_trace_tick_begin(t, 9);
    em_frame_trace_classifier(t, 0);
    em_frame_trace_classifier(t, 2);
}
static void v_second_state(EmFrameTrace *t)
{
    static const uint8_t task[5] = {0, 0, 0, 1, 0};
    em_frame_trace_tick_begin(t, 9);
    em_frame_trace_frame_state(t, task, 0, 0);
    em_frame_trace_frame_state(t, task, 0, 0);
}
static void v_open_at_close(EmFrameTrace *t) { em_frame_trace_tick_begin(t, 9); }

static void test_violations(void)
{
    static const struct { Violation fn; const char *msg; } cases[] = {
        {v_call_outside, "call outside a tick"},
        {v_node_outside, "walk node outside a tick"},
        {v_classifier_outside, "classifier outside a tick"},
        {v_end_outside, "tick ended without a tick open"},
        {v_begin_twice, "tick begun while a tick is open"},
        {v_second_classifier, "second classifier result in one tick"},
        {v_second_state, "second frame state in one tick"},
        {v_open_at_close, "tick still open at close"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        char path[512];
        path_for(path, sizeof path, "violation.jsonl");
        EmFrameTrace t;
        CHECK(em_frame_trace_open(&t, path) == 0);
        em_frame_trace_tick_begin(&t, 7); /* one good tick first */
        CHECK(em_frame_trace_tick_end(&t) == 0);
        cases[i].fn(&t);
        if (cases[i].fn != v_open_at_close) {
            CHECK(em_frame_trace_failed(&t));
            /* Latched: later input is not recorded. */
            em_frame_trace_tick_begin(&t, 8);
            em_frame_trace_call(&t, 0x1AE5E0, 0x15BCF0, 0, 0, 0, 0);
            CHECK(em_frame_trace_tick_end(&t) == -1);
        }
        CHECK(em_frame_trace_close(&t) == -1);
        char *text = slurp(path);
        const char *first = "{\"counter\":7,\"task\":null,\"classifier\":null,\"selector\":null,"
                            "\"fade\":null,\"events\":[]}\n";
        CHECK(strncmp(text, first, strlen(first)) == 0);
        const char *rest = text + strlen(first);
        char want[256];
        snprintf(want, sizeof want, "{\"error\":\"%s\",", cases[i].msg);
        if (strncmp(rest, want, strlen(want)) != 0) {
            fprintf(stderr, "case %zu: got %s want prefix %s\n", i, rest, want);
            CHECK(0);
        }
        CHECK(strchr(rest, '\n') == rest + strlen(rest) - 1); /* exactly one more line */
        free(text);
    }

    /* An unopenable path is a failure, and the recorder stays inert. */
    EmFrameTrace t;
    CHECK(em_frame_trace_open(&t, "/nonexistent-dir-em-frame-trace/x.jsonl") == -1);
    CHECK(em_frame_trace_failed(&t));
    em_frame_trace_tick_begin(&t, 1);
    em_frame_trace_call(&t, 1, 2, 0, 0, 0, 0);
    CHECK(em_frame_trace_tick_end(&t) == -1);
    CHECK(em_frame_trace_close(&t) == -1);
}

static void test_env(void)
{
    /* Unset: disabled, and the hook ignores calls (checked in a child so this
     * process can still enable the gate below). */
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        unsetenv("EM_FRAME_TRACE");
        if (em_frame_trace_env() != NULL)
            _exit(1);
        em_frame_trace_env_hook(NULL, 1, 2, 0, 0, 0, 0);
        _exit(0);
    }
    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    char path[512];
    path_for(path, sizeof path, "env.jsonl");
    CHECK(setenv("EM_FRAME_TRACE", path, 1) == 0);
    EmFrameTrace *t = em_frame_trace_env();
    CHECK(t != NULL);
    CHECK(em_frame_trace_env() == t);

    /* The hook installs as the worker table's trace function. */
    EmSceneWorkers w;
    memset(&w, 0, sizeof w);
    int ctx = 0;
    w.ctx = &ctx;
    w.trace = em_frame_trace_env_hook;
    em_frame_trace_tick_begin(t, 4085);
    w.trace(w.ctx, 0x1AE5E0, 0x1AFD70, 0, 0, 0, 0);
    CHECK(em_frame_trace_tick_end(t) == 0);

    char *text = slurp(path); /* each tick is flushed */
    CHECK(strcmp(text, "{\"counter\":4085,\"task\":null,\"classifier\":null,\"selector\":null,"
                       "\"fade\":null,\"events\":[{\"fn\":\"001AE5E0\",\"target\":\"001AFD70\","
                       "\"args\":[0,0,0,0]}]}\n") == 0);
    free(text);
}

int main(void)
{
    const char *base = getenv("TMPDIR");
    snprintf(dir, sizeof dir, "%s/em_frame_trace_test_XXXXXX", base && *base ? base : "/tmp");
    CHECK(mkdtemp(dir) != NULL);
    test_format();
    test_violations();
    test_env();
    printf("frame_trace_test: all checks passed\n");
    return 0;
}
