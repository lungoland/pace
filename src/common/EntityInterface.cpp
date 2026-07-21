#include "pace/EntityInterface.hpp"

#include "pace/MqttService.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

namespace pace::entities
{
   EntityInterface::EntityInterface( MqttService& mqttService )
      : mqtt( mqttService )
   {}

   util::Task<OperationResult> EntityInterface::subscribe()
   {
      co_return {};
   }

   util::Task<OperationResult> EntityInterface::unsubscribe()
   {
      co_return {};
   }

   std::optional<std::chrono::milliseconds> EntityInterface::pollingInterval() const
   {
      return std::nullopt;
   }

   util::Task<OperationResult> EntityInterface::poll()
   {
      co_return {};
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
         case EntityType::Text : payload[ "command_topic" ] = mqtt.qualifyTopic( commandTopic() ); break;

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

      if( getAttributes().has_value() )
      {
         payload[ "json_attributes_topic" ] = mqtt.qualifyTopic( attributesTopic() );
      }

      return payload;
   }

   std::string EntityInterface::commandTopic() const
   {
      return "command/" + name() + "/set";
   }

   std::optional<nlohmann::json> EntityInterface::getAttributes() const
   {
      return std::nullopt;
   }

   std::string EntityInterface::attributesTopic() const
   {
      return "sensor/" + name() + "/attributes";
   }

   std::string EntityInterface::stateTopic() const
   {
      return "sensor/" + name() + "/state";
   }

} // namespace pace::entities
