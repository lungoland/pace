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
   /// Sentinel type for commands that produce no response.
   struct NoResponse
   {};

   /// Sentinel type for commands that take no input payload.
   struct NoArgs
   {};

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
            return mqtt.subscribe( fmt::format( "command/{}/set", name() ),
                                   [ this ]( mqtt::const_message_ptr msg ) -> util::Task<bool>
                                   {
                                      auto param    = parsePayload( msg->get_payload_str() );
                                      auto response = co_await execute( std::move( param ) );

                                      if( ! response )
                                      {
                                         spdlog::error( "Command {} execution failed: {}", name(), response.error() );
                                         co_return false;
                                      }

                                      co_await mqtt.publish( fmt::format( "command/{}/status", name() ), stringifyResponse( *response ) );
                                      co_return true;
                                   } );
         }

         util::Task<bool> unsubscribe() override
         {
            return mqtt.unsubscribe( commandTopic() );
         }

         virtual util::Task<ResponseType> execute( TRequest request ) const = 0;

      private:

         static TRequest parsePayload( const std::string& payload )
         {
            if constexpr( std::same_as<TRequest, NoArgs> )
            {
               return NoArgs{};
            }
            else if constexpr( std::same_as<TRequest, std::string> )
            {
               return payload;
            }
            else
            {
               auto json = nlohmann::json::parse( payload, nullptr, false );
               if( json.is_discarded() )
               {
                  throw std::invalid_argument( "invalid JSON payload" );
               }
               return json.get<TRequest>();
            }
         }

         static std::optional<std::string> stringifyResponse( const TResponse& response )
         {
            if constexpr( std::same_as<TResponse, NoResponse> )
            {
               return std::nullopt;
            }
            else if constexpr( std::same_as<TResponse, std::string> )
            {
               return std::optional<std::string>{ response };
            }
            else
            {
               return std::optional<std::string>{ nlohmann::json( response ).dump() };
            }
         }
   };
} // namespace pace::commands
