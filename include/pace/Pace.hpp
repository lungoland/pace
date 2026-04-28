#pragma once

#include "pace/Config.hpp"
#include "pace/MqttService.hpp"
#include "pace/SensorFactory.hpp"

#include "pace/commands/BaseCommand.hpp"
#include "pace/sensors/BaseSensor.hpp"

#include "util/AsyncTaskDispatcher.hpp"
#include "util/PeriodicScheduler.hpp"
#include "util/Task.hpp"

namespace pace
{
   class Pace
   {
      public:

         explicit Pace( const Config& cfg, util::AsyncTaskDispatcher& dispatcher );
         ~Pace() = default;

         /// @brief Starts the service by connecting to MQTT and subscribing to relevant topics.
         /// @return true once all subscriptions are successful; false otherwise.
         util::Task<bool> start();
         /// @brief Stops the service and performs any necessary cleanup.
         /// @note While not started by pace, this will also stop the dispatcher!
         /// @return true once stopped successfully; false otherwise.
         util::Task<bool> stop();

         /// @brief Adds a sensor to the scheduler for periodic execution.
         /// @param sensor The sensor to take ownership of and schedule.
         void addSensor( sensors::SensorPtr sensor );
         /// @brief Removes a sensor from the scheduler.
         /// @param sensorName The name of the sensor to remove.
         void removeSensor( const std::string& sensorName );

      private:

         /// @brief Base configuration - mostly MQTT relevant.
         Config config;

         /// @brief MQTT service for handling communication with the MQTT broker.
         MqttService mqtt;
         /// @brief Dispatcher for handling asynchronous tasks, such as sensor data fetching and command execution.
         util::AsyncTaskDispatcher& dispatcher;
         /// @brief  Scheduler for managing periodic execution of sensor data fetching.
         util::PeriodicScheduler scheduler;

         /// @brief Factory for creating sensors based on MQTT configuration topics.
         pace::SensorFactory sensorFactory;

         /// TODO: HA MQTT Type specifics?
         ///       button, binary_sensor, sensor, switch, notify
         ///       "switch" would be sensor + command combined
         ///
         ///   switch, sensor, binary_sensor: "SensorInterface"
         ///   switch, button, notify: "CommandInterface"

         /// @brief List of available commands that the service can execute based on MQTT messages.
         std::vector<pace::commands::CommandPtr> commands;
         /// @brief List of active sensors which are scheduled periodically.
         /// TODO: crashes when removing sensors ... shared_ptr could be a fix but
         ///       causes sensors to stay alive ... for some reasons
         ///  ...  basically a issue when removed when currently executing ...
         std::vector<std::shared_ptr<sensors::SensorInterface>> sensors;
   };
} // namespace pace
