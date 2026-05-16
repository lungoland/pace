#pragma once

#include "pace/EntityInterface.hpp"
#include "pace/MqttService.hpp"

#include "util/Task.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <type_traits>

namespace pace::sensors
{

   /// @brief Template base class for typed sensors. Provides a default implementation of fetch_ that converts the typed data to string.
   /// @tparam T The type of the sensor data. The type must have a std::to_string overload.
   /// @tparam TConfig The config type for this sensor. Must derive from EntityConfig.
   template <typename TDerived, typename TState, typename TConfig = entities::config::EntityConfig>
   class BaseSensor : public entities::EntityInterface
   {
         static_assert( std::is_base_of_v<entities::config::EntityConfig, TConfig>, "TConfig must derive from EntityConfig" );

      public:

         explicit BaseSensor( MqttService& mqttService, const TConfig& cfg )
            : entities::EntityInterface( mqttService )
            , config( cfg )
         {}

         /// @brief Name of the entity. Used to construct MQTT topics.
         /// @return Entity name (e.g., "lock", "count", "ping_status")
         [[nodiscard]] std::string name() const override
         {
            return config.name;
         }

         /// @brief Get entity type based on data type T
         /// Binary sensor if T is bool; regular sensor otherwise
         [[nodiscard]] entities::EntityType type() const override
         {
            if constexpr( std::is_same_v<TState, bool> )
            {
               return entities::EntityType::BinarySensor;
            }
            else
            {
               return entities::EntityType::Sensor;
            }
         }

         [[nodiscard]] std::optional<std::chrono::milliseconds> pollingInterval() const override
         {
            return config.interval;
         }

         util::Task<bool> poll() override
         {
            auto data = entities::stringifyResponse( co_await fetch() );

            // Debounce state to avoid flooding mqtt with unchanged values
            // But publish once in a while for newly connected clients.
            if( debounce < MAX_DEBOUNCE && data == lastData )
            {
               ++debounce;
            }
            else
            {
               lastData = data;
               debounce = 0;
               co_await mqtt.publish( stateTopic(), lastData );
            }

            // Independently debounce and publish attributes (e.g. process lists).
            if( auto attrs = getAttributes(); attrs.has_value() )
            {
               auto attrsStr = attrs->dump();
               if( attrsDebounce < MAX_DEBOUNCE && attrsStr == lastAttrs )
               {
                  ++attrsDebounce;
               }
               else
               {
                  lastAttrs     = attrsStr;
                  attrsDebounce = 0;
                  co_await mqtt.publish( attributesTopic(), lastAttrs );
               }
            }

            co_return true;
         }

         /// @brief Fetch the sensor data. This is the main function that derived sensors
         /// need to implement to provide their specific data fetching logic.
         /// @return The fetched sensor data as the specific type TState.
         virtual util::Task<TState> fetch() const = 0;

      protected:

         util::Logger logger = util::getLogger( std::string{ TDerived::kType } );
         TConfig      config{};

      private:

         static constexpr int32_t MAX_DEBOUNCE = 5;

         /// @brief Cache the last published state to implement debounce logic
         std::string lastData{};
         /// @brief Counter to track same-state repeats for state debounce
         int32_t debounce{};

         /// @brief Cache the last published attributes to implement attribute debounce
         std::string lastAttrs{};
         /// @brief Counter to track same-attribute repeats for attribute debounce
         int32_t attrsDebounce{};
   };
} // namespace pace::sensors
