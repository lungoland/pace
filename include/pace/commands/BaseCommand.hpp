#pragma once

#include "pace/EntityInterface.hpp"
#include "pace/MqttService.hpp"

#include "util/Task.hpp"
#include "util/expected.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

namespace pace::commands
{
   using NoResponse = entities::NoResponse;
   using NoArgs     = entities::NoArgs;

   template <typename TResponse = NoResponse, typename TRequest = NoArgs>
   class BaseCommand : public entities::EntityInterface
   {
      public:

         using EntityInterface::EntityInterface;
         using ResponseType = util::expected<TResponse, std::string>;
         using DataType     = std::optional<std::string>;

         /// @brief Type of this entity.
         /// @return EntityType indicating whether this is a button, sensor, switch, etc.
         virtual entities::EntityType type() const
         {
            return entities::EntityType::Button;
         }

         /// @brief Subscribe to MQTT topics and set up handlers for this entity
         /// @return Task that completes when subscription is successful
         util::Task<bool> subscribe() override
         {
            return mqtt.subscribe( commandTopic(),
                                   [ this ]( mqtt::const_message_ptr msg ) -> util::Task<bool>
                                   {
                                      auto param    = entities::parsePayload<TRequest>( msg->get_payload_str() );
                                      auto response = co_await execute( std::move( param ) );

                                      if( ! response )
                                      {
                                         spdlog::error( "Command {} execution failed: {}", name(), response.error() );
                                         co_return false;
                                      }

                                      /// TODO: Raw string topic
                                      co_await mqtt.publish( fmt::format( "command/{}/status", name() ),
                                                             entities::stringifyResponse( *response ) );
                                      co_return true;
                                   } );
         }

         util::Task<bool> unsubscribe() override
         {
            return mqtt.unsubscribe( commandTopic() );
         }

         virtual util::Task<ResponseType> execute( TRequest request ) const = 0;
   };
} // namespace pace::commands
