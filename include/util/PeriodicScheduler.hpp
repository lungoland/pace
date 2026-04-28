#pragma once

#include "util/Task.hpp"
#include "util/TimerAwaiter.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

namespace util
{
   /// @brief A scheduler for managing periodic execution of tasks.
   /// TODO: create cpp file .. i guess
   class PeriodicScheduler
   {
      public:

         struct Job
         {
               std::string                 name;
               std::chrono::milliseconds   interval{};
               std::function<Task<bool>()> execute;
         };

         explicit PeriodicScheduler( std::vector<Job> configuredJobs = {} )
            : jobs_( std::move( configuredJobs ) )
            , worker_( [ this ]( std::stop_token stopToken ) { run( stopToken ); } )
         {}

         ~PeriodicScheduler()
         {
            stop();
         }

         PeriodicScheduler( const PeriodicScheduler& )            = delete;
         PeriodicScheduler& operator=( const PeriodicScheduler& ) = delete;
         PeriodicScheduler( PeriodicScheduler&& )                 = delete;
         PeriodicScheduler& operator=( PeriodicScheduler&& )      = delete;

         void addJob( Job job )
         {
            /// potential race condition with wake up and re-add job
            /// so we either add some id to the same name or sync somehow
            std::lock_guard lock{ mutex_ };
            spdlog::trace( "Add Job: {}", job.name );
            jobs_.push_back( std::move( job ) );
            wakeSource_.request_stop();
         }

         void removeJob( std::string_view name )
         {
            std::lock_guard lock{ mutex_ };
            spdlog::trace( "Remove Job: {}", name );
            std::erase_if( jobs_, [ & ]( const Job& j ) { return j.name == name; } );
            wakeSource_.request_stop();
         }

         void stop()
         {
            std::call_once( stopOnce_,
                            [ this ]
                            {
                               if( ! worker_.joinable() )
                               {
                                  return;
                               }

                               worker_.request_stop();
                               worker_.join();
                            } );
         }

      private:

         /// @brief Runs the scheduler loop.
         /// @param stopToken Token to signal stopping the scheduler.
         /// TODO split run into smaller functions .. I think
         void run( std::stop_token stopToken )
         {
            try
            {
               sync_wait(
                  [ this, stopToken ]() -> Task<bool>
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
                           spdlog::info( "PeriodicScheduler stopping..." );
                           break;
                        }

                        if( wakeToken.stop_requested() )
                        {
                           spdlog::trace( "PeriodicScheduler woke up early due to job changes, recalculating schedules..." );
                           continue;
                        }

                        const auto              fireTime = Clock::now();
                        std::vector<Task<bool>> dueWork;

                        for( auto& entry : schedules )
                        {
                           if( entry.nextDue > fireTime )
                           {
                              continue;
                           }

                           /// TODO: Could be a race condition if the job is removed after execute() but before
                           /// running task using co_await ...
                           dueWork.emplace_back( entry.execute() );
                           do
                           {
                              entry.nextDue += entry.interval;
                           }
                           while( entry.nextDue <= fireTime );
                        }

                        if( ! dueWork.empty() )
                        {
                           co_await all( std::move( dueWork ) );
                        }
                     }

                     co_return true;
                  }() );
            }
            catch( const std::exception& ex )
            {
               spdlog::error( "PeriodicScheduler encountered an exception: {}", ex.what() );
            }
            catch( ... )
            {
               spdlog::error( "PeriodicScheduler encountered an unknown non-std exception" );
            }
         }

         using Clock = std::chrono::steady_clock;
         struct JobSchedule
         {
               const Job*                  job{ nullptr };
               std::chrono::milliseconds   interval{};
               Clock::time_point           nextDue{};
               std::function<Task<bool>()> execute;
         };
         std::stop_token syncSchedules( std::vector<JobSchedule>& schedules )
         {
            std::lock_guard lock{ mutex_ };
            // Remove schedules whose jobs were deleted
            // If the job was re-added before this could run, it will not be detected and we will leak sensors!
            auto removed = std::erase_if(
               schedules, [ & ]( const JobSchedule& s )
               { return std::none_of( std::begin( jobs_ ), std::end( jobs_ ), [ & ]( const Job& j ) { return &j == s.job; } ); } );
            if( removed > 0 )
            {
               spdlog::trace( "Removed {} schedules for deleted jobs", removed );
            }

            // Add schedules for newly added jobs
            const auto now = Clock::now();
            for( const auto& job : jobs_ )
            {
               const bool exists = std::any_of( std::begin( schedules ), std::end( schedules ),
                                                [ & ]( const JobSchedule& s ) { return s.job == &job; } );
               if( ! exists )
               {
                  auto interval = job.interval;
                  if( interval <= std::chrono::milliseconds{ 0 } )
                  {
                     spdlog::warn( "Job '{}' reported non-positive interval ({}ms), clamping to 1s", job.name, interval.count() );
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
            wakeSource_ = std::stop_source{};
            return wakeSource_.get_token();
         }

         std::mutex       mutex_{};
         std::stop_source wakeSource_{};
         std::vector<Job> jobs_{};
         std::jthread     worker_{};
         std::once_flag   stopOnce_{};
   };

} // namespace util
