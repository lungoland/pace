#include "pace/Pace.hpp"

#include "pace/commands/BaseCommand.hpp"
#include "pace/commands/NullCommand.hpp"
#include "pace/commands/PingCommand.hpp"
#include "pace/commands/StopCommand.hpp"
#include "pace/commands/SystemActionCommand.hpp"

#include "pace/sensor/BaseSensor.hpp"
#include "pace/sensor/CountSensor.hpp"

namespace pace
{
   Pace::Pace( const Config& cfg, util::AsyncTaskDispatcher& disp )
      : config( cfg )
      , dispatcher( disp )
      , mqtt( config, dispatcher )
   {
      commands.emplace_back( std::make_unique<pace::commands::NullCommand>( mqtt ) );
      commands.emplace_back( std::make_unique<pace::commands::PingCommand>( mqtt ) );
      commands.emplace_back( std::make_unique<pace::commands::StopCommand>( mqtt, *this ) );
      commands.emplace_back( std::make_unique<pace::commands::LockCommand>( mqtt ) );
      commands.emplace_back( std::make_unique<pace::commands::SleepCommand>( mqtt ) );
      commands.emplace_back( std::make_unique<pace::commands::RebootCommand>( mqtt ) );
      commands.emplace_back( std::make_unique<pace::commands::ShutdownCommand>( mqtt ) );

      sensors.emplace_back( std::make_unique<pace::sensor::CountSensor>( mqtt ) );
   }

   util::Task<bool> Pace::start()
   {
      co_await mqtt.connect();
      co_await util::all( commands | std::views::transform( []( const auto& command ) { return command->subscribe(); } ) );
      sensorTimer = std::make_unique<SensorTimer>( sensors );
      co_return true;
   }

   util::Task<bool> Pace::stop()
   {
      if( sensorTimer )
      {
         co_await sensorTimer->stop();
      }
      co_await mqtt.disconnect();

      // does not really fit here .. we do not start the dispatcher
      co_await dispatcher.stop();
      co_return true;
   }
} // namespace pace
