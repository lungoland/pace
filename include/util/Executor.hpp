#pragma once

#include <condition_variable>
#include <coroutine>
#include <deque>
#include <mutex>

namespace util
{
   class Executor
   {
      public:

         void post( std::coroutine_handle<> handle )
         {
            {
               std::lock_guard lock{ mutex };
               ready.push_back( handle );
            }
            cv.notify_one();
         }

         bool run_one()
         {
            std::coroutine_handle<> next;
            {
               std::unique_lock lock{ mutex };
               cv.wait( lock, [ this ] { return ! ready.empty(); } );
               next = ready.front();
               ready.pop_front();
            }

            if( next )
            {
               next.resume();
               return true;
            }

            return false;
         }

      private:

         std::mutex                          mutex;
         std::condition_variable             cv;
         std::deque<std::coroutine_handle<>> ready;
   };

   /// @brief Awaitable that returns the current coroutine's executor without suspending.
   struct current_executor_t
   {
         std::shared_ptr<Executor> result;

         bool await_ready() const noexcept
         {
            return false;
         }

         template <typename Promise>
         bool await_suspend( std::coroutine_handle<Promise> h ) noexcept
         {
            if constexpr( requires( Promise& p ) { p.get_executor(); } )
            {
               result = h.promise().get_executor();
            }
            return false;
         }

         std::shared_ptr<Executor> await_resume() noexcept
         {
            return std::move( result );
         }
   };

   inline current_executor_t current_executor() noexcept
   {
      return {};
   }

} // namespace util