#pragma once

#include "pace/EntityInterface.hpp"
#include "pace/MqttService.hpp"

#include "util/Task.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <type_traits>


/// TODO: move into own class
namespace nlohmann
{
   template <>
   struct adl_serializer<std::chrono::milliseconds>
   {
         static void to_json( nlohmann::json& j, const std::chrono::milliseconds& ms )
         {
            j = ms.count();
         }

         static void from_json( const nlohmann::json& j, std::chrono::milliseconds& ms )
         {
            ms = std::chrono::milliseconds( j.get<long long>() );
         }
   };
}

namespace pace::sensors
{
   namespace config
   {
      struct BaseSensorConfig
      {
            std::string               name;
            std::chrono::milliseconds interval = std::chrono::milliseconds{ 5000 };
      };

      // Custom serialization for BaseSensorConfig: 'name' is required, 'interval' has a default
      inline void to_json( nlohmann::json& j, const BaseSensorConfig& cfg )
      {
         j[ "name" ]     = cfg.name;
         j[ "interval" ] = cfg.interval;
      }

      inline void from_json( const nlohmann::json& j, BaseSensorConfig& cfg )
      {
         cfg.name = j.at( "name" ).get<std::string>(); // required: throws if missing
         if( j.contains( "interval" ) )
         {
            cfg.interval = j[ "interval" ].get<std::chrono::milliseconds>();
         }
         // else: use default from struct definition (5000ms)
      }
   }

   /// @brief Template base class for typed sensors. Provides a default implementation of fetch_ that converts the typed data to string.
   /// @tparam T The type of the sensor data. The type must have a std::to_string overload.
   /// @tparam TConfig The config type for this sensor. Must derive from BaseSensorConfig.
   template <typename T, typename TConfig = config::BaseSensorConfig>
   class BaseSensor : public entities::EntityInterface
   {
         static_assert( std::is_base_of_v<config::BaseSensorConfig, TConfig>, "TConfig must derive from BaseSensorConfig" );

      public:

         explicit BaseSensor( MqttService& mqttService, const TConfig& cfg )
            : entities::EntityInterface( mqttService )
            , config( cfg )
         {}

         /// @brief Name of the entity. Used to construct MQTT topics.
         /// @return Entity name (e.g., "lock", "count", "ping_status")
         std::string name() const override
         {
            return config.name;
         }

         /// @brief Get entity type based on data type T
         /// Binary sensor if T is bool; regular sensor otherwise
         entities::EntityType type() const override
         {
            if constexpr( std::is_same_v<T, bool> )
            {
               return entities::EntityType::BinarySensor;
            }
            else
            {
               return entities::EntityType::Sensor;
            }
         }

         util::Task<bool> fetchAndPublish()
         {
            std::string data;
            if constexpr( std::is_same_v<T, std::string> )
            {
               data = co_await fetch();
            }
            else if constexpr( std::is_arithmetic_v<T> )
            {
               data = std::to_string( co_await fetch() );
            }

            // Debounce data to avoid flooding mqtt with unchanged values
            // But publish once in a while for newly connected clients.
            if( debounce < MAX_DEBOUNCE && data == lastData )
            {
               ++debounce;
               co_return false;
            }
            lastData = std::move( data );
            debounce = 0;

            co_await mqtt.publish( stateTopic(), lastData );
            co_return true;
         }

         /// @brief Fetch the sensor data. This is the main function that derived sensors
         /// need to implement to provide their specific data fetching logic.
         /// @return The fetched sensor data as the specific type T.
         virtual util::Task<T> fetch() const = 0;

         std::optional<std::chrono::milliseconds> pollingInterval() const override
         {
            return config.interval;
         }

         util::Task<bool> poll() override
         {
            co_return co_await fetchAndPublish();
         }


      protected:

         TConfig config;

      private:

         static constexpr int32_t MAX_DEBOUNCE = 5;

         /// @brief Cache the last published data to implement debounce logic
         std::string lastData;
         /// @brief Counter to track how many times the same data has been returned by fetch_ to implement debounce logic
         /// TODO: Add Reset Command to reset debounce?
         int32_t debounce = 0;
   };
} // namespace pace::sensors
