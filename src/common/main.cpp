#include "pace/Config.hpp"
#include "pace/Pace.hpp"
#include "pace/SignalShutdownWatcher.hpp"
#include "util/AsyncTaskDispatcher.hpp"
#include "util/Logger.hpp"
#include "util/Task.hpp"

#include <exception>
#include <fmt/base.h>
#include <functional>
#include <string>

#include <spdlog/cfg/env.h>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

int main()
{
   auto logger = util::getLogger( "Pace" );
   try
   {
      const auto cfg = pace::Config::fromEnvironment();
      spdlog::cfg::load_env_levels();
      spdlog::set_pattern( "[%H:%M:%S.%f][%t][%^%=5!l%$][%n] %v" );

      util::AsyncTaskDispatcher dispatcher;
      pace::Pace                pace{ cfg, dispatcher };

      pace::SignalShutdownWatcher shutdownWatcher{ [ &pace, &logger ]( int signalNumber )
                                                   {
                                                      logger->info( "Received signal {}", signalNumber );
                                                      util::sync_wait( pace.stop() );
                                                   } };

      dispatcher.post( "Pace::start", std::bind( &pace::Pace::start, &pace ) );
      util::sync_wait( dispatcher.run() );
      return 0;
   }
   catch( const std::exception& ex )
   {
      logger->critical( "runtime error: {}", ex.what() );
      return 1;
   }
}
