#include <catch2/catch_test_macros.hpp>

#include "util/QueueAwaiter.hpp"
#include "util/Task.hpp"
#include "util/TaskQueue.hpp"

#include <bits/chrono.h>
#include <coroutine>
#include <exception>
#include <fmt/base.h>
#include <functional>
#include <future>
#include <stdexcept>
#include <thread>

namespace
{
   using QueueWork = std::function<util::Task<bool>()>;

   util::Task<bool> failingWork()
   {
      throw std::runtime_error( "publish failed" );
      co_return true;
   }

   util::Task<bool> runQueuedFailingWork( util::TaskQueue<QueueWork>& queue )
   {
      co_return co_await util::QueueAwaiter<bool>( queue, failingWork() );
   }

   util::Task<bool> drainSingleQueueItem( util::TaskQueue<QueueWork>& queue )
   {
      const auto item = co_await queue.next();
      if( ! item )
      {
         co_return false;
      }

      co_await item();
      co_return true;
   }

} // namespace

TEST_CASE( "queue awaiter rethrows work exception to caller without breaking queue consumer", "[queueawaiter]" )
{
   util::TaskQueue<QueueWork> queue;

   std::promise<std::exception_ptr> consumerPromise;
   auto                             consumerFuture = consumerPromise.get_future();

   std::thread consumerThread{ [ & ]
                               {
                                  try
                                  {
                                     util::sync_wait( drainSingleQueueItem( queue ) );
                                     consumerPromise.set_value( nullptr );
                                  }
                                  catch( ... )
                                  {
                                     consumerPromise.set_value( std::current_exception() );
                                  }
                               } };

   std::promise<std::exception_ptr> callerPromise;
   auto                             callerFuture = callerPromise.get_future();

   std::thread callerThread{ [ & ]
                             {
                                try
                                {
                                   util::sync_wait( runQueuedFailingWork( queue ) );
                                   callerPromise.set_value( nullptr );
                                }
                                catch( ... )
                                {
                                   callerPromise.set_value( std::current_exception() );
                                }
                             } };

   REQUIRE( callerFuture.wait_for( std::chrono::seconds( 1 ) ) == std::future_status::ready );
   REQUIRE( consumerFuture.wait_for( std::chrono::seconds( 1 ) ) == std::future_status::ready );

   const auto callerException   = callerFuture.get();
   const auto consumerException = consumerFuture.get();

   CHECK( consumerException == nullptr );
   REQUIRE( callerException != nullptr );
   CHECK_THROWS_AS( std::rethrow_exception( callerException ), std::runtime_error );

   callerThread.join();
   consumerThread.join();
}
