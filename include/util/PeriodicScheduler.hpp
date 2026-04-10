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
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

namespace util
{
   class PeriodicScheduler
   {
      public:

         struct Job
         {
               std::string                 name;
               std::chrono::milliseconds   interval{};
               std::function<Task<bool>()> execute;
         };

         explicit PeriodicScheduler( std::vector<Job> configuredJobs )
            : jobs( std::move( configuredJobs ) )
            , worker( [ this ]( std::stop_token stopToken ) { run( stopToken ); } )
         {}

         ~PeriodicScheduler()
         {
            sync_wait( stop() );
         }

         PeriodicScheduler( const PeriodicScheduler& )            = delete;
         PeriodicScheduler& operator=( const PeriodicScheduler& ) = delete;
         PeriodicScheduler( PeriodicScheduler&& )                 = delete;
         PeriodicScheduler& operator=( PeriodicScheduler&& )      = delete;

         Task<bool> stop()
         {
            std::call_once( stopOnce,
                            [ this ]
                            {
                               if( ! worker.joinable() )
                               {
                                  return;
                               }

                               worker.request_stop();
                               worker.join();
                            } );

            co_return true;
         }

      private:

         void run( std::stop_token stopToken ) const
         {
            sync_wait(
               [ this, stopToken ]() -> Task<bool>
               {
                  using Clock = std::chrono::steady_clock;

                  struct JobSchedule
                  {
                        const Job*                job{ nullptr };
                        std::chrono::milliseconds interval{};
                        Clock::time_point         nextDue{};
                  };

                  std::vector<JobSchedule> schedules;
                  schedules.reserve( jobs.size() );

                  const auto start = Clock::now();
                  for( const auto& job : jobs )
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
                        .nextDue  = start + interval,
                     } );
                  }

                  while( ! stopToken.stop_requested() )
                  {
                     if( schedules.empty() )
                     {
                        co_await sleep_for( std::chrono::seconds{ 1 }, stopToken );
                        continue;
                     }

                     const auto nextIt = std::min_element( schedules.begin(), schedules.end(),
                                                           []( const JobSchedule& lhs, const JobSchedule& rhs )
                                                           { return lhs.nextDue < rhs.nextDue; } );

                     const auto now = Clock::now();
                     if( nextIt != schedules.end() && nextIt->nextDue > now )
                     {
                        co_await sleep_for( nextIt->nextDue - now, stopToken );
                     }

                     if( stopToken.stop_requested() )
                     {
                        break;
                     }

                     const auto              fireTime = Clock::now();
                     std::vector<Task<bool>> dueWork;

                     for( auto& entry : schedules )
                     {
                        if( entry.nextDue > fireTime )
                        {
                           continue;
                        }

                        dueWork.emplace_back( entry.job->execute() );
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

         std::vector<Job> jobs;
         std::jthread     worker{};
         std::once_flag   stopOnce{};
   };

} // namespace util
