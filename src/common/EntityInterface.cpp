#include "pace/EntityInterface.hpp"

#include "pace/MqttService.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

namespace pace::entities
{
   EntityInterface::EntityInterface( MqttService& mqttService )
      : mqtt( mqttService )
   {}

   util::Task<bool> EntityInterface::subscribe()
   {
      co_return true;
   }

   util::Task<bool> EntityInterface::unsubscribe()
   {
      co_return true;
   }

   std::optional<std::chrono::milliseconds> EntityInterface::pollingInterval() const
   {
      return std::nullopt;
   }

   util::Task<bool> EntityInterface::poll()
   {
      co_return true;
   }

   nlohmann::json EntityInterface::getDiscoveryPayload() const
   {
      const auto     uniqueId = fmt::format( "{}_{}", mqtt.nodeId(), name() );
      nlohmann::json payload{
         { "name",               name()                              },
         { "unique_id",          uniqueId                            },
         { "availability_topic", mqtt.qualifyTopic( "availability" ) },
         { "device",
          {
              { "identifiers", nlohmann::json::array( { mqtt.nodeId() } ) },
              { "name", mqtt.nodeId() },
              { "model", "pace" },
              { "manufacturer", "pace" },
           }                                                         },
      };

      switch( type() )
      {
         case EntityType::Button : payload[ "command_topic" ] = mqtt.qualifyTopic( commandTopic() ); break;
         case EntityType::Sensor : payload[ "state_topic" ] = mqtt.qualifyTopic( stateTopic() ); break;
         case EntityType::BinarySensor :
            payload[ "state_topic" ] = mqtt.qualifyTopic( stateTopic() );
            payload[ "payload_on" ]  = "on";
            payload[ "payload_off" ] = "off";
            break;
         case EntityType::Notify : payload[ "command_topic" ] = mqtt.qualifyTopic( commandTopic() ); break;
         case EntityType::Switch :
            payload[ "state_topic" ]   = mqtt.qualifyTopic( stateTopic() );
            payload[ "command_topic" ] = mqtt.qualifyTopic( commandTopic() );
            payload[ "payload_on" ]    = "on";
            payload[ "payload_off" ]   = "off";
            break;
      }

      return payload;
   }

   std::string EntityInterface::commandTopic() const
   {
      return "command/" + name() + "/set";
   }

   std::string EntityInterface::statusTopic() const
   {
      return "command/" + name() + "/status";
   }

   std::string EntityInterface::stateTopic() const
   {
      return "sensor/" + name() + "/state";
   }

} // namespace pace::entities
