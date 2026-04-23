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

         /// @brief Ctor of the MQTT service
         /// @param cfg Process configuration containing MQTT connection details
         /// @param dispatcher Dispatcher to use which should work message handlers.
         explicit MqttService( Config cfg, util::AsyncTaskDispatcher& dispatcher );

         /// @brief Subscribes to a topic with a handler taking a deserialized payload
         /// @tparam Handler Function type of the handler, must be invocable with ( const std::string& topic, const Payload& payload )
         /// @param topic Topic to subscribe to
         /// @param handler Handler to process the deserialized payload
         /// @return
         template <typename Handler>
            requires( ! std::is_invocable_v<Handler, mqtt::const_message_ptr> )
         util::Task<bool> subscribe( const std::string& topic, Handler&& handler )
         {
            return subscribe( topic, std::function( std::forward<Handler>( handler ) ) );
         }

         /// @brief Subscribes to a topic with a handler taking the raw MQTT message, allowing for custom deserialization
         /// @tparam Payload Type of the payload to deserialize
         /// @param topic Topic to subscribe to
         /// @param handler Handler to process the deserialized payload
         /// @return
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

         /// @brief Subscribes to a topic with a handler taking the raw MQTT message, allowing for custom deserialization
         /// @param topic Topic to subscribe to
         /// @param handler Handler to process the raw MQTT message
         /// @return
         util::Task<bool> subscribe( const std::string& topic, MessageHandler handler );
         /// @brief Unsubscribes from a topic
         /// @param topic Topic to unsubscribe from
         /// @return
         util::Task<bool> unsubscribe( const std::string& topic );

         /// @brief Publishes a payload to a topic
         /// @tparam Payload Type of the payload to publish
         /// @param topic Topic to publish to
         /// @param payload Payload to publish; must be serializable to JSON via nlohmann::json
         /// @return Awaitable task
         template <typename Payload>
         util::Task<bool> publish( const std::string& topic, const Payload& payload, bool retained = false )
         {
            const auto json = nlohmann::json( payload ).dump();
            return publish( topic, std::move( json ), retained );
         }

         /// @brief Publishes a payload to a topic
         /// @param topic Topic to publish to
         /// @param payload Payload to publish
         /// @param retained Whether the message should be retained by the broker
         /// @return Awaitable task
         util::Task<bool> publish( const std::string& topic, std::string payload, bool retained = false );


         util::Task<bool> connect();
         util::Task<bool> disconnect();

      private:

         /// @brief Paho MQTT Client callback function for incoming messages
         /// @param msg Incomming message with metadata
         void onMessage( const mqtt::const_message_ptr msg );

         /// @brief Deserializes the payload of an MQTT message into the specified type, using nlohmann::json for deserialization
         /// @tparam Payload Type to deserialize the payload into; must be deserializable from JSON via nlohmann::json
         /// @param msg MQTT message containing the payload to deserialize
         /// @return Deserialized payload, or an error message if deserialization failed
         template <typename Payload>
         static auto deserializePayload( mqtt::const_message_ptr msg ) -> util::expected<Payload, std::string>
         {
            if constexpr( std::is_same_v<Payload, std::string> )
            {
               return msg->get_payload_str();
            }

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
