#include "pace/SystemExecutor.hpp"

#include <fmt/format.h>

#define WIN32_LEAN_AND_MEAN
#include <powrprof.h>
#include <windows.h>

#pragma comment( lib, "PowrProf.lib" )

namespace pace
{
   namespace
   {
      util::expected<bool, std::string> windowsError( const char* operation )
      {
         return util::unexpected{ fmt::format( "{} failed with error {}", operation, GetLastError() ) };
      }
   }

   util::expected<bool, std::string> executeSystemAction( SystemAction action )
   {
      switch( action )
      {
         case SystemAction::Lock :
            if( LockWorkStation() != 0 )
            {
               return true;
            }
            return windowsError( "LockWorkStation" );

         case SystemAction::Sleep :
            if( SetSuspendState( FALSE, TRUE, FALSE ) != 0 )
            {
               return true;
            }
            return windowsError( "SetSuspendState" );

         case SystemAction::Reboot :
            if( ExitWindowsEx( EWX_REBOOT | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER ) != 0 )
            {
               return true;
            }
            return windowsError( "ExitWindowsEx(reboot)" );

         case SystemAction::Shutdown :
            if( ExitWindowsEx( EWX_POWEROFF | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER ) != 0 )
            {
               return true;
            }
            return windowsError( "ExitWindowsEx(shutdown)" );
      }

      return util::unexpected{ "unsupported action" };
   }

} // namespace pace