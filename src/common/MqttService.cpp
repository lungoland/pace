#include "pace/MqttService.hpp"

#include "util/AsyncTaskDispatcher.hpp"
#include "util/Task.hpp"
#include "util/TokenAwaiter.hpp"

#include <fmt/base.h>
#include <mqtt/connect_options.h>
#include <mqtt/create_options.h>
#include <mqtt/topic.h>

#include <exception>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

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

   util::Task<MqttService::OperationResult> MqttService::connect()
   {
      try
      {
         logger->info( "Connecting to MQTT broker '{}'", config.brokerUri );
         co_await client.connect( mqtt::connect_options_builder{}
                                     .clean_session( true )
                                     .automatic_reconnect( true )
                                     .user_name( config.username )
                                     .password( config.password )
                                     .will( mqtt::message( fmt::format( "{}availability", baseTopic ), "offline", 1, true ) )
                                     .finalize() );

         client.set_connected_handler( std::bind( &MqttService::onConnected, this, std::placeholders::_1 ) );
         client.set_connection_lost_handler( [ this ]( const std::string& cause )
                                             { logger->warn( "MQTT connection lost: {}", cause.empty() ? "unknown cause" : cause ); } );
         client.set_message_callback( std::bind( &MqttService::onMessage, this, std::placeholders::_1 ) );
         co_return co_await restoreSubscriptions();
      }
      catch( const std::exception& ex )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::NotConnected, "MQTT connect failed: {}", ex.what() ) };
      }
   }

   util::Task<MqttService::OperationResult> MqttService::disconnect()
   {
      if( ! client.is_connected() )
      {
         co_return {};
      }

      try
      {
         logger->info( "Disconnecting from MQTT broker '{}'", config.brokerUri );
         if( auto result = co_await publish( "availability", std::string{ "offline" }, true ); ! result )
         {
            logger->warn( "Failed to publish offline availability before disconnect: {}", result.error() );
         }
         co_await client.disconnect();
         co_return {};
      }
      catch( const std::exception& ex )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::NotConnected, "MQTT disconnect failed: {}", ex.what() ) };
      }
   }


   util::Task<MqttService::OperationResult> MqttService::subscribe( std::string topic, MessageHandler handler )
   {
      auto fqTopic = fmt::format( "{}{}", baseTopic, topic );
      logger->debug( " @  {}", fqTopic );

      /// TODO: Find a better place maybe?
      auto guardedHandler = [ this, handler = std::move( handler ) ]( mqtt::const_message_ptr msg ) -> util::Task<OperationResult>
      {
         try
         {
            co_return co_await handler( msg );
         }
         catch( const std::exception& ex )
         {
            logger->warn( "MQTT handler for '{}' failed: {}", msg->get_topic(), ex.what() );
            co_return util::unexpected{ util::makeError( util::ErrorCode::Internal, "MQTT handler failed: {}", ex.what() ) };
         }
         catch( ... )
         {
            logger->error( "MQTT handler for '{}' failed with a non-standard exception", msg->get_topic() );
            co_return util::unexpected{ util::makeError( util::ErrorCode::Internal, "MQTT handler failed with a non-standard exception" ) };
         }
      };

      {
         std::lock_guard lock{ topicHandlersMutex };
         topicHandlers.insert_or_assign( fqTopic, std::move( guardedHandler ) );
      }

      if( ! client.is_connected() )
      {
         co_return {};
      }

      try
      {
         co_await client.subscribe( fqTopic, config.qos );
         co_return {};
      }
      catch( const std::exception& ex )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::SubscriptionFailure, "MQTT subscribe failed for '{}': {}", fqTopic,
                                                      ex.what() ) };
      }
   }

   util::Task<MqttService::OperationResult> MqttService::unsubscribe( std::string topic )
   {
      auto fqTopic = fmt::format( "{}{}", baseTopic, topic );
      logger->debug( " @  {}", fqTopic );

      {
         std::lock_guard lock{ topicHandlersMutex };
         topicHandlers.erase( fqTopic );
      }

      if( ! client.is_connected() )
      {
         co_return {};
      }

      try
      {
         co_await client.unsubscribe( fqTopic );
         co_return {};
      }
      catch( const std::exception& ex )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::SubscriptionFailure, "MQTT unsubscribe failed for '{}': {}", fqTopic,
                                                      ex.what() ) };
      }
   }

   util::Task<MqttService::OperationResult> MqttService::restoreSubscriptions()
   {
      std::vector<std::string> topics;
      {
         std::lock_guard lock{ topicHandlersMutex };
         topics.reserve( topicHandlers.size() );
         for( const auto& [ topic, _ ] : topicHandlers )
         {
            topics.push_back( topic );
         }
      }

      for( const auto& topic : topics )
      {
         logger->debug( "Resubscribing to {}", topic );
         try
         {
            co_await client.subscribe( topic, config.qos );
         }
         catch( const std::exception& ex )
         {
            co_return util::unexpected{ util::makeError( util::ErrorCode::SubscriptionFailure, "MQTT resubscribe failed for '{}': {}",
                                                         topic, ex.what() ) };
         }
      }

      co_return co_await publish( "availability", std::string{ "online" }, true );
   }

   void MqttService::onConnected( const std::string& cause )
   {
      logger->info( "MQTT connected{}", cause.empty() ? "" : fmt::format( " ({})", cause ) );
      dispatcher.post( "MqttService::restoreSubscriptions",
                       [ this ]() -> util::Task<void>
                       {
                          if( auto result = co_await restoreSubscriptions(); ! result )
                          {
                             logger->error( "Failed to restore MQTT subscriptions: {}", result.error() );
                          }
                          co_return;
                       } );
   }


   util::Task<MqttService::OperationResult> MqttService::publish( const std::string& topic, std::string payload, bool retained )
   {
      // In case the connection was teared down - i.e. StopCommand was executed, we can no longer
      // pubish messages. So just stop here.
      if( ! client.is_connected() )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::NotConnected, "MQTT client is not connected" ) };
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
      try
      {
         co_await client.publish( fqTopic, payload, config.qos, retained );
         co_return {};
      }
      catch( const std::exception& ex )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::PublishFailure, "MQTT publish failed for '{}': {}", fqTopic,
                                                      ex.what() ) };
      }
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
      std::vector<MessageHandler> matchedHandlers;
      {
         std::lock_guard lock{ topicHandlersMutex };
         matchedHandlers.reserve( topicHandlers.size() );
         for( const auto& [ topicFilter, handler ] : topicHandlers )
         {
            /// Not really sure of constructing this every time ...
            /// but cannot be stored in container as does not implement operator<
            if( mqtt::topic_filter filter{ topicFilter }; filter.matches( msg->get_topic() ) )
            {
               matchedHandlers.push_back( handler );
            }
         }
      }

      for( const auto& handler : matchedHandlers )
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
         dispatcher.post( msg->get_topic(),
                          [ this, handler, msg ]() -> util::Task<void>
                          {
                             if( auto result = co_await handler( msg ); ! result )
                             {
                                logger->warn( "MQTT message handler reported error for '{}': {}", msg->get_topic(), result.error() );
                             }
                             co_return;
                          } );
      }
   }


} // namespace pace
