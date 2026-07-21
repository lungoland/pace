#include "pace/Config.hpp"
#include "pace/EntityInterface.hpp"
#include "pace/MqttService.hpp"
#include "util/AsyncTaskDispatcher.hpp"
#include "util/Executor.hpp"
#include "util/Task.hpp"

#include <catch2/catch_test_macros.hpp>
#include <mqtt/message.h>

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
   struct DispatcherDriver
   {
         util::AsyncTaskDispatcher       dispatcher;
         std::shared_ptr<util::Executor> executor = std::make_shared<util::Executor>();
         util::Task<void>                dispatcherTask{ dispatcher.run() };
         std::thread                     executorThread;

         DispatcherDriver()
         {
            auto& promise = dispatcherTask.handle.promise();
            promise.set_executor( executor );
            promise.started = true;
            dispatcherTask.handle.resume();

            executorThread = std::thread{ [ this ]
                                          {
                                             while( executor->run_one() )
                                             {}
                                          } };
         }

         ~DispatcherDriver()
         {
            dispatcher.stop();
            executor->post( {} );
            if( executorThread.joinable() )
            {
               executorThread.join();
            }
         }
   };

   pace::MqttService makeOfflineMqtt( util::AsyncTaskDispatcher& dispatcher )
   {
      pace::Config cfg{};
      cfg.nodeId = "test-node";
      return pace::MqttService( cfg, dispatcher );
   }

   pace::MqttService::OperationResult awaitResultTask( util::Task<pace::MqttService::OperationResult>&& task )
   {
      auto& promise = task.handle.promise();
      util::sync_wait( std::move( task ) );
      return promise.get_result();
   }
} // namespace

namespace pace
{
   struct MqttServiceProbe
   {
         static bool hasHandlerForTopic( MqttService& mqtt, const std::string& topic )
         {
            std::lock_guard lock{ mqtt.topicHandlersMutex };
            return mqtt.topicHandlers.contains( topic );
         }

         static void deliver( MqttService& mqtt, mqtt::const_message_ptr msg )
         {
            mqtt.onMessage( std::move( msg ) );
         }
   };
} // namespace pace

TEST_CASE( "mqtt subscribe keeps handlers while offline", "[mqtt]" )
{
   util::AsyncTaskDispatcher dispatcher;
   auto                      mqtt = makeOfflineMqtt( dispatcher );

   const auto subscribed = awaitResultTask( mqtt.subscribe(
      "command/test/set", []( mqtt::const_message_ptr ) -> util::Task<pace::MqttService::OperationResult> { co_return {}; } ) );

   CHECK( subscribed.has_value() );
   CHECK( pace::MqttServiceProbe::hasHandlerForTopic( mqtt, "pace/test-node/command/test/set" ) );
}

TEST_CASE( "mqtt handler exceptions do not stop the dispatcher", "[mqtt]" )
{
   DispatcherDriver driver;
   auto             mqtt = makeOfflineMqtt( driver.dispatcher );

   std::promise<void> badPayloadHandled;
   auto               badPayloadFuture = badPayloadHandled.get_future();
   std::promise<void> followUpHandled;
   auto               followUpFuture = followUpHandled.get_future();

   const auto subscribed = awaitResultTask(
      mqtt.subscribe( "switch/test/set",
                      [ & ]( mqtt::const_message_ptr ) -> util::Task<pace::MqttService::OperationResult>
                      {
                         badPayloadHandled.set_value();
                         throw std::runtime_error{ "simulated handler failure" };
                         co_return {};
                      } ) );

   REQUIRE( subscribed.has_value() );

   pace::MqttServiceProbe::deliver( mqtt, mqtt::make_message( "pace/test-node/switch/test/set", "not-a-bool" ) );
   REQUIRE( badPayloadFuture.wait_for( std::chrono::seconds{ 1 } ) == std::future_status::ready );

   driver.dispatcher.post( "follow-up",
                           [ & ]() -> util::Task<void>
                           {
                              followUpHandled.set_value();
                              co_return;
                           } );

   REQUIRE( followUpFuture.wait_for( std::chrono::seconds{ 1 } ) == std::future_status::ready );
}