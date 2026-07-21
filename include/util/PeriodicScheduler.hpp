#pragma once

#include "util/AsyncTaskDispatcher.hpp"
#include "util/Executor.hpp"
#include "util/Logger.hpp"
#include "util/Task.hpp"
#include "util/TimerAwaiter.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace util
{
   /// @brief A scheduler for managing periodic execution of tasks.
   /// TODO: create cpp file .. i guess
   class PeriodicScheduler
   {
      public:

         struct Job
         {
               std::string                 name{};
               std::chrono::milliseconds   interval{};
               std::function<Task<void>()> execute{};
         };

         explicit PeriodicScheduler( std::vector<Job> configuredJobs = {} )
            : jobs( std::move( configuredJobs ) )
         {}
         PeriodicScheduler( const PeriodicScheduler& )            = delete;
         PeriodicScheduler& operator=( const PeriodicScheduler& ) = delete;
         PeriodicScheduler( PeriodicScheduler&& )                 = delete;
         PeriodicScheduler& operator=( PeriodicScheduler&& )      = delete;
         ~PeriodicScheduler()
         {
            stop();
         }

         /// @brief Starts the scheduler coroutine on the given executor.
         /// Must be called once before any jobs can fire. The coroutine runs
         /// cooperatively on @p executor alongside whatever drives that executor
         /// (e.g. sync_wait for the main AsyncTaskDispatcher).
         /// Due jobs are posted to @p dispatcher rather than awaited inline,
         /// so the scheduler loop is never blocked by sensor execution.
         void start( std::shared_ptr<Executor> executor, AsyncTaskDispatcher& disp )
         {
            std::call_once( startOnce,
                            [ this, exec = std::move( executor ), &disp ]() mutable
                            {
                               dispatcher = &disp;
                               schedulerTask.emplace( run() );
                               auto& p = schedulerTask->handle.promise();
                               p.set_executor( std::move( exec ) );
                               p.started = true;
                               schedulerTask->handle.resume();
                            } );
         }

         void addJob( Job job )
         {
            /// potential race condition with wake up and re-add job
            /// so we either add some id to the same name or sync somehow
            std::lock_guard lock{ mutex };
            logger->debug( "Add Job: {}", job.name );
            jobs.push_back( std::move( job ) );
            wakeSource.request_stop();
         }

         void removeJob( std::string_view name )
         {
            std::lock_guard lock{ mutex };
            logger->debug( "Remove Job: {}", name );
            std::erase_if( jobs, [ & ]( const Job& j ) { return j.name == name; } );
            wakeSource.request_stop();
         }

         /// @brief Async stop — signals the scheduler and co_awaits completion.
         /// Must be called from a coroutine running on the same executor as the scheduler
         /// (e.g. inside a Task dispatched on the AsyncTaskDispatcher).
         Task<void> stopAsync()
         {
            std::call_once( stopOnce, [ this ] { stopSource.request_stop(); } );
            if( schedulerTask.has_value() )
            {
               // request_stop() fires the TimerAwaiter stop-callback synchronously, which
               // posts the scheduler's coroutine handle to the executor before we reach this
               // co_await. A plain `co_await *schedulerTask_` would do a symmetric transfer
               // to that same handle, causing a double-resume (UB/crash). await_passively()
               // suspends without symmetric transfer, letting the executor's already-queued
               // handle be the sole thing that drives the scheduler to completion.
               co_await schedulerTask->await_passively();
            }
            co_return;
         }

         /// @brief Synchronous stop - blocks until the scheduler coroutine finishes.
         /// Safe to call from outside a coroutine context (e.g. the destructor).
         /// Do NOT call from the same executor thread that drives the scheduler.
         void stop()
         {
            std::call_once( stopOnce, [ this ] { stopSource.request_stop(); } );
            if( schedulerTask.has_value() )
            {
               auto&            p = schedulerTask->handle.promise();
               std::unique_lock lock{ p.mutex };
               p.cv.wait( lock, [ &p ] { return p.done; } );
            }
         }

      private:

         /// @brief Runs the scheduler loop as a coroutine on the caller's executor.
         /// @note stopSource_ must be signalled to exit the loop.
         /// TODO split run into smaller functions .. I think
         Task<void> run()
         {
            const std::stop_token stopToken = stopSource.get_token();
            try
            {
               std::vector<JobSchedule> schedules;
               while( ! stopToken.stop_requested() )
               {
                  std::stop_token wakeToken = syncSchedules( schedules );
                  if( schedules.empty() )
                  {
                     std::stop_source   combined;
                     std::stop_callback cb1( stopToken, [ &combined ] { combined.request_stop(); } );
                     std::stop_callback cb2( wakeToken, [ &combined ] { combined.request_stop(); } );
                     co_await sleep_for( std::chrono::seconds{ 1 }, combined.get_token() );
                     continue;
                  }

                  const auto nextIt = std::min_element( std::begin( schedules ), std::end( schedules ),
                                                        []( const JobSchedule& lhs, const JobSchedule& rhs )
                                                        { return lhs.nextDue < rhs.nextDue; } );

                  const auto now = Clock::now();
                  if( nextIt != std::end( schedules ) && nextIt->nextDue > now )
                  {
                     std::stop_source   combined;
                     std::stop_callback cb1( stopToken, [ &combined ] { combined.request_stop(); } );
                     std::stop_callback cb2( wakeToken, [ &combined ] { combined.request_stop(); } );
                     co_await sleep_for( nextIt->nextDue - now, combined.get_token() );
                  }

                  if( stopToken.stop_requested() )
                  {
                     logger->info( "Stopping..." );
                     break;
                  }

                  if( wakeToken.stop_requested() )
                  {
                     logger->trace( "Woke up early due to job changes, recalculating schedules..." );
                     continue;
                  }

                  const auto fireTime = Clock::now();

                  for( auto& entry : schedules )
                  {
                     if( entry.nextDue > fireTime )
                     {
                        continue;
                     }

                     dispatcher->post( entry.job->name, entry.execute );
                     do
                     {
                        entry.nextDue += entry.interval;
                     }
                     while( entry.nextDue <= fireTime );
                  }
               }
            }
            catch( const std::exception& ex )
            {
               logger->critical( "runtime error: {}", ex.what() );
               throw;
            }
            co_return;
         }

         using Clock = std::chrono::steady_clock;
         struct JobSchedule
         {
               const Job*                  job{ nullptr };
               std::chrono::milliseconds   interval{};
               Clock::time_point           nextDue{};
               std::function<Task<void>()> execute;
         };

         std::stop_token syncSchedules( std::vector<JobSchedule>& schedules )
         {
            std::lock_guard lock{ mutex };
            // Remove schedules whose jobs were deleted
            // If the job was re-added before this could run, it will not be detected and we will leak sensors!
            auto removed = std::erase_if(
               schedules, [ & ]( const JobSchedule& s )
               { return std::none_of( std::begin( jobs ), std::end( jobs ), [ & ]( const Job& j ) { return &j == s.job; } ); } );
            if( removed > 0 )
            {
               logger->trace( "Removed {} schedules for deleted jobs", removed );
            }

            // Add schedules for newly added jobs
            const auto now = Clock::now();
            for( const auto& job : jobs )
            {
               const bool exists = std::any_of( std::begin( schedules ), std::end( schedules ),
                                                [ & ]( const JobSchedule& s ) { return s.job == &job; } );
               if( ! exists )
               {
                  auto interval = job.interval;
                  if( interval <= std::chrono::milliseconds{ 0 } )
                  {
                     logger->warn( "Job '{}' reported non-positive interval ({}ms), clamping to 1s", job.name, interval.count() );
                     interval = std::chrono::seconds{ 1 };
                  }
                  schedules.push_back( JobSchedule{
                     .job      = &job,
                     .interval = interval,
                     .nextDue  = now + interval,
                     .execute  = job.execute,
                  } );
               }
            }

            // Reset wake signal for next sleep
            wakeSource = std::stop_source{};
            return wakeSource.get_token();
         }

         mutable util::Logger logger = util::getLogger( "Scheduler" );

         std::mutex                mutex;
         std::stop_source          stopSource{};
         std::stop_source          wakeSource{};
         std::vector<Job>          jobs{};
         std::optional<Task<void>> schedulerTask{};
         std::once_flag            stopOnce;
         std::once_flag            startOnce;
         AsyncTaskDispatcher*      dispatcher{ nullptr };
   };

} // namespace util
