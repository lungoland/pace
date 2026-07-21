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

   template <typename TDerived, typename TResponse = NoResponse, typename TRequest = NoArgs>
   class BaseCommand : public entities::EntityInterface
   {
      public:

         using EntityInterface::EntityInterface;
         using ResponseType = util::Result<TResponse>;
         using DataType     = std::optional<std::string>;

         /// @brief Type of this entity. For Command this usually is a button
         /// or text depending on whether the command takes arguments.
         [[nodiscard]] entities::EntityType type() const
         {
            if constexpr( std::is_same_v<TRequest, std::string> )
            {
               return entities::EntityType::Text;
            }
            else
            {
               return entities::EntityType::Button;
            }
         }

         /// @brief Subscribe to MQTT topics and set up handlers for this entity
         /// @return Task that completes when subscription is successful
         util::Task<entities::OperationResult> subscribe() override
         {
            return mqtt.subscribe(
               commandTopic(),
               [ this ]( mqtt::const_message_ptr msg ) -> util::Task<entities::OperationResult>
               {
                  auto param = entities::parsePayload<TRequest>( msg->get_payload_str() );
                  if( ! param )
                  {
                     co_return util::unexpected{ util::makeError( util::ErrorCode::InvalidPayload, "Invalid payload for '{}': {}", name(),
                                                                  param.error() ) };
                  }

                  auto response = co_await execute( std::move( *param ) );

                  if( ! response )
                  {
                     co_return util::unexpected{ util::makeError( util::ErrorCode::CommandFailure, "Command {} execution failed: {}",
                                                                  name(), response.error() ) };
                  }

                  /// TODO: Raw string topic
                  auto publishResult = co_await mqtt.publish( fmt::format( "command/{}/status", name() ),
                                                              entities::stringifyResponse( *response ) );
                  if( ! publishResult )
                  {
                     co_return util::unexpected{ util::makeError(
                        util::ErrorCode::PublishFailure, "Failed to publish command status for '{}': {}", name(), publishResult.error() ) };
                  }
                  co_return {};
               } );
         }

         util::Task<entities::OperationResult> unsubscribe() override
         {
            return mqtt.unsubscribe( commandTopic() );
         }

         /// @brief Performs the command
         /// @param request The command argument, if any. For commands without arguments, this will be NoArgs.
         /// @return The result of the command execution. For commands without a response, this will be NoResponse.
         virtual util::Task<ResponseType> execute( TRequest request ) const = 0;

      protected:

         util::Logger logger = util::getLogger( std::string( TDerived::kType ) );
   };
} // namespace pace::commands
