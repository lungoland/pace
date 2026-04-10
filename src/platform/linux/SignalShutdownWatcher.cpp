#include "pace/SignalShutdownWatcher.hpp"

#include <fmt/base.h>
#include <spdlog/spdlog.h>

#include <cerrno>
#include <signal.h>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>

namespace pace
{
   SignalShutdownWatcher::SignalShutdownWatcher( SignalHandler onSignal )
      : onSignalCallback( std::move( onSignal ) )
   {
      sigset_t signalSet{};
      sigemptyset( &signalSet );
      sigaddset( &signalSet, SIGINT );
      sigaddset( &signalSet, SIGTERM );

      const int maskResult = pthread_sigmask( SIG_BLOCK, &signalSet, nullptr );
      if( maskResult != 0 )
      {
         throw std::runtime_error( "Failed to block shutdown signals: " + std::to_string( maskResult ) );
      }

      watcherThread = std::jthread{ [ this, signalSet ]( std::stop_token stopToken )
                                    {
                                       while( ! stopToken.stop_requested() )
                                       {
                                          timespec  timeout{ .tv_sec = 0, .tv_nsec = 200000000 };
                                          const int signalNumber = sigtimedwait( &signalSet, nullptr, &timeout );

                                          if( signalNumber == -1 )
                                          {
                                             if( errno == EAGAIN || errno == EINTR )
                                             {
                                                continue;
                                             }

                                             spdlog::error( "Signal waiter failed: {}", errno );
                                             return;
                                          }

                                          onSignalCallback( signalNumber );
                                          return;
                                       }
                                    } };
   }

   SignalShutdownWatcher::~SignalShutdownWatcher() = default;

} // namespace pace
