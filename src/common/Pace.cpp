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

      util::Task<bool> publishDiscovery( MqttService& mqtt, const entities::EntityInterface& entity )
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

   util::Task<bool> Pace::start()
   {
      try
      {
         scheduler.start( co_await util::current_executor(), dispatcher );
         co_await mqtt.connect();
         co_await entityFactory.subscribe();
         co_return true;
      }
      catch( const std::exception& ex )
      {
         logger->error( "Failed to start: {}", ex.what() );
         dispatcher.post( "Pace::stop", std::bind( &pace::Pace::stop, this ) );
         co_return false;
      }
   }

   util::Task<bool> Pace::stop()
   {
      co_await mqtt.disconnect();
      co_await scheduler.stopAsync();

      // does not really fit here .. we do not start the dispatcher
      dispatcher.stop();
      co_return true;
   }

   util::Task<bool> Pace::addEntity( entities::EntityPtr entity )
   {
      if( ! entity )
      {
         co_return false;
      }

      std::shared_ptr<entities::EntityInterface> sharedEntity = std::move( entity );

      const auto& entityName = sharedEntity->name();
      const auto& type       = sharedEntity->type();
      logger->info( "Adding entity '{}' of type {}", entityName, static_cast<int>( type ) );


      if( ! co_await sharedEntity->subscribe() )
      {
         logger->warn( "Failed to subscribe entity '{}'", entityName );
         co_return false;
      }

      if( ! co_await publishDiscovery( mqtt, *sharedEntity ) )
      {
         logger->warn( "Failed to publish discovery for entity '{}'", entityName );
         co_return false;
      }

      if( auto interval = sharedEntity->pollingInterval(); interval.has_value() )
      {
         scheduler.addJob( util::PeriodicScheduler::Job{
            .name     = entityName,
            .interval = *interval,
            .execute  = [ sharedEntity ]() -> util::Task<bool> { return sharedEntity->poll(); },
         } );
      }

      this->entities.push_back( std::move( sharedEntity ) );
      co_return true;
   }

   util::Task<bool> Pace::removeEntity( const std::string& entityName )
   {
      auto it = std::ranges::find_if( this->entities, [ & ]( const auto& e ) { return e->name() == entityName; } );
      if( it == std::end( this->entities ) )
      {
         co_return true;
      }

      auto        entity = *it;
      const auto& type   = entity->type();
      logger->info( "Removing entity '{}' of type {}", entityName, static_cast<int>( type ) );

      if( auto interval = entity->pollingInterval(); interval.has_value() )
      {
         scheduler.removeJob( entityName );
      }

      if( ! co_await entity->unsubscribe() )
      {
         logger->warn( "Failed to unsubscribe entity '{}'", entityName );
         co_return false;
      }

      this->entities.erase( it );
      co_return true;
   }

} // namespace pace
