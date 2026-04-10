#include <catch2/catch_test_macros.hpp>

#include "util/Task.hpp"
#include "util/TaskQueue.hpp"

#include <algorithm>
#include <array>
#include <bits/chrono.h>
#include <coroutine>
#include <exception>
#include <fmt/base.h>
#include <functional>
#include <future>
#include <thread>

namespace
{
   using WorkItem = std::function<int()>;

   util::Task<bool> waitForSingleItem( util::TaskQueue<WorkItem>& queue, int& out )
   {
      const auto item = co_await queue.next();
      if( ! item )
      {
         out = -1;
         co_return true;
      }

      out = item();
      co_return true;
   }

} // namespace

TEST_CASE( "task queue supports multiple waiting consumers", "[taskqueue]" )
{
   util::TaskQueue<WorkItem> queue;

   int resultA = 0;
   int resultB = 0;

   std::promise<void> doneA;
   std::promise<void> doneB;
   auto               futureA = doneA.get_future();
   auto               futureB = doneB.get_future();

   std::thread consumerA{ [ & ]
                          {
                             try
                             {
                                util::sync_wait( waitForSingleItem( queue, resultA ) );
                                doneA.set_value();
                             }
                             catch( ... )
                             {
                                doneA.set_exception( std::current_exception() );
                             }
                          } };

   std::thread consumerB{ [ & ]
                          {
                             try
                             {
                                util::sync_wait( waitForSingleItem( queue, resultB ) );
                                doneB.set_value();
                             }
                             catch( ... )
                             {
                                doneB.set_exception( std::current_exception() );
                             }
                          } };

   std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );

   const auto enqueue = queue.get_message_callback();
   enqueue( [] { return 7; } );
   enqueue( [] { return 11; } );

   REQUIRE( futureA.wait_for( std::chrono::seconds( 1 ) ) == std::future_status::ready );
   REQUIRE( futureB.wait_for( std::chrono::seconds( 1 ) ) == std::future_status::ready );

   futureA.get();
   futureB.get();

   consumerA.join();
   consumerB.join();

   std::array<int, 2> results{ resultA, resultB };
   std::ranges::sort( results );
   CHECK( results[ 0 ] == 7 );
   CHECK( results[ 1 ] == 11 );
}

TEST_CASE( "task queue close wakes all waiting consumers", "[taskqueue]" )
{
   util::TaskQueue<WorkItem> queue;

   int resultA = 0;
   int resultB = 0;

   std::promise<void> doneA;
   std::promise<void> doneB;
   auto               futureA = doneA.get_future();
   auto               futureB = doneB.get_future();

   std::thread consumerA{ [ & ]
                          {
                             try
                             {
                                util::sync_wait( waitForSingleItem( queue, resultA ) );
                                doneA.set_value();
                             }
                             catch( ... )
                             {
                                doneA.set_exception( std::current_exception() );
                             }
                          } };

   std::thread consumerB{ [ & ]
                          {
                             try
                             {
                                util::sync_wait( waitForSingleItem( queue, resultB ) );
                                doneB.set_value();
                             }
                             catch( ... )
                             {
                                doneB.set_exception( std::current_exception() );
                             }
                          } };

   std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
   queue.close();

   REQUIRE( futureA.wait_for( std::chrono::seconds( 1 ) ) == std::future_status::ready );
   REQUIRE( futureB.wait_for( std::chrono::seconds( 1 ) ) == std::future_status::ready );

   futureA.get();
   futureB.get();

   consumerA.join();
   consumerB.join();

   CHECK( resultA == -1 );
   CHECK( resultB == -1 );
}
