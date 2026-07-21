#include "pace/Pace.hpp"

#include <fmt/format.h>

namespace pace
{
   namespace
   {
      std::string entityTypeToDiscoveryName( entities::EntityType type )
      {
         switch( type )
         {
            case entities::EntityType::Button : return "button";
            case entities::EntityType::Text : return "text";
            case entities::EntityType::Sensor : return "sensor";
            case entities::EntityType::BinarySensor : return "binary_sensor";
            case entities::EntityType::Notify : return "notify";
            case entities::EntityType::Switch : return "switch";
         }
         return "sensor";
      }

      util::Task<Pace::OperationResult> publishDiscovery( MqttService& mqtt, const entities::EntityInterface& entity )
      {
         auto payload = entity.getDiscoveryPayload();
         auto topic   = fmt::format( "/homeassistant/{}/{}/config", entityTypeToDiscoveryName( entity.type() ),
                                     payload.at( "unique_id" ).get<std::string>() );
         co_return co_await mqtt.publish( topic, payload, true );
      }
   }

   Pace::Pace( const Config& cfg, util::AsyncTaskDispatcher& disp )
      : config( cfg )
      , mqtt( config, disp )
      , dispatcher( disp )
      , entityFactory( *this, mqtt )
   {}

   util::Task<Pace::OperationResult> Pace::start()
   {
      try
      {
         scheduler.start( co_await util::current_executor(), dispatcher );

         if( auto connectResult = co_await mqtt.connect(); ! connectResult )
         {
            co_return util::unexpected{ util::makeError( util::ErrorCode::NotConnected, "Failed to connect MQTT: {}",
                                                         connectResult.error() ) };
         }

         if( auto subscribeResult = co_await entityFactory.subscribe(); ! subscribeResult )
         {
            co_return util::unexpected{ util::makeError( util::ErrorCode::SubscriptionFailure, "Failed to subscribe entity factory: {}",
                                                         subscribeResult.error() ) };
         }

         co_return {};
      }
      catch( const std::exception& ex )
      {
         logger->error( "Failed to start: {}", ex.what() );
         dispatcher.post( "Pace::stop",
                          [ this ]() -> util::Task<void>
                          {
                             if( auto stopResult = co_await stop(); ! stopResult )
                             {
                                logger->error( "Pace::stop after failed start also failed: {}", stopResult.error() );
                             }
                             co_return;
                          } );
         co_return util::unexpected{ util::makeError( util::ErrorCode::Internal, "{}", ex.what() ) };
      }
   }

   util::Task<Pace::OperationResult> Pace::stop()
   {
      if( auto disconnectResult = co_await mqtt.disconnect(); ! disconnectResult )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::NotConnected, "Failed to disconnect MQTT: {}",
                                                      disconnectResult.error() ) };
      }

      co_await scheduler.stopAsync();

      // does not really fit here .. we do not start the dispatcher
      dispatcher.stop();
      co_return {};
   }

   util::Task<Pace::OperationResult> Pace::addEntity( entities::EntityPtr entity )
   {
      if( ! entity )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::InvalidArgument, "Cannot add null entity" ) };
      }

      std::shared_ptr<entities::EntityInterface> sharedEntity = std::move( entity );

      const auto& entityName = sharedEntity->name();
      const auto& type       = sharedEntity->type();
      logger->info( "Adding entity '{}' of type {}", entityName, static_cast<int>( type ) );


      if( auto subscribeResult = co_await sharedEntity->subscribe(); ! subscribeResult )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::SubscriptionFailure, "Failed to subscribe entity '{}': {}",
                                                      entityName, subscribeResult.error() ) };
      }

      if( auto discoveryResult = co_await publishDiscovery( mqtt, *sharedEntity ); ! discoveryResult )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::PublishFailure, "Failed to publish discovery for entity '{}': {}",
                                                      entityName, discoveryResult.error() ) };
      }

      if( auto interval = sharedEntity->pollingInterval(); interval.has_value() )
      {
         scheduler.addJob( util::PeriodicScheduler::Job{
            .name     = entityName,
            .interval = *interval,
            .execute  = [ sharedEntity ]() -> util::Task<void>
            {
               if( auto pollResult = co_await sharedEntity->poll(); ! pollResult )
               {
                  auto paceLogger = util::getLogger( "Pace" );
                  paceLogger->warn( "Polling entity '{}' failed: {}", sharedEntity->name(), pollResult.error() );
               }
               co_return;
            },
         } );
      }

      this->entities.push_back( std::move( sharedEntity ) );
      co_return {};
   }

   util::Task<Pace::OperationResult> Pace::removeEntity( const std::string& entityName )
   {
      auto it = std::ranges::find_if( this->entities, [ & ]( const auto& e ) { return e->name() == entityName; } );
      if( it == std::end( this->entities ) )
      {
         co_return {};
      }

      auto        entity = *it;
      const auto& type   = entity->type();
      logger->info( "Removing entity '{}' of type {}", entityName, static_cast<int>( type ) );

      if( auto interval = entity->pollingInterval(); interval.has_value() )
      {
         scheduler.removeJob( entityName );
      }

      if( auto unsubscribeResult = co_await entity->unsubscribe(); ! unsubscribeResult )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::SubscriptionFailure, "Failed to unsubscribe entity '{}': {}",
                                                      entityName, unsubscribeResult.error() ) };
      }

      this->entities.erase( it );
      co_return {};
   }

} // namespace pace
