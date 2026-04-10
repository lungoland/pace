#pragma once

#include "util/Executor.hpp"

#include <mqtt/delivery_token.h>
#include <mqtt/iaction_listener.h>
#include <mqtt/token.h>

#include <atomic>
#include <coroutine>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

/**
 * dont like the mqtt namespace here ...
 */
namespace mqtt
{
   struct token_awaiter : iaction_listener
   {
         explicit token_awaiter( token_ptr tokenPtr )
            : token( std::move( tokenPtr ) )
         {}

         explicit token_awaiter( delivery_token_ptr tokenPtr )
            : token( std::move( tokenPtr ) )
         {}

         ~token_awaiter() noexcept override = default;

         bool await_ready() const noexcept
         {
            return ! token || token->is_complete();
         }

         template <typename Promise>
         bool await_suspend( std::coroutine_handle<Promise> h )
         {
            if( ! token )
            {
               return false;
            }

            continuation = h;
            if constexpr( requires( Promise& p ) { p.get_executor(); } )
            {
               executor = h.promise().get_executor();
            }

            token->set_action_callback( *this );

            if( token->is_complete() || completed.load( std::memory_order_acquire ) )
            {
               return false;
            }

            suspended.store( true, std::memory_order_release );

            if( token->is_complete() || completed.load( std::memory_order_acquire ) )
            {
               schedule_resume();
            }

            return ! completed.load( std::memory_order_acquire ) && ! token->is_complete();
         }

         void await_resume()
         {
            if( ! token )
            {
               return;
            }

            if( failed || token->get_return_code() != 0 )
            {
               const int  rc          = token->get_return_code();
               auto       detail      = token->get_error_message();
               const auto fallback    = mqtt::exception::error_str( rc );
               const bool hasDetail   = ! detail.empty();
               const bool hasFallback = ! fallback.empty();

               if( ! hasDetail && hasFallback )
               {
                  detail = fallback;
               }

               if( ! hasDetail && ! hasFallback )
               {
                  detail = "unknown";
               }

               spdlog::error( "mqtt async action failed with error code {}: {}", rc, detail );
               throw std::runtime_error( "mqtt async action failed (rc=" + std::to_string( rc ) + "): " + detail );
            }
         }

         void on_failure( const token& ) override
         {
            failed.store( true, std::memory_order_release );
            completed.store( true, std::memory_order_release );
            schedule_resume();
         }

         void on_success( const token& ) override
         {
            completed.store( true, std::memory_order_release );
            schedule_resume();
         }

      private:

         void schedule_resume()
         {
            if( ! suspended.load( std::memory_order_acquire ) )
            {
               return;
            }

            if( resumed.exchange( true, std::memory_order_acq_rel ) || ! continuation )
            {
               return;
            }

            if( executor )
            {
               executor->post( continuation );
               return;
            }

            continuation.resume();
         }

         mqtt::token_ptr                 token;
         std::coroutine_handle<>         continuation{};
         std::shared_ptr<util::Executor> executor{};
         std::atomic_bool                completed{ false };
         std::atomic_bool                failed{ false };
         std::atomic_bool                suspended{ false };
         std::atomic_bool                resumed{ false };
   };

   inline token_awaiter operator co_await( token_ptr token )
   {
      return token_awaiter{ std::move( token ) };
   }

   inline token_awaiter operator co_await( delivery_token_ptr token )
   {
      return token_awaiter{ std::move( token ) };
   }

} // namespace mqtt
