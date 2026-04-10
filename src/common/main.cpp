#include "pace/Config.hpp"
#include "pace/Pace.hpp"
#include "pace/SignalShutdownWatcher.hpp"
#include "util/AsyncTaskDispatcher.hpp"
#include "util/Task.hpp"

#include <exception>
#include <fmt/base.h>
#include <functional>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>
#include <string>

int main()
{
   try
   {
      const auto cfg = pace::Config::fromEnvironment();
      spdlog::set_level( cfg.dryRun ? spdlog::level::debug : spdlog::level::info );
      // https://github.com/gabime/spdlog/wiki/Custom-formatting
      spdlog::set_pattern( "[%H:%M:%S.%e][%t][%^%=5!l%$] %v" );

      util::AsyncTaskDispatcher dispatcher;
      pace::Pace                pace{ cfg, dispatcher };

      pace::SignalShutdownWatcher shutdownWatcher{ [ &pace ]( int signalNumber )
                                                   {
                                                      spdlog::info( "Received signal {}", signalNumber );
                                                      util::sync_wait( pace.stop() );
                                                   } };

      dispatcher.post( std::bind( &pace::Pace::start, &pace ) );
      util::sync_wait( dispatcher.run() );
      return 0;
   }
   catch( const std::exception& ex )
   {
      spdlog::error( "runtime error: {}", ex.what() );
      return 1;
   }
}
