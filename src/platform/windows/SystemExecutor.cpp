#include "pace/commands/SystemExecutor.hpp"
#include "pace/sensors/SensorProvider.hpp"

#include "WinHandle.hpp"

#include "util/TimerAwaiter.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <powrprof.h>
#include <unordered_map>

#ifdef _MSC_VER
#pragma comment( lib, "PowrProf.lib" )
#endif

namespace pace::commands::impl
{
   namespace
   {
      using pace::win::UniqueHandle;

      std::mutex                                      groupMutex;
      std::unordered_map<std::uint32_t, UniqueHandle> processGroups;

      util::VoidResult windowsError( const char* operation )
      {
         return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "{} failed with error {}", operation, GetLastError() ) };
      }

      util::Result<PROCESS_INFORMATION> createProcess( const std::string& processName, const DWORD creationFlags )
      {
         STARTUPINFOA        si{};
         PROCESS_INFORMATION pi{};
         std::string         commandLine = processName;

         si.cb = sizeof( si );
         if( ! CreateProcessA( nullptr, commandLine.data(), nullptr, nullptr, FALSE, creationFlags, nullptr, nullptr, &si, &pi ) )
         {
            return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "CreateProcessA failed with error {}",
                                                      GetLastError() ) };
         }

         return pi;
      }

      util::Result<UniqueHandle> createKillOnCloseJob()
      {
         UniqueHandle job{ CreateJobObjectA( nullptr, nullptr ) };
         if( ! job )
         {
            return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "CreateJobObjectA failed with error {}",
                                                      GetLastError() ) };
         }

         JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
         info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
         if( ! SetInformationJobObject( job.get(), JobObjectExtendedLimitInformation, &info, sizeof( info ) ) )
         {
            return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "SetInformationJobObject failed with error {}",
                                                      GetLastError() ) };
         }

         return std::move( job );
      }
   }

   util::VoidResult executeSystemAction( SystemAction action )
   {
      switch( action )
      {
         case SystemAction::Lock :
            if( LockWorkStation() != 0 )
            {
               return {};
            }
            return windowsError( "LockWorkStation" );

         case SystemAction::Sleep :
            if( SetSuspendState( FALSE, TRUE, FALSE ) != 0 )
            {
               return {};
            }
            return windowsError( "SetSuspendState" );

         case SystemAction::Reboot :
            if( ExitWindowsEx( EWX_REBOOT | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER ) != 0 )
            {
               return {};
            }
            return windowsError( "ExitWindowsEx(reboot)" );

         case SystemAction::Shutdown :
            if( ExitWindowsEx( EWX_POWEROFF | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OTHER ) != 0 )
            {
               return {};
            }
            return windowsError( "ExitWindowsEx(shutdown)" );
      }

      return util::unexpected{ util::makeError( util::ErrorCode::Unsupported, "unsupported action" ) };
   }


   util::VoidResult spawnNewProcess( const std::string& processName )
   {
      auto created = createProcess( processName, CREATE_NO_WINDOW );
      if( ! created )
      {
         return util::unexpected{ created.error() };
      }

      UniqueHandle process{ created->hProcess };
      UniqueHandle thread{ created->hThread };

      return {};
   }

   util::Result<std::int64_t> spawnNewProcessGroup( const std::string& processName )
   {
      auto created = createProcess( processName, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED );
      if( ! created )
      {
         return util::unexpected{ created.error() };
      }

      UniqueHandle process{ created->hProcess };
      UniqueHandle thread{ created->hThread };

      auto jobResult = createKillOnCloseJob();
      if( ! jobResult )
      {
         TerminateProcess( process.get(), 1 );
         return util::unexpected{ jobResult.error() };
      }

      auto job = std::move( *jobResult );

      if( ! AssignProcessToJobObject( job.get(), process.get() ) )
      {
         const auto error = GetLastError();
         TerminateProcess( process.get(), 1 );
         return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "AssignProcessToJobObject failed with error {}",
                                                   error ) };
      }

      if( ResumeThread( thread.get() ) == static_cast<DWORD>( -1 ) )
      {
         const auto error = GetLastError();
         TerminateProcess( process.get(), 1 );
         return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "ResumeThread failed with error {}", error ) };
      }

      const auto groupId = static_cast<std::uint32_t>( created->dwProcessId );
      {
         std::scoped_lock lock{ groupMutex };
         processGroups.insert_or_assign( groupId, std::move( job ) );
      }

      return static_cast<std::int64_t>( groupId );
   }

   util::Task<util::VoidResult> killProcessByName( const std::string& processName )
   {
      auto pids = sensors::impl::findPidsByName( processName );
      for( auto pid : pids )
      {
         pace::win::UniqueHandle processHandle{ OpenProcess( PROCESS_TERMINATE, FALSE, pid ) };
         if( processHandle )
         {
            TerminateProcess( processHandle.get(), 1 );
            co_return {};
         }
      }
      co_return util::unexpected{ util::makeError( util::ErrorCode::NotFound, "No process found with name {}", processName ) };
   }

   util::Task<util::VoidResult> killProcessGroup( const std::int64_t processGroupId )
   {
      if( processGroupId <= 0 )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::InvalidArgument, "invalid process group id '{}'", processGroupId ) };
      }

      const auto groupId = static_cast<std::uint32_t>( processGroupId );

      UniqueHandle groupHandle;
      {
         std::scoped_lock lock{ groupMutex };
         if( auto it = processGroups.find( groupId ); it != processGroups.end() )
         {
            groupHandle = std::move( it->second );
            processGroups.erase( it );
         }
      }

      if( groupHandle )
      {
         if( ! TerminateJobObject( groupHandle.get(), 1 ) )
         {
            co_return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "TerminateJobObject failed with error {}",
                                                         GetLastError() ) };
         }
      }
      else
      {
         auto processHandle = UniqueHandle{ OpenProcess( PROCESS_TERMINATE, FALSE, groupId ) };
         if( ! processHandle )
         {
            co_return util::unexpected{ util::makeError( util::ErrorCode::NotFound, "no process found for process group id '{}'",
                                                         processGroupId ) };
         }

         if( ! TerminateProcess( processHandle.get(), 1 ) )
         {
            co_return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "TerminateProcess failed with error {}",
                                                         GetLastError() ) };
         }
      }

      for( int attempt = 0; attempt < 10; ++attempt )
      {
         if( sensors::impl::findPidsByProcessGroup( processGroupId ).empty() )
         {
            co_return {};
         }

         co_await util::sleep_for( std::chrono::milliseconds{ 100 } );
      }

      auto remaining = sensors::impl::findPidsByProcessGroup( processGroupId );
      co_return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "process group '{}' still running (pids: {})",
                                                   processGroupId, fmt::join( remaining, ", " ) ) };
   }

   util::VoidResult sendNotification( const std::string& message )
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

      return {};
   }


} // namespace pace::commands::impl