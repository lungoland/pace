#include "pace/MqttService.hpp"

#include "util/AsyncTaskDispatcher.hpp"
#include "util/Task.hpp"
#include "util/TokenAwaiter.hpp"

#include <fmt/base.h>
#include <mqtt/connect_options.h>
#include <mqtt/create_options.h>
#include <mqtt/topic.h>
#include <spdlog/spdlog.h>

#include <ranges>
#include <string>
#include <utility>

namespace pace
{

   MqttService::MqttService( Config cfg, util::AsyncTaskDispatcher& dispatcher_ )
      : config( std::move( cfg ) )
      , client( mqtt::create_options_builder()
                   .server_uri( config.brokerUri )
                   .client_id( config.clientId )
                   .max_buffered_messages( 25 )
                   .delete_oldest_messages()
                   .finalize() )
      , baseTopic( fmt::format( "pace/{}/", config.nodeId ) )
      , dispatcher( dispatcher_ )
   {
      // dispatcher.post( [ this ]() { return connect(); } );
   }

   util::Task<bool> MqttService::connect()
   {
      spdlog::info( "Connecting to MQTT broker '{}'", config.brokerUri );
      co_await client.connect( mqtt::connect_options_builder{}
                                  .clean_session( true )
                                  .automatic_reconnect( true )
                                  .user_name( config.username )
                                  .password( config.password )
                                  .will( mqtt::message( fmt::format( "{}availability", baseTopic ), "offline", 1, true ) )
                                  .finalize() );

      client.set_message_callback( std::bind( &MqttService::onMessage, this, std::placeholders::_1 ) );
      co_await util::all( topicHandlers | std::views::keys
                          | std::views::transform(
                             [ this ]( const std::string& topic ) -> util::Task<bool>
                             {
                                co_await client.subscribe( topic, config.qos );
                                co_return true;
                             } ) );
      co_await publish( "availability", std::string{ "online" }, true );
      co_return true;
   }

   util::Task<bool> MqttService::disconnect()
   {
      spdlog::info( "Disconnecting from MQTT broker '{}'", config.brokerUri );
      if( ! client.is_connected() )
      {
         co_return true;
      }

      co_await publish( "availability", std::string{ "offline" }, true );
      co_await client.disconnect();
      co_return true;
   }


   util::Task<bool> MqttService::subscribe( const std::string& topic, MessageHandler handler )
   {
      auto fqTopic = fmt::format( "{}{}", baseTopic, topic );
      spdlog::debug( " @  {}", fqTopic );

      topicHandlers.emplace( fqTopic, std::move( handler ) );
      if( ! client.is_connected() )
      {
         co_return false;
      }
      co_await client.subscribe( fqTopic, config.qos );
      co_return true;
   }

   util::Task<bool> MqttService::unsubscribe( const std::string& topic )
   {
      auto fqTopic = fmt::format( "{}{}", baseTopic, topic );
      spdlog::debug( " @  {}", fqTopic );

      co_await client.unsubscribe( fqTopic );
      topicHandlers.erase( fqTopic );
      co_return true;
   }


   util::Task<bool> MqttService::publish( const std::string& topic, std::string payload )
   {
      return publish( topic, std::move( payload ), false );
   }

   util::Task<bool> MqttService::publish( const std::string& topic, std::string payload, bool retained )
   {
      auto fqTopic = fmt::format( "{}{}", baseTopic, topic );
      spdlog::debug( "<-- {}: {}", fqTopic, payload );
      co_await client.publish( fqTopic, payload, config.qos, retained );
      co_return true;
   }


   void MqttService::onMessage( const mqtt::const_message_ptr msg )
   {
      for( const auto& [ topicFilter, handler ] : topicHandlers )
      {
         /// Not really sure of constructing this every time ...
         /// but cannot be stored in container as does not implement operator<
         if( mqtt::topic_filter filter{ topicFilter }; filter.matches( msg->get_topic() ) )
         {
            /// Perform Context Switch between MQTT callback thread
            /// and our worker ... should/can this be a co_await?
            spdlog::debug( "--> {}: {}", msg->get_topic(), msg->to_string() );
            dispatcher.post( std::bind( handler, msg ) );
         }
      }
   }


} // namespace pace
