#include <catch2/catch_test_macros.hpp>

#include "pace/Config.hpp"
#include "pace/EntityInterface.hpp"
#include "pace/MqttService.hpp"
#include "pace/commands/NotifyCommand.hpp"
#include "pace/sensors/BaseSensor.hpp"
#include "pace/switches/ProcessSwitch.hpp"
#include "util/AsyncTaskDispatcher.hpp"

#include <memory>
#include <string>

namespace
{
   class TestEntity final : public pace::entities::EntityInterface
   {
      public:

         TestEntity( pace::MqttService& mqttService, pace::entities::EntityType entityType )
            : pace::entities::EntityInterface( mqttService )
            , typeValue( entityType )
         {}

         std::string name() const override
         {
            return "test_entity";
         }

         pace::entities::EntityType type() const override
         {
            return typeValue;
         }

      private:

         pace::entities::EntityType typeValue;
   };

   class BoolSensor final : public pace::sensors::BaseSensor<bool>
   {
      public:

         using Base = pace::sensors::BaseSensor<bool>;
         using Base::Base;

         util::Task<bool> fetch() const override
         {
            co_return true;
         }
   };

   class IntSensor final : public pace::sensors::BaseSensor<int>
   {
      public:

         using Base = pace::sensors::BaseSensor<int>;
         using Base::Base;

         util::Task<int> fetch() const override
         {
            co_return 42;
         }
   };

   pace::MqttService makeOfflineMqtt( util::AsyncTaskDispatcher& dispatcher )
   {
      pace::Config cfg{};
      cfg.nodeId = "test-node";
      return pace::MqttService( cfg, dispatcher );
   }

} // namespace

TEST_CASE( "entity discovery payload includes expected topics by type", "[entity][discovery]" )
{
   util::AsyncTaskDispatcher dispatcher;
   auto                      mqtt = makeOfflineMqtt( dispatcher );

   SECTION( "button" )
   {
      TestEntity entity{ mqtt, pace::entities::EntityType::Button };
      const auto payload = entity.getDiscoveryPayload();
      CHECK( payload.contains( "command_topic" ) );
      CHECK_FALSE( payload.contains( "state_topic" ) );
      CHECK( payload.at( "unique_id" ).get<std::string>() == "test-node_test_entity" );
      CHECK( payload.at( "command_topic" ).get<std::string>() == "pace/test-node/command/test_entity/set" );
      CHECK( payload.at( "availability_topic" ).get<std::string>() == "pace/test-node/availability" );
      REQUIRE( payload.contains( "device" ) );
      CHECK( payload.at( "device" ).at( "name" ).get<std::string>() == "test-node" );
      CHECK( payload.at( "device" ).at( "identifiers" ).at( 0 ).get<std::string>() == "test-node" );
   }

   SECTION( "sensor" )
   {
      TestEntity entity{ mqtt, pace::entities::EntityType::Sensor };
      const auto payload = entity.getDiscoveryPayload();
      CHECK( payload.contains( "state_topic" ) );
      CHECK_FALSE( payload.contains( "command_topic" ) );
      CHECK( payload.at( "state_topic" ).get<std::string>() == "pace/test-node/sensor/test_entity/state" );
   }

   SECTION( "binary_sensor" )
   {
      TestEntity entity{ mqtt, pace::entities::EntityType::BinarySensor };
      const auto payload = entity.getDiscoveryPayload();
      CHECK( payload.contains( "state_topic" ) );
      CHECK( payload.at( "payload_on" ).get<std::string>() == "on" );
      CHECK( payload.at( "payload_off" ).get<std::string>() == "off" );
   }

   SECTION( "switch" )
   {
      TestEntity entity{ mqtt, pace::entities::EntityType::Switch };
      const auto payload = entity.getDiscoveryPayload();
      CHECK( payload.contains( "state_topic" ) );
      CHECK( payload.contains( "command_topic" ) );
   }

   SECTION( "notify" )
   {
      TestEntity entity{ mqtt, pace::entities::EntityType::Notify };
      const auto payload = entity.getDiscoveryPayload();
      CHECK( payload.contains( "command_topic" ) );
      CHECK_FALSE( payload.contains( "state_topic" ) );
   }
}

TEST_CASE( "sensor templates map to expected entity types", "[entity][sensor]" )
{
   util::AsyncTaskDispatcher dispatcher;
   auto                      mqtt = makeOfflineMqtt( dispatcher );

   pace::entities::config::EntityConfig boolCfg;
   boolCfg.name = "bool_sensor";
   pace::entities::config::EntityConfig intCfg;
   intCfg.name = "int_sensor";

   BoolSensor boolSensor{ mqtt, boolCfg };
   IntSensor  intSensor{ mqtt, intCfg };

   CHECK( boolSensor.type() == pace::entities::EntityType::BinarySensor );
   CHECK( intSensor.type() == pace::entities::EntityType::Sensor );
}

TEST_CASE( "process guard switch config parses from json", "[entity][switch]" )
{
   const nlohmann::json config{
      { "name",      "process_guard_1" },
      { "imagePath", "myapp"           },
   };

   const auto parsed = config.get<pace::switches::config::ProcessSwitchConfig>();
   CHECK( parsed.name == "process_guard_1" );
   CHECK( parsed.imagePath == "myapp" );
}

TEST_CASE( "notify command is classified as notify entity", "[entity][notify]" )
{
   util::AsyncTaskDispatcher dispatcher;
   auto                      mqtt = makeOfflineMqtt( dispatcher );

   pace::commands::NotifyCommand notify{ mqtt };

   CHECK( notify.type() == pace::entities::EntityType::Notify );

   const auto payload = notify.getDiscoveryPayload();
   CHECK( payload.at( "command_topic" ).get<std::string>() == "pace/test-node/command/notify/set" );
}
