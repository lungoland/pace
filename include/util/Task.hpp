#pragma once

#include "util/Executor.hpp"

#include <spdlog/spdlog.h>

#include <concepts>
#include <condition_variable>
#include <coroutine>
#include <exception>
#include <memory>
#include <mutex>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>

/// https://blog.eiler.eu/posts/20210512/

namespace util
{
   template <typename T>
   class Task
   {
      public:

         struct task_promise;
         using promise_type = task_promise;
         using task_handle  = std::coroutine_handle<promise_type>;

         struct final_awaiter
         {
               bool await_ready() noexcept
               {
                  return false;
               }

               void await_suspend( task_handle h ) noexcept
               {
                  auto& p = h.promise();
                  {
                     std::lock_guard lock{ p.mutex };
                     p.done = true;
                  }
                  p.cv.notify_all();

                  if( ! p.continuation )
                  {
                     return;
                  }

                  if( p.executor )
                  {
                     p.executor->post( p.continuation );
                     return;
                  }

                  p.continuation.resume();
               }

               void await_resume() noexcept
               {}
         };

         struct task_promise
         {
               auto get_return_object()
               {
                  return Task{ task_handle::from_promise( *this ) };
               }
               auto initial_suspend() noexcept
               {
                  /// Should suspend immediatly to allow the caller to set the executor before the task starts running
                  /// caller will invoke await_suspend and pass the executor to the promise before resuming the task
                  return std::suspend_always{};
               }
               auto final_suspend() noexcept
               {
                  return final_awaiter{};
               }

               // void return_void()
               // {}
               void return_value( T value )
               {
                  result = std::move( value );
               }
               void unhandled_exception()
               {
                  error = std::current_exception();
                  try
                  {
                     if( error )
                     {
                        std::rethrow_exception( error );
                     }
                     spdlog::error( "task unhandled exception: unknown" );
                  }
                  catch( const std::exception& ex )
                  {
                     spdlog::error( "task unhandled exception: {}", ex.what() );
                  }
                  catch( ... )
                  {
                     spdlog::error( "task unhandled exception: unknown non-std exception" );
                  }
               }


               std::shared_ptr<Executor> get_executor() const
               {
                  return executor;
               }
               void set_executor( std::shared_ptr<Executor> newExecutor )
               {
                  executor = std::move( newExecutor );
               }

               std::shared_ptr<Executor> executor{};
               std::coroutine_handle<>   continuation{};
               std::exception_ptr        error{};
               std::mutex                mutex;
               std::condition_variable   cv;
               bool                      done{ false };
               bool                      started{ false };
               T                         result{};
         };

         explicit Task( task_handle h )
            : handle( h )
         {}
         Task( const Task& other ) = delete;
         Task( Task&& other ) noexcept
            : handle( std::exchange( other.handle, {} ) )
         {}

         Task& operator=( const Task& other ) = delete;
         Task& operator=( Task&& other ) noexcept
         {
            if( this != &other )
            {
               if( handle )
               {
                  handle.destroy();
               }
               handle = std::exchange( other.handle, {} );
            }
            return *this;
         }

         ~Task()
         {
            if( handle )
            {
               handle.destroy();
            }
         }

         bool await_ready() const noexcept
         {
            return ! handle || handle.done();
         }
         T await_resume()
         {
            if( handle && handle.promise().error )
            {
               std::rethrow_exception( handle.promise().error );
            }
            return std::move( handle.promise().result );
         }
         template </*Concept*/ typename Promise>
         std::coroutine_handle<> await_suspend( std::coroutine_handle<Promise> h ) noexcept
         {
            /// h is the handle of the caller aka the continunation
            /// handle is the handle of the callee aka the task being co_awaited
            if( ! handle )
            {
               return std::noop_coroutine();
            }

            auto& promise        = handle.promise();
            promise.continuation = h;
            promise.started      = true;

            if constexpr( requires( Promise& p ) { p.get_executor(); } )
            {
               promise.set_executor( h.promise().get_executor() );
            }

            return handle;
         }

         task_handle handle;
   };

   template <typename T>
   inline void sync_wait( Task<T>&& task, std::shared_ptr<Executor> executor = std::make_shared<Executor>() )
   {
      if( ! task.handle )
      {
         return;
      }

      auto& promise = task.handle.promise();
      promise.set_executor( std::move( executor ) );

      if( ! promise.started )
      {
         promise.started = true;
         task.handle.resume();
      }

      while( true )
      {
         {
            std::unique_lock lock{ promise.mutex };
            if( promise.done )
            {
               break;
            }
         }

         if( promise.executor )
         {
            promise.executor->run_one();
            continue;
         }

         std::unique_lock lock{ promise.mutex };
         promise.cv.wait( lock, [ &promise ] { return promise.done; } );
      }

      if( promise.error )
      {
         std::rethrow_exception( promise.error );
      }
   }

   template <typename... Ts>
   inline Task<std::tuple<Ts...>> when_all( Task<Ts>&&... tasks )
   {
      co_return std::tuple<Ts...>{ co_await std::move( tasks )... };
   }

   template <typename... Ts>
      requires( std::same_as<Ts, bool> && ... )
   inline Task<bool> all( Task<Ts>&&... tasks )
   {
      co_return ( co_await std::move( tasks ) && ... );
   }

   template <std::ranges::input_range R>
      requires( std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<R>>, Task<bool>>
                && ! std::is_const_v<std::remove_reference_t<std::ranges::range_reference_t<R>>> )
   inline Task<bool> all( R&& tasks )
   {
      for( auto&& task : tasks )
      {
         if( ! co_await std::move( task ) )
         {
            co_return false;
         }
      }

      co_return true;
   }

} // namespace util
