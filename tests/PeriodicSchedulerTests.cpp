#include <catch2/catch_test_macros.hpp>

#include "util/Executor.hpp"
#include "util/PeriodicScheduler.hpp"
#include "util/Task.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

namespace
{
   /// Drives the scheduler's executor on a background thread for the duration of a test.
   struct SchedulerDriver
   {
         util::PeriodicScheduler         scheduler;
         std::shared_ptr<util::Executor> executor;
         std::thread                     executorThread;

         explicit SchedulerDriver()
            : executor( std::make_shared<util::Executor>() )
         {
            scheduler.start( executor );
            executorThread = std::thread{ [ this ]
                                          {
                                             while( executor->run_one() )
                                             {}
                                          } };
         }

         ~SchedulerDriver()
         {
            scheduler.stop();
            // Null handle makes run_one() return false, unblocking the drain thread.
            executor->post( {} );
            if( executorThread.joinable() )
            {
               executorThread.join();
            }
         }
   };

} // namespace

TEST_CASE( "scheduler fires job after interval", "[scheduler]" )
{
   SchedulerDriver driver;

   std::atomic_int    count{ 0 };
   std::promise<void> firedPromise;
   auto               firedFuture = firedPromise.get_future();
   bool               promiseSet{ false };

   driver.scheduler.addJob( util::PeriodicScheduler::Job{
      .name     = "counter",
      .interval = std::chrono::milliseconds{ 50 },
      .execute  = [ & ]() -> util::Task<bool>
      {
         ++count;
         if( ! promiseSet )
         {
            promiseSet = true;
            firedPromise.set_value();
         }
         co_return true;
      },
   } );

   REQUIRE( firedFuture.wait_for( std::chrono::seconds{ 2 } ) == std::future_status::ready );
   CHECK( count >= 1 );
}

TEST_CASE( "scheduler fires job multiple times", "[scheduler]" )
{
   SchedulerDriver driver;

   std::atomic_int    count{ 0 };
   std::promise<void> enoughFired;
   auto               enoughFuture = enoughFired.get_future();
   bool               promiseSet{ false };

   driver.scheduler.addJob( util::PeriodicScheduler::Job{
      .name     = "repeater",
      .interval = std::chrono::milliseconds{ 40 },
      .execute  = [ & ]() -> util::Task<bool>
      {
         int c = ++count;
         if( c >= 3 && ! promiseSet )
         {
            promiseSet = true;
            enoughFired.set_value();
         }
         co_return true;
      },
   } );

   REQUIRE( enoughFuture.wait_for( std::chrono::seconds{ 3 } ) == std::future_status::ready );
   CHECK( count >= 3 );
}

TEST_CASE( "scheduler stops firing after removeJob", "[scheduler]" )
{
   SchedulerDriver driver;

   std::atomic_int    count{ 0 };
   std::promise<void> firstFire;
   auto               firstFuture = firstFire.get_future();
   bool               promiseSet{ false };

   driver.scheduler.addJob( util::PeriodicScheduler::Job{
      .name     = "removable",
      .interval = std::chrono::milliseconds{ 30 },
      .execute  = [ & ]() -> util::Task<bool>
      {
         ++count;
         if( ! promiseSet )
         {
            promiseSet = true;
            firstFire.set_value();
         }
         co_return true;
      },
   } );

   // Wait for at least one firing, then remove.
   REQUIRE( firstFuture.wait_for( std::chrono::seconds{ 2 } ) == std::future_status::ready );
   driver.scheduler.removeJob( "removable" );

   const int countAfterRemoval = count.load();
   // Give the scheduler a couple of intervals to (incorrectly) fire again.
   std::this_thread::sleep_for( std::chrono::milliseconds{ 120 } );

   CHECK( count.load() <= countAfterRemoval + 1 );
}

TEST_CASE( "scheduler handles multiple concurrent jobs", "[scheduler]" )
{
   SchedulerDriver driver;

   std::atomic_int    countA{ 0 };
   std::atomic_int    countB{ 0 };
   std::promise<void> bothFired;
   auto               bothFuture = bothFired.get_future();
   bool               promiseSet{ false };

   driver.scheduler.addJob( util::PeriodicScheduler::Job{
      .name     = "jobA",
      .interval = std::chrono::milliseconds{ 30 },
      .execute  = [ & ]() -> util::Task<bool>
      {
         ++countA;
         if( countA >= 1 && countB >= 1 && ! promiseSet )
         {
            promiseSet = true;
            bothFired.set_value();
         }
         co_return true;
      },
   } );

   driver.scheduler.addJob( util::PeriodicScheduler::Job{
      .name     = "jobB",
      .interval = std::chrono::milliseconds{ 50 },
      .execute  = [ & ]() -> util::Task<bool>
      {
         ++countB;
         if( countA >= 1 && countB >= 1 && ! promiseSet )
         {
            promiseSet = true;
            bothFired.set_value();
         }
         co_return true;
      },
   } );

   REQUIRE( bothFuture.wait_for( std::chrono::seconds{ 3 } ) == std::future_status::ready );
   CHECK( countA >= 1 );
   CHECK( countB >= 1 );
}

TEST_CASE( "scheduler does not fire before interval elapses", "[scheduler]" )
{
   SchedulerDriver driver;

   std::atomic_int count{ 0 };

   driver.scheduler.addJob( util::PeriodicScheduler::Job{
      .name     = "slow",
      .interval = std::chrono::seconds{ 60 },
      .execute  = [ & ]() -> util::Task<bool>
      {
         ++count;
         co_return true;
      },
   } );

   std::this_thread::sleep_for( std::chrono::milliseconds{ 150 } );
   CHECK( count == 0 );
}

TEST_CASE( "scheduler can add job after start", "[scheduler]" )
{
   SchedulerDriver driver;

   std::atomic_int    count{ 0 };
   std::promise<void> fired;
   auto               firedFuture = fired.get_future();
   bool               promiseSet{ false };

   // Deliberately add the job after the scheduler is already running.
   std::this_thread::sleep_for( std::chrono::milliseconds{ 20 } );

   driver.scheduler.addJob( util::PeriodicScheduler::Job{
      .name     = "late_job",
      .interval = std::chrono::milliseconds{ 40 },
      .execute  = [ & ]() -> util::Task<bool>
      {
         ++count;
         if( ! promiseSet )
         {
            promiseSet = true;
            fired.set_value();
         }
         co_return true;
      },
   } );

   REQUIRE( firedFuture.wait_for( std::chrono::seconds{ 2 } ) == std::future_status::ready );
   CHECK( count >= 1 );
}
