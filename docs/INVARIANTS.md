# Runtime invariants (milestone 39)

These are enforced with **`assert`** in Debug builds (`NDEBUG` disables them). They document expectations at **critical state transitions** and buffer boundaries.

## Shutdown lifecycle (`ShutdownCoordinator`)

- After a successful `Running → Draining` CAS in `request_shutdown()`, phase is **`Draining`**.
- `try_advance_to_stopped_if_drained` only runs when phase is **`Draining`**, the listener is stopped, there are **no** active connections, and **no** pending writes; then phase becomes **`Stopped`** (possibly via a concurrent peer that wins the CAS first).

## Connection buffers (`Connection`)

- `set_state` only accepts enum values up to **`ConnectionState::Closing`**.
- `consume_read` / `consume_write` require **`byte_count <=` current buffer size** (caller must not over-consume).

## Worker pool (`WorkerPool`)

- After `try_submit`, inbound task count **`<= task_capacity_`**.
- After a worker pops a task, inbound size remains **`<= task_capacity_`**.
- Before pushing a completed result, outbound size **`< result_capacity_`**; after push, **`<= result_capacity_`**.
- After `try_pop_result`, outbound size is **`< result_capacity_`** (strictly below cap until another push fills it).

## Frame codec (`try_decode_frame` / `append_encoded_frame`)

- On **`Complete`**, `consumed_bytes == 4 + payload.size()` and matches the declared length prefix.
- After a successful append, `out.size()` grew by exactly **`4 + payload.size()`** bytes.
