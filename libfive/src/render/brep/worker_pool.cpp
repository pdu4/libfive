/*
libfive: a CAD kernel for modeling with implicit functions

Copyright (C) 2018  Matt Keeter

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this file,
You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include "libfive/render/brep/worker_pool.hpp"
#include "libfive/render/brep/free_thread_handler.hpp"
#include "libfive/eval/eval_xtree.hpp"

namespace Kernel {

template <typename T, typename Neighbors, unsigned N>
Root<T> WorkerPool<T, Neighbors, N>::build(
        const Tree t, const Region<N>& region,
        double min_feature, double max_err, unsigned workers,
        ProgressCallback progress_callback,
        FreeThreadHandler* free_thread_handler)
{
    std::vector<XTreeEvaluator, Eigen::aligned_allocator<XTreeEvaluator>> es;
    es.reserve(workers);
    for (unsigned i=0; i < workers; ++i)
    {
        es.emplace_back(XTreeEvaluator(t));
    }
    std::atomic_bool cancel(false);
    return build(es.data(), region, min_feature,
                 max_err, workers, cancel, progress_callback,
                 free_thread_handler);
}

template <typename T, typename Neighbors, unsigned N>
Root<T> WorkerPool<T, Neighbors, N>::build(
    XTreeEvaluator* eval,
    const Region<N>& region_, double min_feature,
    double max_err, unsigned workers, std::atomic_bool& cancel,
    ProgressCallback progress_callback,
    FreeThreadHandler* free_thread_handler)
{
    const auto region = region_.withResolution(min_feature);
    auto root(new T(nullptr, 0, region));
    std::atomic_bool done(false);

    LockFreeStack tasks(workers);
    tasks.push({root, eval->deck->tape, region, Neighbors()});

    std::vector<std::future<void>> futures;
    futures.resize(workers);

    Root<T> out(root);
    std::mutex root_lock;

    // Kick off the progress tracking thread, based on the number of
    // octree levels and a fixed split per level
    uint64_t ticks = 0;
    for (int i=0; i <= region.level; ++i) {
        ticks = (ticks + 1) * (1 << N);
    }
    auto progress_watcher = ProgressWatcher::build(ticks, 0, progress_callback,
                                                   done, cancel);

    for (unsigned i=0; i < workers; ++i)
    {
        futures[i] = std::async(std::launch::async,
                [&eval, &tasks, &cancel, &done, &out, &root_lock,
                 max_err, i, progress_watcher, free_thread_handler](){
                    run(eval + i, tasks, max_err, done, cancel, out,
                        root_lock, progress_watcher, free_thread_handler);
                    });
    }

    // Wait on all of the futures
    for (auto& f : futures)
    {
        f.get();
    }

    assert(done.load() || cancel.load());

    // Wait for the progress bar to finish, which happens in the destructor.
    delete progress_watcher;

    if (cancel.load())
    {
        return Root<T>();
    }
    else
    {
        return out;
    }
}

template <typename T, typename Neighbors, unsigned N>
void WorkerPool<T, Neighbors, N>::run(
        XTreeEvaluator* eval, LockFreeStack& tasks, const float max_err,
        std::atomic_bool& done, std::atomic_bool& cancel,
        Root<T>& root, std::mutex& root_lock,
        ProgressWatcher* progress, FreeThreadHandler* free_thread_handler)
{
    // Tasks to be evaluated by this thread (populated when the
    // MPMC stack is completely full).
    std::stack<Task, std::vector<Task>> local;

    typename T::Pool object_pool;

    while (!done.load() && !cancel.load())
    {
        // Prioritize picking up a local task before going to
        // the MPMC queue, to keep things in this thread for
        // as long as possible.
        Task task;
        if (local.size())
        {
            task = local.top();
            local.pop();
        }
        else if (!tasks.pop(task))
        {
            task.target = nullptr;
        }

        // If we failed to get a task, keep looping
        // (so that we terminate when either of the flags are set).
        if (task.target == nullptr)
        {
            if (free_thread_handler != nullptr) {
                free_thread_handler->offerWait();
            }
            continue;
        }

        auto tape = task.tape;
        auto t = task.target;
        Region<N> region = task.region;

        // Find our local neighbors.  We do this at the last minute to
        // give other threads the chance to populate more pointers.
        Neighbors neighbors;
        if (t->parent)
        {
            neighbors = task.parent_neighbors.push(
                t->parent_index, t->parent->children);
        }

        // If this tree is larger than the minimum size, then it will either
        // be unambiguously filled/empty, or we'll need to recurse.
        const bool can_subdivide = region.level > 0;
        if (can_subdivide)
        {
            tape = t->evalInterval(eval, task.tape, region, object_pool);

            // If this Tree is ambiguous, then push the children to the stack
            // and keep going (because all the useful work will be done
            // by collectChildren eventually).
            assert(t->type != Interval::UNKNOWN);
            if (t->type == Interval::AMBIGUOUS)
            {
                auto rs = region.subdivide();
                for (unsigned i=0; i < t->children.size(); ++i)
                {
                    // If there are available slots, then pass this work
                    // to the queue; otherwise, undo the decrement and
                    // assign it to be evaluated locally.
                    auto next_tree = object_pool.get(t, i, rs[i]);
                    Task next{next_tree, tape, rs[i], neighbors};
                    if (!tasks.bounded_push(next))
                    {
                        local.push(next);
                    }
                }

                // If we did an interval evaluation, then we either
                // (a) are done with this tree because it is empty / filled
                // (b) don't do anything until all of its children are done
                //
                // In both cases, we should keep looping; the latter case
                // is handled in collectChildren below.
                continue;
            }
        }
        else
        {
            t->evalLeaf(eval, tape, region, object_pool, neighbors);
        }

        if (progress)
        {
            if (can_subdivide)
            {
                // Accumulate all of the child XTree cells that would have been
                // included if we continued to subdivide this tree, then pass
                // all of them to the progress tracker
                uint64_t ticks = 0;
                for (int i=0; i <= region.level; ++i) {
                    ticks = (ticks + 1) * (1 << N);
                }
                progress->tick(ticks);
            }
            else
            {
                progress->tick(1);
            }
        }

        // If all of the children are done, then ask the parent to collect them
        // (recursively, merging the trees on the way up, and reporting
        // completed tree cells to the progress tracker if present).
        auto up = [&]{
            region = region.parent(t->parent_index);
            tape = Tape::getBase(tape, region.region3());
            t = t->parent;
        };
        up();
        while (t != nullptr &&
               t->collectChildren(eval, tape, region, object_pool, max_err))
        {
            // Report the volume of completed trees as we walk back
            // up towards the root of the tree.
            if (progress) {
                progress->tick();
            }
            up();
        }

        // Termination condition:  if we've ended up pointing at the parent
        // of the tree's root (which is nullptr), then we're done and break
        if (t == nullptr)
        {
            break;
        }
    }

    // If we've broken out of the loop, then we should set the done flag
    // so that other worker threads also terminate.
    done.store(true);

    {   // Release the pooled objects to the root
        std::lock_guard<std::mutex> lock(root_lock);
        root.claim(object_pool);
    }
}

}   // namespace Kernel
