#include "pace/sensors/BaseSensor.hpp"
#include "pace/MqttService.hpp"

#include <fmt/format.h>

#include <coroutine>
#include <string>

namespace pace::sensors
{
   SensorInterface::SensorInterface( MqttService& mqttService, std::unique_ptr<config::BaseSensorConfig> cfg )
      : mqtt( mqttService )
      , config( std::move( cfg ) )
   {}

   util::Task<bool> SensorInterface::fetchAndPublish()
   {
      auto data = co_await fetch_();

      // Debounce data to avoid flooding mqtt with unchanged values
      // But publish once in a while for newly connected clients.
      if( debounce < MAX_DEBOUNCE && data == lastData )
      {
         ++debounce;
         co_return false;
      }
      lastData = std::move( data );
      debounce = 0;

      co_await mqtt.publish( fmt::format( "sensor/{}/state", name() ), lastData );
      co_return true;
   }

   std::string SensorInterface::name() const
   {
      return config->name;
   }
   std::chrono::milliseconds SensorInterface::interval() const
   {
      return config->interval;
   }

} // namespace pace::sensors
