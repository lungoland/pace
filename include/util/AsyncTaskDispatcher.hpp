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

         using WorkItem = std::function<Task<void>()>;

         void reset()
         {
            stopRequested.store( false, std::memory_order_release );
            queue.reset();
         }

         void post( std::string name, WorkItem work )
         {
            queue.post(
               [ n = std::move( name ), w = std::move( work ), logger = logger ]() mutable -> Task<void>
               {
                  logger->debug( "running task: '{}'", n );
                  co_await w();
                  logger->debug( "finished task: '{}'", n );
                  co_return;
               } );
         }

         Task<void> run()
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
               try
               {
                  co_await work();
               }
               catch( const std::exception& ex )
               {
                  logger->error( "Work item failed: {}", ex.what() );
               }
               catch( ... )
               {
                  logger->error( "Work item failed with non-standard exception" );
               }
            }

            workerExecuting.store( false, std::memory_order_release );
            workerActive.store( false, std::memory_order_release );
            co_return;
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
