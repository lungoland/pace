#pragma once

#include "util/QueueAwaiter.hpp"
#include "util/Task.hpp"
#include "util/TaskQueue.hpp"

#include <atomic>
#include <functional>

namespace util
{
   class AsyncTaskDispatcher
   {
      public:

         using WorkItem = std::function<Task<bool>()>;

         void reset()
         {
            stopRequested.store( false, std::memory_order_release );
            queue.reset();
         }

         bool isWorkerActive() const noexcept
         {
            return workerActive.load( std::memory_order_acquire );
         }

         bool isWorkerExecuting() const noexcept
         {
            return workerExecuting.load( std::memory_order_acquire );
         }

         void post( WorkItem work )
         {
            queue.post( std::move( work ) );
         }

         Task<bool> switchToWorker( Task<bool> work )
         {
            co_return co_await QueueAwaiter<bool>( queue, std::move( work ) );
         }

         /**
          * @brief Routes work to the dispatcher thread, or runs it inline if already there.
          *
          * Inline when:
          *   - No worker is active (dispatcher not running) — nothing to hop to.
          *   - Already executing inside a dispatched work item — hopping would self-deadlock.
          * Otherwise: context-switch via the queue so the caller runs on the task thread.
          */
         static constexpr bool dispatchInlineWhen( bool workerActive, bool workerExecuting ) noexcept
         {
            return ! workerActive || workerExecuting;
         }

         Task<bool> dispatch( Task<bool> work )
         {
            if( dispatchInlineWhen( isWorkerActive(), isWorkerExecuting() ) )
            {
               co_return co_await std::move( work );
            }
            co_return co_await switchToWorker( std::move( work ) );
         }

         Task<bool> run()
         {
            workerActive.store( true, std::memory_order_release );
            while( ! stopRequested.load( std::memory_order_acquire ) )
            {
               const auto work = co_await queue.next();
               if( ! work )
               {
                  continue;
               }

               struct executing_guard
               {
                     std::atomic_bool& flag;

                     explicit executing_guard( std::atomic_bool& f )
                        : flag( f )
                     {
                        flag.store( true, std::memory_order_release );
                     }

                     ~executing_guard()
                     {
                        flag.store( false, std::memory_order_release );
                     }
               };

               executing_guard guard{ workerExecuting };
               co_await work();
            }

            workerExecuting.store( false, std::memory_order_release );
            workerActive.store( false, std::memory_order_release );
            co_return true;
         }

         Task<bool> stop()
         {
            if( stopRequested.exchange( true, std::memory_order_acq_rel ) )
            {
               co_return true;
            }

            queue.close();
            co_return true;
         }

      private:

         TaskQueue<WorkItem> queue;
         std::atomic_bool    stopRequested{ false };
         std::atomic_bool    workerActive{ false };
         std::atomic_bool    workerExecuting{ false };
   };

} // namespace util
