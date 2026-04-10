#pragma once

#include "util/Executor.hpp"

#include <coroutine>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace util
{
   template <typename T>
   class TaskQueue
   {
      public:

         struct waiter
         {
               std::coroutine_handle<>   handle{};
               std::shared_ptr<Executor> executor{};
         };

         struct item_awaiter
         {
               explicit item_awaiter( TaskQueue& owner )
                  : queue( owner )
               {}

               bool await_ready()
               {
                  std::lock_guard lock{ queue.mutex };
                  if( queue.closed )
                  {
                     return true;
                  }

                  if( queue.queue.empty() )
                  {
                     return false;
                  }

                  message = std::move( queue.queue.front() );
                  queue.queue.pop_front();
                  return true;
               }

               template <typename Promise>
               bool await_suspend( std::coroutine_handle<Promise> h )
               {
                  if constexpr( requires( Promise& p ) { p.get_executor(); } )
                  {
                     executor = h.promise().get_executor();
                  }

                  std::lock_guard lock{ queue.mutex };
                  if( ! queue.queue.empty() )
                  {
                     message = std::move( queue.queue.front() );
                     queue.queue.pop_front();
                     return false;
                  }

                  if( queue.closed )
                  {
                     return false;
                  }

                  queue.waiting.push_back( waiter{ .handle = h, .executor = std::move( executor ) } );
                  return true;
               }

               T await_resume()
               {
                  if( ! message )
                  {
                     std::lock_guard lock{ queue.mutex };
                     if( ! queue.queue.empty() )
                     {
                        message = std::move( queue.queue.front() );
                        queue.queue.pop_front();
                     }
                  }
                  return std::move( message );
               }

               TaskQueue&                queue;
               T                         message{};
               std::shared_ptr<Executor> executor{};
         };

         std::function<void( T )> get_message_callback()
         {
            return std::bind( &TaskQueue::post, this, std::placeholders::_1 );
         }

         void post( T msg )
         {
            waiter current;

            {
               std::lock_guard lock{ mutex };
               if( closed )
               {
                  return;
               }

               queue.push_back( std::move( msg ) );

               if( ! waiting.empty() )
               {
                  current = std::move( waiting.front() );
                  waiting.pop_front();
               }
            }

            if( current.handle )
            {
               if( current.executor )
               {
                  current.executor->post( current.handle );
               }
               else
               {
                  current.handle.resume();
               }
            }
         }


         item_awaiter next()
         {
            return item_awaiter{ *this };
         }

         void close()
         {
            std::deque<waiter> waiters;

            {
               std::lock_guard lock{ mutex };
               closed = true;
               waiters.swap( waiting );
            }

            for( auto& current : waiters )
            {
               if( ! current.handle )
               {
                  continue;
               }

               if( current.executor )
               {
                  current.executor->post( current.handle );
               }
               else
               {
                  current.handle.resume();
               }
            }
         }

         void reset()
         {
            std::lock_guard lock{ mutex };
            closed = false;
         }

      private:

         std::mutex         mutex;
         std::deque<T>      queue;
         std::deque<waiter> waiting;
         bool               closed{ false };
   };

} // namespace util