#pragma once

#include "util/Task.hpp"

#include <nlohmann/json_fwd.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace pace
{
   class MqttService;
}

namespace pace::entities
{
   /// @brief Enumeration of Home Assistant entity types
   enum class EntityType
   {
      Button,       ///< Stateless trigger entity (e.g., lock, reboot)
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


         /// @brief Get the base topic for commands targeting this entity
         /// Default: "command/{name}/set"
         virtual std::string commandTopic() const;

         /// @brief Get the base topic for command responses from this entity
         /// Default: "command/{name}/status"
         virtual std::string statusTopic() const;

         /// @brief Get the base topic for publishing state from this entity
         /// Default: "pace/{node}/sensor/{name}/state" for sensors
         /// Default: "pace/{node}/switch/{name}/state" for switches
         virtual std::string stateTopic() const;

      protected:

         /// Reference to the MQTT service for publishing and subscribing
         MqttService& mqtt;
   };
   using EntityPtr = std::unique_ptr<EntityInterface>;

} // namespace pace::entities
