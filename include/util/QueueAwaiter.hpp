#pragma once

#include "util/Executor.hpp"
#include "util/Task.hpp"
#include "util/TaskQueue.hpp"

#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <utility>

namespace util
{
   /**
    * @brief Awaiter for a coroutine context switch through a TaskQueue.
    *
    * Suspends the caller, posts the work onto the queue (to be executed by the
    * thread running TaskQueue::next()), and resumes the caller on its original
    * executor once the work completes.
    *
    * Usage:
    *   co_await QueueAwaiter<bool>(
    *       messageQueue,
    *       [this]() -> Task<bool> { co_await client.publish(...); co_return true; }()
    *   );
    */
   template <typename T>
   class QueueAwaiter
   {
      public:

         explicit QueueAwaiter( TaskQueue<std::function<Task<bool>()>>& q, Task<T> w ) noexcept
            : queue( q )
            , work( std::move( w ) )
         {}

         bool await_ready() const noexcept
         {
            return false; // Always suspend to perform the context switch
         }

         template <typename Promise>
         bool await_suspend( std::coroutine_handle<Promise> caller ) noexcept
         {
            // Capture the caller's executor so the work can resume it when done
            if constexpr( requires( Promise& p ) { p.get_executor(); } )
            {
               callerExecutor = caller.promise().get_executor();
            }

            // Post a lambda to the queue; when run() picks it up, it executes
            // the work then resumes the suspended caller via its own executor.
            queue.post(
               [ this, caller ]() mutable -> Task<bool>
               {
                  try
                  {
                     result = co_await work;
                  }
                  catch( ... )
                  {
                     error = std::current_exception();
                     try
                     {
                        std::rethrow_exception( error );
                     }
                     catch( const std::exception& ex )
                     {
                        spdlog::error( "QueueAwaiter work exception: {}", ex.what() );
                     }
                     catch( ... )
                     {
                        spdlog::error( "QueueAwaiter work exception: unknown non-std exception" );
                     }
                  }

                  // Resume the caller: post back to its executor if available,
                  // otherwise resume inline (e.g. in sync_wait without executor).
                  if( callerExecutor )
                  {
                     callerExecutor->post( caller );
                  }
                  else
                  {
                     caller.resume();
                  }

                  co_return true;
               } );

            return true; // Suspend the caller until the work resumes it
         }

         T await_resume()
         {
            if( error )
            {
               std::rethrow_exception( error );
            }
            return std::move( result );
         }

      private:

         TaskQueue<std::function<Task<bool>()>>& queue;
         Task<T>                                 work;
         std::shared_ptr<Executor>               callerExecutor{};
         std::exception_ptr                      error{};
         T                                       result{};
   };

} // namespace util
