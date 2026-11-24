/* coro.h - minimal coroutine used to run game code between frames. */
#ifndef NMD_CORO_H
#define NMD_CORO_H

#include <stdbool.h>

typedef struct nmd_coro nmd_coro;

nmd_coro *coro_create(void (*entry)(void), unsigned stack_size);
void      coro_destroy(nmd_coro *c);

/* Switch into the coroutine; returns when it yields or its entry returns. */
void coro_resume(nmd_coro *c);
/* Called from inside the coroutine: switch back to the resumer. */
void coro_yield(nmd_coro *c);
bool coro_done(const nmd_coro *c);

#endif
