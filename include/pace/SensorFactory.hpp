#pragma once
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "pace/sensors/BaseSensor.hpp"
#include "util/Task.hpp"

namespace pace
{
   class MqttService;
   class Pace;

   class SensorFactory
   {
      public:

         explicit SensorFactory( Pace& p, MqttService& m )
            : pace( p )
            , mqtt( m )
         {}

         /// @brief Subscribes to sensor config topics and creates/updates sensors based on received config.
         /// Complete sensors are posted to the queue; a background consumer loop co_awaits them.
         /// Topic format: pace/sensor/{sensorName}/config/{configNode}
         util::Task<bool> subscribe();

      private:

         /// @brief Handles incoming partial sensor config updates, creates/updates sensors when complete config is available.
         /// @param name Name of the sensor being configured
         /// @param node Config node being updated (e.g., "type", "interval", "command", etc.)
         /// @param data Value of the config node as a string; will be parsed to the appropriate type based on the node
         /// @note An update to an existing sensor will first remove the sensor and re-add a new instance with updated config.
         void onPartialConfig( const std::string& name, const std::string& node, const std::string& data );

         Pace&        pace;
         MqttService& mqtt;

         /// @brief In memory storage for the configured sensors ... partially or constructed.
         std::map<std::string, nlohmann::json> sensorConfigs;
   };
} // namespace pace
