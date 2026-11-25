/* coro_ucontext.c - POSIX ucontext backend for coro.h. */
#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <ucontext.h>

#include "coro.h"

struct nmd_coro {
    ucontext_t self, caller;
    void     (*entry)(void);
    void      *stack;
    bool       done;
};

static nmd_coro *current;

static void trampoline(void)
{
    nmd_coro *c = current;
    c->entry();
    c->done = true;
    swapcontext(&c->self, &c->caller);
}

nmd_coro *coro_create(void (*entry)(void), unsigned stack_size)
{
    nmd_coro *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->stack = malloc(stack_size);
    if (!c->stack) { free(c); return NULL; }
    c->entry = entry;
    getcontext(&c->self);
    c->self.uc_stack.ss_sp = c->stack;
    c->self.uc_stack.ss_size = stack_size;
    c->self.uc_link = NULL;
    makecontext(&c->self, trampoline, 0);
    return c;
}

void coro_destroy(nmd_coro *c)
{
    if (!c) return;
    free(c->stack);
    free(c);
}

void coro_resume(nmd_coro *c)
{
    if (c->done) return;
    nmd_coro *prev = current;
    current = c;
    swapcontext(&c->caller, &c->self);
    current = prev;
}

void coro_yield(nmd_coro *c)
{
    swapcontext(&c->self, &c->caller);
}

bool coro_done(const nmd_coro *c) { return c->done; }
