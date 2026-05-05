#include "pace/commands/SystemExecutor.hpp"
#include "pace/sensors/SensorProvider.hpp"

#include "util/TimerAwaiter.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <vector>

extern char** environ;

namespace pace::commands::impl
{
   namespace
   {
      std::vector<char*> buildArgv( std::vector<std::string>& command )
      {
         std::vector<char*> argv;
         argv.reserve( command.size() + 1 );
         for( auto& part : command )
         {
            argv.push_back( part.data() );
         }
         argv.push_back( nullptr );
         return argv;
      }

      util::expected<pid_t, std::string> spawnProcess( std::vector<std::string>& command, std::string_view actionName )
      {
         if( command.empty() )
         {
            return util::unexpected{ "empty command" };
         }

         auto argv = buildArgv( command );

         pid_t     processId = 0;
         const int spawnRc   = posix_spawnp( &processId, argv.front(), nullptr, nullptr, argv.data(), environ );
         if( spawnRc != 0 )
         {
            return util::unexpected{ fmt::format( "posix_spawnp failed for {}: {}", actionName, std::strerror( spawnRc ) ) };
         }

         return processId;
      }

      util::expected<bool, std::string> spawnAndWait( std::vector<std::string> command, std::string_view actionName )
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
            return util::unexpected{ fmt::format( "waitpid failed for {}: {}", actionName, std::strerror( errno ) ) };
         }

         if( WIFEXITED( status ) && WEXITSTATUS( status ) == 0 )
         {
            return true;
         }

         if( WIFEXITED( status ) )
         {
            return util::unexpected{ fmt::format( "{} exited with status {}", actionName, WEXITSTATUS( status ) ) };
         }

         if( WIFSIGNALED( status ) )
         {
            return util::unexpected{ fmt::format( "{} terminated by signal {}", actionName, WTERMSIG( status ) ) };
         }

         return util::unexpected{ fmt::format( "{} failed with unknown process status", actionName ) };
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

   util::expected<bool, std::string> executeSystemAction( SystemAction action )
   {
      switch( action )
      {
         case SystemAction::Lock : return spawnAndWait( { "loginctl", "lock-session" }, actionName( action ) );
         case SystemAction::Sleep : return spawnAndWait( { "systemctl", "suspend" }, actionName( action ) );
         case SystemAction::Reboot : return spawnAndWait( { "systemctl", "reboot" }, actionName( action ) );
         case SystemAction::Shutdown : return spawnAndWait( { "systemctl", "poweroff" }, actionName( action ) );
      }

      return util::unexpected{ "unsupported action" };
   }

   util::expected<bool, std::string> spawnNewProcess( const std::string& imagePath )
   {
      auto command = std::vector<std::string>{ imagePath };

      /// TODO: Pipe to dev/null
      /// TODO: is posix_spawnp even a good api? plain old fork/exec?
      auto spawned = spawnProcess( command, imagePath );
      if( ! spawned )
      {
         return util::unexpected{ spawned.error() };
      }

      return true; // or pid?
   }

   util::Task<util::expected<bool, std::string>> killProcessByName( const std::string& processName )
   {
      auto pids = sensors::impl::findPidsByName( processName );
      if( pids.empty() )
      {
         co_return util::unexpected{ fmt::format( "no process found with name '{}'", processName ) };
      }

      auto signalErrors = sendSignal( pids, SIGTERM );

      if( ! signalErrors.empty() )
      {
         co_return util::unexpected{ fmt::format( "failed to send SIGTERM: {}", fmt::join( signalErrors, "; " ) ) };
      }

      auto remaining = co_await waitForExit( pids, std::chrono::seconds{ 2 } );
      if( remaining.empty() )
      {
         co_return true;
      }

      signalErrors = sendSignal( remaining, SIGKILL );

      if( ! signalErrors.empty() )
      {
         co_return util::unexpected{ fmt::format( "failed to send SIGKILL: {}", fmt::join( signalErrors, "; " ) ) };
      }

      remaining = co_await waitForExit( pids, std::chrono::seconds{ 1 } );
      if( ! remaining.empty() )
      {
         co_return util::unexpected{ fmt::format( "process '{}' still running after SIGKILL (pids: {})", processName,
                                                  fmt::join( remaining, ", " ) ) };
      }

      co_return true;
   }

   util::expected<bool, std::string> sendNotification( const std::string& message )
   {
      return spawnAndWait( { "notify-send", "-a", "Pace", message }, "send notification" );
   }

} // namespace pace::commands::impl