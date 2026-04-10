#pragma once

#include "util/Executor.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace util
{
   class TimerAwaiter
   {
      public:

         explicit TimerAwaiter( std::chrono::milliseconds duration, std::stop_token stopToken = {} )
            : delayDuration( duration )
            , externalStopToken( stopToken )
         {}

         bool await_ready() const noexcept
         {
            return delayDuration <= std::chrono::milliseconds{ 0 } || externalStopToken.stop_requested();
         }

         template <typename Promise>
         bool await_suspend( std::coroutine_handle<Promise> h )
         {
            requestState               = std::make_shared<Request>();
            requestState->continuation = h;

            if constexpr( requires( Promise& p ) { p.get_executor(); } )
            {
               requestState->executor = h.promise().get_executor();
            }

            if( externalStopToken.stop_requested() )
            {
               return false;
            }

            if( externalStopToken.stop_possible() )
            {
               const auto weakRequest = std::weak_ptr<Request>{ requestState };
               stopCallback.emplace( externalStopToken,
                                     std::function<void()>{ [ weakRequest ]() { scheduler().resume_if_pending( weakRequest ); } } );
            }

            scheduler().schedule( requestState, delayDuration );

            return true;
         }

         void await_resume() noexcept
         {}

         ~TimerAwaiter()
         {
            if( requestState )
            {
               scheduler().resume_if_pending( std::weak_ptr<Request>{ requestState } );
            }
         }

      private:

         struct Request
         {
               std::coroutine_handle<>   continuation{};
               std::shared_ptr<Executor> executor{};
               bool                      completed{ false };
         };

         struct ScheduledRequest
         {
               std::chrono::steady_clock::time_point deadline{};
               std::uint64_t                         id{ 0 };
               std::shared_ptr<Request>              request{};
         };

         struct ScheduledRequestLater
         {
               bool operator()( const ScheduledRequest& lhs, const ScheduledRequest& rhs ) const noexcept
               {
                  if( lhs.deadline == rhs.deadline )
                  {
                     return lhs.id > rhs.id;
                  }
                  return lhs.deadline > rhs.deadline;
               }
         };

         class TimerScheduler
         {
            public:

               TimerScheduler()
                  : worker( [ this ]( std::stop_token stopToken ) { run( stopToken ); } )
               {}

               ~TimerScheduler()
               {
                  {
                     std::lock_guard lock{ mutex };
                     shuttingDown = true;
                  }
                  cv.notify_all();
               }

               void schedule( std::shared_ptr<Request> scheduledRequest, std::chrono::milliseconds scheduledDelay )
               {
                  const auto clampedDelay = std::max( scheduledDelay, std::chrono::milliseconds{ 0 } );

                  {
                     std::lock_guard lock{ mutex };
                     queue.push( ScheduledRequest{ .deadline = std::chrono::steady_clock::now() + clampedDelay,
                                                   .id       = nextId++,
                                                   .request  = std::move( scheduledRequest ) } );
                  }

                  cv.notify_all();
               }

               void resume_if_pending( const std::weak_ptr<Request>& weakRequest )
               {
                  auto pendingRequest = weakRequest.lock();
                  if( ! pendingRequest )
                  {
                     return;
                  }

                  std::coroutine_handle<>   continuation;
                  std::shared_ptr<Executor> executor;

                  {
                     std::lock_guard lock{ mutex };
                     if( pendingRequest->completed || ! pendingRequest->continuation )
                     {
                        return;
                     }

                     pendingRequest->completed    = true;
                     continuation                 = pendingRequest->continuation;
                     executor                     = pendingRequest->executor;
                     pendingRequest->continuation = {};
                  }

                  if( executor )
                  {
                     executor->post( continuation );
                     return;
                  }

                  continuation.resume();
               }

            private:

               void run( const std::stop_token& stopToken )
               {
                  std::unique_lock lock{ mutex };

                  while( ! stopToken.stop_requested() && ! shuttingDown )
                  {
                     if( queue.empty() )
                     {
                        cv.wait( lock, [ this, &stopToken ] { return stopToken.stop_requested() || shuttingDown || ! queue.empty(); } );
                        continue;
                     }

                     const auto deadline = queue.top().deadline;
                     cv.wait_until( lock, deadline, [ this, &stopToken ] { return stopToken.stop_requested() || shuttingDown; } );

                     if( stopToken.stop_requested() || shuttingDown )
                     {
                        break;
                     }

                     const auto now = std::chrono::steady_clock::now();
                     while( ! queue.empty() && queue.top().deadline <= now )
                     {
                        auto scheduled = queue.top();
                        queue.pop();
                        auto pendingRequest = std::move( scheduled.request );

                        if( ! pendingRequest || pendingRequest->completed || ! pendingRequest->continuation )
                        {
                           continue;
                        }

                        pendingRequest->completed    = true;
                        auto continuation            = pendingRequest->continuation;
                        auto executor                = pendingRequest->executor;
                        pendingRequest->continuation = {};

                        lock.unlock();
                        if( executor )
                        {
                           executor->post( continuation );
                        }
                        else
                        {
                           continuation.resume();
                        }
                        lock.lock();
                     }
                  }
               }

               std::mutex                                                                                  mutex;
               std::condition_variable                                                                     cv;
               std::priority_queue<ScheduledRequest, std::vector<ScheduledRequest>, ScheduledRequestLater> queue;
               std::jthread                                                                                worker;
               std::uint64_t                                                                               nextId{ 0 };
               bool                                                                                        shuttingDown{ false };
         };

         static TimerScheduler& scheduler()
         {
            static TimerScheduler sharedScheduler;
            return sharedScheduler;
         }

         std::chrono::milliseconds                                delayDuration;
         std::stop_token                                          externalStopToken;
         std::shared_ptr<Request>                                 requestState{};
         std::optional<std::stop_callback<std::function<void()>>> stopCallback{};
   };

   template <typename Rep, typename Period>
   inline TimerAwaiter sleep_for( std::chrono::duration<Rep, Period> duration, std::stop_token stopToken = {} )
   {
      return TimerAwaiter{ std::chrono::duration_cast<std::chrono::milliseconds>( duration ), stopToken };
   }

} // namespace util