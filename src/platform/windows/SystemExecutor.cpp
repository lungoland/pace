#include "pace/commands/SystemExecutor.hpp"
#include "pace/sensors/SensorProvider.hpp"

#include "WinHandle.hpp"

#include <fmt/format.h>

#include <powrprof.h>

#ifdef _MSC_VER
#pragma comment( lib, "PowrProf.lib" )
#endif

namespace pace::commands::impl
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

   util::expected<bool, std::string> killProcessByName( const std::string& processName )
   {
      auto pids = sensors::impl::findPidsByName( processName );
      for( auto pid : pids )
      {
         pace::win::UniqueHandle processHandle{ OpenProcess( PROCESS_TERMINATE, FALSE, pid ) };
         if( processHandle )
         {
            TerminateProcess( processHandle.get(), 1 );
            return true;
         }
      }
      return util::unexpected{ fmt::format( "No process found with name {}", processName ) };
   }

   util::expected<bool, std::string> sendNotification( const std::string& message )
   {
      // Windows does not have a built-in command line tool for sending notifications, so we use PowerShell to display a toast notification.
      // Note: This requires Windows 10 or later and may not work if the user has disabled toast notifications.
      std::string command = fmt::format( "powershell -Command \"[Windows.UI.Notifications.ToastNotificationManager, "
                                         "Windows.UI.Notifications, ContentType = WindowsRuntime] > $null;"
                                         "$template = [Windows.UI.Notifications.ToastTemplateType]::ToastText01;"
                                         "$toastXml = [Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent($template);"
                                         "$textNodes = $toastXml.GetElementsByTagName('text');"
                                         "$textNodes.Item(0).AppendChild($toastXml.CreateTextNode('{}')) > $null;"
                                         "$toast = [Windows.UI.Notifications.ToastNotification]::new($toastXml);"
                                         "[Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier('Pace').Show($toast)\"",
                                         message );

      STARTUPINFOA        si{};
      PROCESS_INFORMATION pi{};

      si.cb = sizeof( si );
      if( ! CreateProcessA( nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi ) )
      {
         return windowsError( "CreateProcessA" );
      }

      WaitForSingleObject( pi.hProcess, INFINITE );
      CloseHandle( pi.hProcess );
      CloseHandle( pi.hThread );

      return true;
   }


} // namespace pace::commands::impl