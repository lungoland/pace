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

} // namespace util