#pragma once

#include "pace/EntityInterface.hpp"
#include "pace/MqttService.hpp"
#include "pace/sensors/BaseSensor.hpp"
#include "util/Task.hpp"
#include "util/expected.hpp"

#include <fmt/format.h>
#include <mqtt/message.h>
#include <nlohmann/json.hpp>

#include <concepts>
#include <exception>
#include <optional>
#include <string>
#include <type_traits>


namespace pace::switches
{
   using NoResponse = entities::NoResponse;
   using NoArgs     = entities::NoArgs;

   /// @brief Base class for bidirectional switch entities
   ///
   /// Switches are Home Assistant entities that accept command input and maintain state.
   /// Unlike sensors (read-only), switches can be commanded to change state.
   /// Unlike stateless commands (buttons), switches maintain and report their current state.
   ///
   /// Examples: process start/stop, power state, dimmer level, etc.
   ///
   /// @tparam TState Type of the state value (e.g., bool for on/off, int for level)
   /// @tparam TConfig Type of the configuration struct (must derive from EntityConfig)
   template <typename TDerived, typename TState, typename TConfig = entities::config::EntityConfig>
   class BaseSwitch : public entities::EntityInterface
   {
         static_assert( std::is_base_of_v<entities::config::EntityConfig, TConfig>, "TConfig must derive from EntityConfig" );

      public:

         using ResponseType = util::Result<TState>;

         explicit BaseSwitch( MqttService& mqttService, const TConfig& cfg )
            : entities::EntityInterface( mqttService )
            , config( cfg )
         {}

         /// @brief Name of the entity. Used to construct MQTT topics.
         /// @return Entity name (e.g., "lock", "count", "ping_status")
         [[nodiscard]] std::string name() const override
         {
            return config.name;
         }

         /// @brief Entity type is always Switch
         [[nodiscard]] entities::EntityType type() const final
         {
            return entities::EntityType::Switch;
         }

         /// @brief Subscribe to command topic and set up handler
         util::Task<entities::OperationResult> subscribe() override
         {
            return mqtt.subscribe(
               commandTopic(),
               [ this ]( mqtt::const_message_ptr msg ) -> util::Task<entities::OperationResult>
               {
                  auto param = entities::parsePayload<TState>( msg->get_payload_str() );
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

                  if( auto publishResult = co_await mqtt.publish( stateTopic(), entities::stringifyResponse( *response ) ); ! publishResult )
                  {
                     co_return util::unexpected{ util::makeError(
                        util::ErrorCode::PublishFailure, "Failed to publish switch state for '{}': {}", name(), publishResult.error() ) };
                  }

                  co_return {};
               } );
         }

         util::Task<entities::OperationResult> unsubscribe() override
         {
            co_return co_await mqtt.unsubscribe( commandTopic() );
         }

         [[nodiscard]] std::optional<std::chrono::milliseconds> pollingInterval() const override
         {
            return config.interval;
         }

         util::Task<entities::OperationResult> poll() override
         {
            try
            {
               auto data = co_await fetch();

               // Debounce data to avoid flooding mqtt with unchanged values
               // But publish once in a while for newly connected clients.
               if( debounce < MAX_DEBOUNCE && data == lastData )
               {
                  ++debounce;
                  co_return {};
               }
               lastData = data;
               debounce = 0;
               if( auto publishResult = co_await mqtt.publish( stateTopic(), entities::stringifyResponse( data ) ); ! publishResult )
               {
                  co_return util::unexpected{ util::makeError(
                     util::ErrorCode::PublishFailure, "Failed to publish switch poll state for '{}': {}", name(), publishResult.error() ) };
               }
               co_return {};
            }
            catch( const std::exception& ex )
            {
               co_return util::unexpected{ util::makeError( util::ErrorCode::PollFailure, "Polling switch '{}' failed: {}", name(),
                                                            ex.what() ) };
            }
         }

         virtual util::Task<TState>       fetch() const             = 0;
         virtual util::Task<ResponseType> execute( TState request ) = 0;

         /// @brief Get MQTT state topic for this switch
         /// Default: pace/switch/{name}/state
         [[nodiscard]] std::string stateTopic() const override
         {
            return fmt::format( "switch/{}/state", name() );
         }

         /// @brief Get MQTT command topic for this switch
         /// Default: pace/switch/{name}/set
         [[nodiscard]] std::string commandTopic() const override
         {
            return fmt::format( "switch/{}/set", name() );
         }

      protected:

         util::Logger logger = util::getLogger( std::string{ TDerived::kType } );
         TConfig      config{};

      private:

         static constexpr int32_t MAX_DEBOUNCE = 5;

         /// @brief Cache the last published data to implement debounce logic
         TState lastData{};
         /// @brief Counter to track how many times the same data has been returned by fetch_ to implement debounce logic
         /// TODO: Add Reset Command to reset debounce?
         int32_t debounce{};
   };

} // namespace pace::switches
