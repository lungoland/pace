#pragma once

#include "util/Logger.hpp"
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

         void post( std::string name, WorkItem work )
         {
            queue.post(
               [ n = std::move( name ), w = std::move( work ), logger = logger ]() mutable -> Task<bool>
               {
                  logger->debug( "running task: '{}'", n );
                  const bool result = co_await w();
                  logger->debug( "finished task: '{}' (result={})", n, result );
                  co_return result;
               } );
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
                     explicit executing_guard( std::atomic_bool& f )
                        : flag( f )
                     {
                        flag.store( true, std::memory_order_release );
                     }

                     ~executing_guard()
                     {
                        flag.store( false, std::memory_order_release );
                     }

                     std::atomic_bool& flag;
               };

               executing_guard guard{ workerExecuting };
               co_await work();
            }

            workerExecuting.store( false, std::memory_order_release );
            workerActive.store( false, std::memory_order_release );
            co_return true;
         }

         void stop()
         {
            logger->info( "Stop requested" );
            if( stopRequested.exchange( true, std::memory_order_acq_rel ) )
            {
               return;
            }

            queue.close();
         }

      private:

         mutable util::Logger logger = util::getLogger( "Dispatcher" );

         TaskQueue<WorkItem> queue;
         std::atomic_bool    stopRequested{ false };
         std::atomic_bool    workerActive{ false };
         std::atomic_bool    workerExecuting{ false };
   };

} // namespace util
