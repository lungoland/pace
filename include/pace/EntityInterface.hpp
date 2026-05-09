#pragma once

#include "util/Task.hpp"

// #include <nlohmann/json_fwd.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace pace
{
   class MqttService;
}

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

namespace pace::entities
{
   namespace config
   {
      struct EntityConfig
      {
            std::string               name;
            std::chrono::milliseconds interval = std::chrono::milliseconds{ 5000 };
      };

      // Custom serialization for EntityConfig: 'name' is required, 'interval' has a default
      inline void to_json( nlohmann::json& j, const EntityConfig& cfg )
      {
         j[ "name" ]     = cfg.name;
         j[ "interval" ] = cfg.interval;
      }

      inline void from_json( const nlohmann::json& j, EntityConfig& cfg )
      {
         cfg.name = j.at( "name" ).get<std::string>(); // required: throws if missing
         if( j.contains( "interval" ) )
         {
            cfg.interval = j[ "interval" ].get<std::chrono::milliseconds>();
         }
         // else: use default from struct definition (5000ms)
      }
   }

   /// Sentinel type for entities that produce no response.
   struct NoResponse
   {};

   /// Sentinel type for entities that take no input payload.
   struct NoArgs
   {};

   template <typename TRequest>
   TRequest parsePayload( const std::string& payload )
   {
      if constexpr( std::same_as<TRequest, NoArgs> )
      {
         return NoArgs{};
      }
      else if constexpr( std::same_as<TRequest, std::string> )
      {
         return payload;
      }
      else if constexpr( std::same_as<TRequest, bool> )
      {
         if( payload == "on" || payload == "true" )
         {
            return true;
         }
         else if( payload == "off" || payload == "false" )
         {
            return false;
         }
         else
         {
            throw std::invalid_argument( "invalid boolean payload: expected 'on', 'off', 'true', or 'false'" );
         }
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

   template <typename TResponse>
   std::string stringifyResponse( const TResponse& response )
   {
      if constexpr( std::same_as<TResponse, NoResponse> )
      {
         return "";
      }
      else if constexpr( std::same_as<TResponse, bool> )
      {
         return response ? "on" : "off";
      }
      else if constexpr( std::same_as<TResponse, std::string> )
      {
         return response;
      }
      else
      {
         return nlohmann::json( response ).dump();
      }
   }


   /// @brief Enumeration of Home Assistant entity types
   enum class EntityType
   {
      Button,       ///< Stateless trigger entity (e.g., lock, reboot)
      Text,         ///< Stateless trigger with payload (e.g. kill)
      Sensor,       ///< Read-only polled state with generic type
      BinarySensor, ///< Read-only polled boolean state
      Notify,       ///< One-way notification trigger
      Switch        ///< Bidirectional state + command entity
   };

   /// @brief Unified interface for all Home Assistant entities
   ///
   /// This is the base abstraction for all entity types in PACE.
   /// It replaces the parallel CommandInterface/SensorInterface hierarchy
   /// with a single, extensible interface aligned with Home Assistant's entity model.
   ///
   /// All entity types (button, sensor, binary_sensor, notify, switch) implement this interface.
   class EntityInterface
   {
      public:

         explicit EntityInterface( MqttService& mqttService );
         virtual ~EntityInterface() = default;

         /// @brief Name of the entity. Used to construct MQTT topics.
         /// @return Entity name (e.g., "lock", "count", "ping_status")
         virtual std::string name() const = 0;

         /// @brief Type of this entity.
         /// @return EntityType indicating whether this is a button, sensor, switch, etc.
         virtual EntityType type() const = 0;

         /// @brief Subscribe to MQTT topics and set up handlers for this entity
         /// @return Task that completes when subscription is successful
         virtual util::Task<bool> subscribe();

         /// @brief Tear down MQTT subscriptions for this entity.
         /// @return Task that completes when teardown is successful.
         virtual util::Task<bool> unsubscribe();

         /// @brief Optional polling interval for entities that need periodic execution.
         /// @return Interval when polling is supported, std::nullopt otherwise.
         virtual std::optional<std::chrono::milliseconds> pollingInterval() const;

         /// @brief Periodic work callback for polled entities.
         /// @return true when work succeeds.
         virtual util::Task<bool> poll();

         /// @brief Publish MQTT Discovery payload for Home Assistant auto-discovery
         /// @return JSON payload in Home Assistant MQTT Discovery format, or std::nullopt if discovery not supported
         virtual nlohmann::json getDiscoveryPayload() const;

         /// @brief Optional JSON attributes for this entity, published alongside state.
         ///
         /// Override to expose structured detail data (e.g. process lists, raw readings).
         /// The returned object is serialised and published to attributesTopic() whenever
         /// it changes, independently of the main state debounce.
         ///
         /// @return JSON object with attribute key/value pairs, or std::nullopt if not supported.
         virtual std::optional<nlohmann::json> getAttributes() const;


         /// @brief Get the base topic for commands targeting this entity
         /// Default: "command/{name}/set"
         virtual std::string commandTopic() const;

         /// @brief Get the base topic for publishing state from this entity
         /// Default: "pace/{node}/sensor/{name}/state" for sensors
         /// Default: "pace/{node}/switch/{name}/state" for switches
         virtual std::string stateTopic() const;

         /// @brief Topic for publishing JSON attributes.
         /// Default: "sensor/{name}/attributes"
         virtual std::string attributesTopic() const;

      protected:

         /// Reference to the MQTT service for publishing and subscribing
         MqttService& mqtt;
   };
   using EntityPtr = std::unique_ptr<EntityInterface>;

} // namespace pace::entities
