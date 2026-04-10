#pragma once

#include "pace/Config.hpp"
#include "util/Task.hpp"
#include "util/expected.hpp"

#include <coroutine>
#include <exception>
#include <fmt/format.h>
#include <mqtt/async_client.h>
#include <mqtt/message.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <map>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <type_traits>
#include <utility>

namespace util
{
   class AsyncTaskDispatcher;
}

namespace pace
{
   /// @brief By now its a wrapper around pahe ... so consider renaming?
   class MqttService
   {
      public:

         using MessageHandler = std::function<util::Task<bool>( mqtt::const_message_ptr )>;

         explicit MqttService( Config cfg, util::AsyncTaskDispatcher& dispatcher );

         template <typename Handler>
            requires( ! std::is_invocable_v<Handler, mqtt::const_message_ptr> )
         util::Task<bool> subscribe( const std::string& topic, Handler&& handler )
         {
            return subscribe( topic, std::function( std::forward<Handler>( handler ) ) );
         }
         template <typename Payload>
         util::Task<bool> subscribe( const std::string&                                                          topic,
                                     std::function<util::Task<bool>( const std::string& topic, const Payload& )> handler )
         {
            auto deserializer = [ handler = std::move( handler ) ]( mqtt::const_message_ptr msg ) -> util::Task<bool>
            {
               if( auto body = deserializePayload<Payload>( msg ); body )
               {
                  co_await handler( msg->get_topic(), *body );
               }
               else
               {
                  spdlog::info( "Invalid Payload: {}", body.error() );
                  // co_return co_await error( body.error() );
               }
            };
            return subscribe( topic, std::move( deserializer ) );
         }
         util::Task<bool> subscribe( const std::string& topic, std::function<util::Task<bool>( const std::string& topic )> handler )
         {
            auto wrapper = [ handler = std::move( handler ) ]( mqtt::const_message_ptr msg ) -> util::Task<bool>
            {
               co_await handler( msg->get_topic() );
               co_return true;
            };
            return subscribe( topic, std::move( wrapper ) );
         }
         util::Task<bool> subscribe( const std::string& topic, MessageHandler handler );
         util::Task<bool> unsubscribe( const std::string& topic );

         template <typename Payload>
         util::Task<bool> publish( const std::string& topic, const Payload& payload )
         {
            const auto json = nlohmann::json( payload ).dump();
            return publish( topic, std::move( json ), false );
         }
         template <typename Payload>
         util::Task<bool> publish( const std::string& topic, const Payload& payload, bool retained )
         {
            const auto json = nlohmann::json( payload ).dump();
            return publish( topic, std::move( json ), retained );
         }
         /**
          * TODO: Can this be a const reference? -> getting bad alocs if it is
          * probably needs to be a reference because of coroutine frame
          */
         util::Task<bool> publish( const std::string& topic, std::string payload );
         util::Task<bool> publish( const std::string& topic, std::string payload, bool retained );


         util::Task<bool> connect();
         util::Task<bool> disconnect();

      private:

         void onMessage( const mqtt::const_message_ptr msg );

         template <typename Payload>
         static auto deserializePayload( mqtt::const_message_ptr msg ) -> util::expected<Payload, std::string>
         {
            try
            {
               const auto json    = nlohmann::json::parse( msg->get_payload() );
               const auto payload = json.get<Payload>();
               return payload;
            }
            catch( const std::exception& ex )
            {
               return util::unexpected{ fmt::format( "Failed to deserialize message payload: {}", ex.what() ) };
            }
         }

         Config                     config;
         mqtt::async_client         client;
         std::string                baseTopic;
         util::AsyncTaskDispatcher& dispatcher;

         std::map<std::string, MessageHandler> topicHandlers;
   };

} // namespace pace
