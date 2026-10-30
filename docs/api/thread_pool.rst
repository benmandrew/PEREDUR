thread_pool.hpp
================

A fixed-size pool of worker threads consuming tasks from a shared queue. Workers are spawned once at construction and reused for the pool's lifetime, avoiding the per-task OS thread creation cost of ``std::async``. The pool exposes ``submit(callable)``, which queues the callable and returns nothing: a task reports its own completion, as ``run_bounded_async``'s tasks do through their completion queue.

``global_thread_pool()`` returns the process-lifetime pool shared by all fitness-evaluation dispatch sites, sized to ``Config::parallel``, which defaults to ``available_parallelism()``. Callers that need test isolation can construct their own ``ThreadPool`` instance.

.. doxygenfile:: thread_pool.hpp
