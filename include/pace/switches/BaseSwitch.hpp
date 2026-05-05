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
   template <typename TState, typename TConfig = entities::config::EntityConfig>
   class BaseSwitch : public entities::EntityInterface
   {
         static_assert( std::is_base_of_v<entities::config::EntityConfig, TConfig>, "TConfig must derive from EntityConfig" );

      public:

         using ResponseType = util::expected<TState, std::string>;

         explicit BaseSwitch( MqttService& mqttService, const TConfig& cfg )
            : entities::EntityInterface( mqttService )
            , config( cfg )
         {}

         /// @brief Name of the entity. Used to construct MQTT topics.
         /// @return Entity name (e.g., "lock", "count", "ping_status")
         std::string name() const override
         {
            return config.name;
         }

         /// @brief Entity type is always Switch
         entities::EntityType type() const final
         {
            return entities::EntityType::Switch;
         }

         /// @brief Subscribe to command topic and set up handler
         util::Task<bool> subscribe() override
         {
            return mqtt.subscribe( commandTopic(),
                                   [ this ]( mqtt::const_message_ptr msg ) -> util::Task<bool>
                                   {
                                      auto param    = entities::parsePayload<TState>( msg->get_payload_str() );
                                      auto response = co_await execute( std::move( param ) );

                                      if( ! response )
                                      {
                                         spdlog::error( "Command {} execution failed: {}", name(), response.error() );
                                         co_return false;
                                      }

                                      co_await mqtt.publish( stateTopic(), entities::stringifyResponse( *response ) );
                                      co_return true;
                                   } );
         }

         util::Task<bool> unsubscribe() override
         {
            co_return co_await mqtt.unsubscribe( commandTopic() );
         }

         std::optional<std::chrono::milliseconds> pollingInterval() const override
         {
            return config.interval;
         }

         util::Task<bool> poll() override
         {
            auto data = co_await fetch();

            // Debounce data to avoid flooding mqtt with unchanged values
            // But publish once in a while for newly connected clients.
            if( debounce < MAX_DEBOUNCE && data == lastData )
            {
               ++debounce;
               co_return false;
            }
            debounce = 0;
            co_await mqtt.publish( stateTopic(), entities::stringifyResponse( data ) );
            co_return true;
         }

         virtual util::Task<TState>       fetch() const             = 0;
         virtual util::Task<ResponseType> execute( TState request ) = 0;

         /// @brief Get MQTT state topic for this switch
         /// Default: pace/switch/{name}/state
         std::string stateTopic() const override
         {
            return fmt::format( "switch/{}/state", name() );
         }

         /// @brief Get MQTT command topic for this switch
         /// Default: pace/switch/{name}/set
         std::string commandTopic() const override
         {
            return fmt::format( "switch/{}/set", name() );
         }

      protected:

         TConfig config;

      private:

         static constexpr int32_t MAX_DEBOUNCE = 5;

         /// @brief Cache the last published data to implement debounce logic
         bool lastData;
         /// @brief Counter to track how many times the same data has been returned by fetch_ to implement debounce logic
         /// TODO: Add Reset Command to reset debounce?
         int32_t debounce = 0;
   };

} // namespace pace::switches
