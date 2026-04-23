#include "pace/Pace.hpp"

#include "pace/commands/BaseCommand.hpp"
#include "pace/commands/ExecCommand.hpp"
#include "pace/commands/KillCommand.hpp"
#include "pace/commands/NotifyCommand.hpp"
#include "pace/commands/NullCommand.hpp"
#include "pace/commands/PingCommand.hpp"
#include "pace/commands/StopCommand.hpp"
#include "pace/commands/SystemActionCommand.hpp"

#include "pace/sensors/BaseSensor.hpp"
#include "pace/sensors/CountSensor.hpp"
#include "pace/sensors/GameSensor.hpp"
#include "pace/sensors/ProcSensor.hpp"

namespace pace
{
   namespace
   {
      void scheduleJob( util::PeriodicScheduler& scheduler, std::shared_ptr<sensors::SensorInterface> sensor )
      {
         scheduler.addJob( util::PeriodicScheduler::Job{
            .name     = sensor->name(),
            .interval = sensor->interval(),
            .execute  = [ sensor ]() -> util::Task<bool> { return sensor->fetchAndPublish(); },
         } );
      }
   }

   Pace::Pace( const Config& cfg, util::AsyncTaskDispatcher& disp )
      : config( cfg )
      , mqtt( config, disp )
      , dispatcher( disp )
      , sensorFactory( *this, mqtt )
   {
      /// TODO: Move to CommandFactory and make configurable via mqtt config topic
      /// which config?!
      commands.emplace_back( std::make_unique<pace::commands::NullCommand>( mqtt ) );
      commands.emplace_back( std::make_unique<pace::commands::PingCommand>( mqtt ) );
      commands.emplace_back( std::make_unique<pace::commands::ExecCommand>( mqtt ) );
      commands.emplace_back( std::make_unique<pace::commands::KillCommand>( mqtt ) );
      commands.emplace_back( std::make_unique<pace::commands::NotifyCommand>( mqtt ) );
      commands.emplace_back( std::make_unique<pace::commands::StopCommand>( mqtt, *this ) );
      commands.emplace_back( std::make_unique<pace::commands::LockCommand>( mqtt ) );
      commands.emplace_back( std::make_unique<pace::commands::SleepCommand>( mqtt ) );
      commands.emplace_back( std::make_unique<pace::commands::RebootCommand>( mqtt ) );
      commands.emplace_back( std::make_unique<pace::commands::ShutdownCommand>( mqtt ) );
   }

   util::Task<bool> Pace::start()
   {
      bool ret = co_await mqtt.connect();

      ret &= co_await util::all( commands | std::views::transform( []( const auto& command ) { return command->subscribe(); } ) );
      ret &= co_await sensorFactory.subscribe();
      co_return ret;
   }

   util::Task<bool> Pace::stop()
   {
      scheduler.stop();
      co_await mqtt.disconnect();

      // does not really fit here .. we do not start the dispatcher
      dispatcher.stop();
      co_return true;
   }

   void Pace::addSensor( sensors::SensorPtr sensor )
   {
      /// use fmt::format .. but that requires constexpr for strings ...
      spdlog::info( "Adding \033[3m{}\033[0m sensor name {}", sensor->name(), sensor->name() );
      auto sharedSensor = std::shared_ptr<sensors::SensorInterface>( std::move( sensor ) );
      scheduleJob( scheduler, sharedSensor );
      sensors.emplace_back( std::move( sharedSensor ) );
   }

   void Pace::removeSensor( const std::string& sensorName )
   {
      auto it = std::find_if( sensors.begin(), sensors.end(), [ & ]( const auto& s ) { return s->name() == sensorName; } );
      if( it == sensors.end() )
      {
         return;
      }

      spdlog::info( "Removing \033[3m{}\033[0m sensor name {}", ( *it )->name(), sensorName );
      scheduler.removeJob( sensorName );
      sensors.erase( it );
   }
} // namespace pace
