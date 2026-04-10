#pragma once

#include "util/Task.hpp"

#include <chrono>
#include <memory>
#include <string>

namespace pace
{
   class MqttService;
}

namespace pace::sensor
{
   class BaseSensor
   {
      public:

         explicit BaseSensor( MqttService& mqttService );
         virtual ~BaseSensor() = default;

         util::Task<bool> publish() const;

         virtual std::string               name() const = 0;
         virtual std::chrono::milliseconds interval() const
         {
            return std::chrono::seconds{ 5 };
         }
         virtual std::string fetch_() const = 0;

      private:

         MqttService& mqtt;
   };
   using BaseSensorPtr = std::unique_ptr<BaseSensor>;

} // namespace pace::sensor
