#pragma once

#include "pace/Config.hpp"
#include "pace/EntityFactory.hpp"
#include "pace/EntityInterface.hpp"
#include "pace/MqttService.hpp"

#include "pace/commands/BaseCommand.hpp"
#include "pace/sensors/BaseSensor.hpp"

#include "util/AsyncTaskDispatcher.hpp"
#include "util/Logger.hpp"
#include "util/PeriodicScheduler.hpp"
#include "util/Task.hpp"

namespace pace
{
   class Pace
   {
      public:

         explicit Pace( const Config& cfg, util::AsyncTaskDispatcher& dispatcher );
         Pace( const Pace& )            = delete;
         Pace& operator=( const Pace& ) = delete;
         Pace( Pace&& )                 = delete;
         Pace& operator=( Pace&& )      = delete;
         ~Pace()                        = default;

         /// @brief Starts the service by connecting to MQTT and subscribing to relevant topics.
         /// @return true once all subscriptions are successful; false otherwise.
         util::Task<bool> start();
         /// @brief Stops the service and performs any necessary cleanup.
         /// @note While not started by pace, this will also stop the dispatcher!
         /// @return true once stopped successfully; false otherwise.
         util::Task<bool> stop();

         /// @brief Adds a generic entity to the registry and scheduler if applicable.
         /// @param entity The entity to take ownership of and register.
         util::Task<bool> addEntity( entities::EntityPtr entity );

         /// @brief Removes an entity from the registry and scheduler if applicable.
         /// @param entityName The name of the entity to remove.
         util::Task<bool> removeEntity( const std::string& entityName );

      private:

         util::Logger logger = util::getLogger( "Pace" );

         /// @brief Base configuration - mostly MQTT relevant.
         Config config;

         /// @brief MQTT service for handling communication with the MQTT broker.
         MqttService mqtt;
         /// @brief Dispatcher for handling asynchronous tasks, such as sensor data fetching and command execution.
         util::AsyncTaskDispatcher& dispatcher;
         /// @brief  Scheduler for managing periodic execution of sensor data fetching.
         util::PeriodicScheduler scheduler;

         /// @brief Factory for creating entities based on MQTT configuration topics.
         pace::EntityFactory entityFactory;

         /// @brief Unified registry of all entities (commands, sensors, switches, notify, etc.)
         std::vector<std::shared_ptr<entities::EntityInterface>> entities{};
   };
} // namespace pace
