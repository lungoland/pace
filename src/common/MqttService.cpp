#include "pace/MqttService.hpp"

#include "util/AsyncTaskDispatcher.hpp"
#include "util/Task.hpp"
#include "util/TokenAwaiter.hpp"

#include <fmt/base.h>
#include <mqtt/connect_options.h>
#include <mqtt/create_options.h>
#include <mqtt/topic.h>

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
   {}

   util::Task<bool> MqttService::connect()
   {
      logger->info( "Connecting to MQTT broker '{}'", config.brokerUri );
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
      logger->info( "Disconnecting from MQTT broker '{}'", config.brokerUri );
      if( ! client.is_connected() )
      {
         co_return true;
      }

      co_await publish( "availability", std::string{ "offline" }, true );
      co_await client.disconnect();
      co_return true;
   }


   util::Task<bool> MqttService::subscribe( std::string topic, MessageHandler handler )
   {
      auto fqTopic = fmt::format( "{}{}", baseTopic, topic );
      logger->debug( " @  {}", fqTopic );

      topicHandlers.emplace( fqTopic, std::move( handler ) );
      if( ! client.is_connected() )
      {
         co_return false;
      }
      co_await client.subscribe( fqTopic, config.qos );
      co_return true;
   }

   util::Task<bool> MqttService::unsubscribe( std::string topic )
   {
      auto fqTopic = fmt::format( "{}{}", baseTopic, topic );
      logger->debug( " @  {}", fqTopic );

      co_await client.unsubscribe( fqTopic );
      topicHandlers.erase( fqTopic );
      co_return true;
   }


   util::Task<bool> MqttService::publish( const std::string& topic, std::string payload, bool retained )
   {
      // In case the connection was teared down - i.e. StopCommand was executed, we can no longer
      // pubish messages. So just stop here.
      if( ! client.is_connected() )
      {
         co_return false;
      }

      auto fqTopic = qualifyTopic( topic );
      if( payload.size() > 100 )
      {
         logger->debug( "<-- {}: {} bytes", fqTopic, payload.size() );
      }
      else
      {
         logger->debug( "<-- {}: {}", fqTopic, payload );
      }
      co_await client.publish( fqTopic, payload, config.qos, retained );
      co_return true;
   }

   std::string MqttService::qualifyTopic( const std::string& topic ) const
   {
      return topic.starts_with( '/' ) ? topic.substr( 1 ) : fmt::format( "{}{}", baseTopic, topic );
   }

   const std::string& MqttService::nodeId() const
   {
      return config.nodeId;
   }


   void MqttService::onMessage( mqtt::const_message_ptr msg )
   {
      for( const auto& [ topicFilter, handler ] : topicHandlers )
      {
         /// Not really sure of constructing this every time ...
         /// but cannot be stored in container as does not implement operator<
         if( mqtt::topic_filter filter{ topicFilter }; filter.matches( msg->get_topic() ) )
         {
            /// Perform Context Switch between MQTT callback thread
            /// and our worker ... should/can this be a co_await?
            if( msg->get_payload().size() > 100 )
            {
               logger->debug( "--> {}: {} bytes", msg->get_topic(), msg->get_payload().size() );
            }
            else
            {
               logger->debug( "--> {}: {}", msg->get_topic(), msg->to_string() );
            }
            dispatcher.post( msg->get_topic(), std::bind( handler, msg ) );
         }
      }
   }


} // namespace pace
