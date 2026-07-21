#include "pace/commands/SystemExecutor.hpp"
#include "pace/sensors/SensorProvider.hpp"

#include "util/TimerAwaiter.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace pace::commands::impl
{
   namespace
   {
      std::vector<char*> buildArgv( const std::vector<std::string>& command )
      {
         std::vector<char*> argv;
         argv.reserve( command.size() + 1 );
         for( auto& part : command )
         {
            argv.push_back( const_cast<char*>( part.data() ) );
         }
         argv.push_back( nullptr );
         return argv;
      }

      util::Result<pid_t> spawnProcess( const std::vector<std::string>& command, const std::string_view& actionName,
                                        posix_spawnattr_t* attr = nullptr )
      {
         if( command.empty() )
         {
            return util::unexpected{ util::makeError( util::ErrorCode::InvalidArgument, "empty command" ) };
         }

         // TODO: Why cant this be const?
         auto argv = buildArgv( command );

         pid_t     processId = 0;
         const int spawnRc   = posix_spawnp( &processId, argv.front(), nullptr, attr, argv.data(), environ );
         if( spawnRc != 0 )
         {
            return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "posix_spawnp failed for {}: {}", actionName,
                                                      std::strerror( spawnRc ) ) };
         }

         return processId;
      }

      util::VoidResult spawnAndWait( const std::vector<std::string>& command, const std::string_view& actionName )
      {
         auto spawned = spawnProcess( command, actionName );
         if( ! spawned )
         {
            return util::unexpected{ spawned.error() };
         }

         const auto processId = *spawned;

         int status = 0;
         if( waitpid( processId, &status, 0 ) < 0 )
         {
            return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "waitpid failed for {}: {}", actionName,
                                                      std::strerror( errno ) ) };
         }

         if( WIFEXITED( status ) && WEXITSTATUS( status ) == 0 )
         {
            return {};
         }

         if( WIFEXITED( status ) )
         {
            return util::unexpected{ util::makeError( util::ErrorCode::CommandFailure, "{} exited with status {}", actionName,
                                                      WEXITSTATUS( status ) ) };
         }

         if( WIFSIGNALED( status ) )
         {
            return util::unexpected{ util::makeError( util::ErrorCode::CommandFailure, "{} terminated by signal {}", actionName,
                                                      WTERMSIG( status ) ) };
         }

         return util::unexpected{ util::makeError( util::ErrorCode::CommandFailure, "{} failed with unknown process status", actionName ) };
      }

      template <typename TPid>
      std::vector<std::string> sendSignal( const std::vector<TPid>& pids, const int signalNumber )
      {
         std::vector<std::string> signalErrors;
         for( const auto pid : pids )
         {
            const auto nativePid = static_cast<pid_t>( pid );
            if( kill( nativePid, signalNumber ) != 0 && errno != ESRCH )
            {
               signalErrors.push_back( fmt::format( "pid {}: {}", nativePid, std::strerror( errno ) ) );
            }
         }
         return signalErrors;
      }

      bool processExists( const pid_t pid )
      {
         if( kill( pid, 0 ) == 0 )
         {
            return true;
         }

         return errno == EPERM;
      }

      std::vector<pid_t> collectRunningPids( const std::vector<std::uint32_t>& pids )
      {
         std::vector<pid_t> remaining;
         remaining.reserve( pids.size() );
         for( const auto pid : pids )
         {
            const auto nativePid = static_cast<pid_t>( pid );
            if( processExists( nativePid ) )
            {
               remaining.push_back( nativePid );
            }
         }

         return remaining;
      }

      util::Task<std::vector<pid_t>> waitForExit( const std::vector<std::uint32_t>& pids, const std::chrono::milliseconds timeout )
      {
         const auto pollInterval = std::chrono::milliseconds{ 100 };
         const auto deadline     = std::chrono::steady_clock::now() + timeout;

         for( ;; )
         {
            auto remaining = collectRunningPids( pids );
            if( remaining.empty() )
            {
               co_return remaining;
            }

            if( std::chrono::steady_clock::now() >= deadline )
            {
               co_return remaining;
            }

            co_await util::sleep_for( pollInterval );
         }
      }


   } // namespace

   util::VoidResult executeSystemAction( SystemAction action )
   {
      switch( action )
      {
         case SystemAction::Lock : return spawnAndWait( { "loginctl", "lock-session" }, actionName( action ) );
         case SystemAction::Sleep : return spawnAndWait( { "systemctl", "suspend" }, actionName( action ) );
         case SystemAction::Reboot : return spawnAndWait( { "systemctl", "reboot" }, actionName( action ) );
         case SystemAction::Shutdown : return spawnAndWait( { "systemctl", "poweroff" }, actionName( action ) );
      }

      return util::unexpected{ util::makeError( util::ErrorCode::Unsupported, "unsupported action" ) };
   }

   util::VoidResult spawnNewProcess( const std::string& imagePath )
   {
      auto command = std::vector<std::string>{ imagePath };

      /// TODO: Pipe to dev/null
      /// TODO: is posix_spawnp even a good api? plain old fork/exec?
      auto spawned = spawnProcess( command, imagePath );
      if( ! spawned )
      {
         return util::unexpected{ spawned.error() };
      }

      return {};
   }

   util::Result<std::int64_t> spawnNewProcessGroup( const std::string& imagePath )
   {
      auto              command = std::vector<std::string>{ imagePath };
      posix_spawnattr_t attr{};

      if( const int initRc = posix_spawnattr_init( &attr ); initRc != 0 )
      {
         return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "posix_spawnattr_init failed: {}",
                                                   std::strerror( initRc ) ) };
      }

      auto finalizeAttr = [ &attr ]() { posix_spawnattr_destroy( &attr ); };

      if( const int setFlagsRc = posix_spawnattr_setflags( &attr, POSIX_SPAWN_SETPGROUP ); setFlagsRc != 0 )
      {
         finalizeAttr();
         return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "posix_spawnattr_setflags failed: {}",
                                                   std::strerror( setFlagsRc ) ) };
      }

      // pgroup=0 means create a new group with PGID == child PID.
      if( const int setPgroupRc = posix_spawnattr_setpgroup( &attr, 0 ); setPgroupRc != 0 )
      {
         finalizeAttr();
         return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "posix_spawnattr_setpgroup failed: {}",
                                                   std::strerror( setPgroupRc ) ) };
      }

      auto spawned = spawnProcess( command, imagePath, &attr );
      finalizeAttr();

      if( ! spawned )
      {
         return util::unexpected{ spawned.error() };
      }

      return static_cast<std::int64_t>( *spawned );
   }

   util::Task<util::VoidResult> killProcessByName( const std::string& processName )
   {
      auto pids = sensors::impl::findPidsByName( processName );
      if( pids.empty() )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::NotFound, "no process found with name '{}'", processName ) };
      }

      auto signalErrors = sendSignal( pids, SIGTERM );

      if( ! signalErrors.empty() )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "failed to send SIGTERM: {}",
                                                      fmt::join( signalErrors, "; " ) ) };
      }

      auto remaining = co_await waitForExit( pids, std::chrono::seconds{ 2 } );
      if( remaining.empty() )
      {
         co_return {};
      }

      signalErrors = sendSignal( remaining, SIGKILL );

      if( ! signalErrors.empty() )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "failed to send SIGKILL: {}",
                                                      fmt::join( signalErrors, "; " ) ) };
      }

      remaining = co_await waitForExit( pids, std::chrono::seconds{ 1 } );
      if( ! remaining.empty() )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure,
                                                      "process '{}' still running after SIGKILL (pids: {})", processName,
                                                      fmt::join( remaining, ", " ) ) };
      }

      co_return {};
   }

   util::Task<util::VoidResult> killProcessGroup( const std::int64_t processGroupId )
   {
      if( processGroupId <= 0 )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::InvalidArgument, "invalid process group id '{}'", processGroupId ) };
      }

      auto pids = sensors::impl::findPidsByProcessGroup( processGroupId );
      if( pids.empty() )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::NotFound, "no processes found in process group '{}'",
                                                      processGroupId ) };
      }

      auto signalErrors = sendSignal( pids, SIGTERM );

      if( ! signalErrors.empty() )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "failed to send SIGTERM to process group '{}': {}",
                                                      processGroupId, fmt::join( signalErrors, "; " ) ) };
      }

      auto remaining = co_await waitForExit( pids, std::chrono::seconds{ 2 } );
      if( remaining.empty() )
      {
         co_return {};
      }

      signalErrors = sendSignal( remaining, SIGKILL );

      if( ! signalErrors.empty() )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "failed to send SIGKILL to process group '{}': {}",
                                                      processGroupId, fmt::join( signalErrors, "; " ) ) };
      }

      remaining = co_await waitForExit( pids, std::chrono::seconds{ 1 } );
      if( ! remaining.empty() )
      {
         co_return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure,
                                                      "process group '{}' still running after SIGKILL (pids: {})", processGroupId,
                                                      fmt::join( remaining, ", " ) ) };
      }

      co_return {};
   }

   util::VoidResult sendNotification( const std::string& message )
   {
      return spawnAndWait( { "notify-send", "-a", "Pace", message }, "send notification" );
   }

} // namespace pace::commands::impl