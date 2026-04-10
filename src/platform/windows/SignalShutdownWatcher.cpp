#include "pace/SignalShutdownWatcher.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace pace
{
   namespace
   {
      std::atomic_bool                    g_signalReceived{ false };
      std::atomic_int                     g_signalNumber{ 0 };
      std::atomic<SignalShutdownWatcher*> g_activeInstance{ nullptr };

      BOOL WINAPI consoleCtrlHandler( DWORD ctrlType )
      {
         if( g_activeInstance.load() == nullptr )
         {
            return FALSE;
         }

         switch( ctrlType )
         {
            case CTRL_C_EVENT :
            case CTRL_BREAK_EVENT :
            case CTRL_CLOSE_EVENT :
            case CTRL_SHUTDOWN_EVENT :
               g_signalNumber.store( static_cast<int>( ctrlType ) );
               g_signalReceived.store( true );
               return TRUE;
            default : return FALSE;
         }
      }
   } // namespace

   SignalShutdownWatcher::SignalShutdownWatcher( SignalHandler onSignal )
      : onSignalCallback( std::move( onSignal ) )
   {
      g_activeInstance.store( this );

      if( ! SetConsoleCtrlHandler( &consoleCtrlHandler, TRUE ) )
      {
         g_activeInstance.store( nullptr );
         throw std::runtime_error( "Failed to register console control handler" );
      }

      watcherThread = std::jthread{ [ this ]( std::stop_token stopToken )
                                    {
                                       using namespace std::chrono_literals;

                                       while( ! stopToken.stop_requested() )
                                       {
                                          if( g_signalReceived.exchange( false ) )
                                          {
                                             onSignalCallback( g_signalNumber.load() );
                                             return;
                                          }

                                          std::this_thread::sleep_for( 200ms );
                                       }
                                    } };
   }

   SignalShutdownWatcher::~SignalShutdownWatcher()
   {
      if( g_activeInstance.load() == this )
      {
         SetConsoleCtrlHandler( &consoleCtrlHandler, FALSE );
         g_activeInstance.store( nullptr );
      }
   }

} // namespace pace