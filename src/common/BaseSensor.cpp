#include "pace/sensor/BaseSensor.hpp"
#include "pace/MqttService.hpp"

#include <fmt/format.h>

#include <coroutine>
#include <string>

namespace pace::sensor
{
   BaseSensor::BaseSensor( MqttService& mqttService )
      : mqtt( mqttService )
   {}

   util::Task<bool> BaseSensor::publish() const
   {
      auto data = fetch_();
      co_await mqtt.publish( fmt::format( "sensor/{}/state", name() ), data );
      co_return true;
   }
} // namespace pace::sensor
