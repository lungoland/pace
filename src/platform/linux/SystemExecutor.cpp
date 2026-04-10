#include "pace/SystemExecutor.hpp"

#include <fmt/format.h>

#include <cerrno>
#include <cstring>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <vector>

extern char** environ;

namespace pace
{
   namespace
   {
      util::expected<bool, std::string> spawnAndWait( std::vector<std::string> command, std::string_view actionName )
      {
         if( command.empty() )
         {
            return util::unexpected{ "empty command" };
         }

         std::vector<char*> argv;
         argv.reserve( command.size() + 1 );
         for( auto& part : command )
         {
            argv.push_back( part.data() );
         }
         argv.push_back( nullptr );

         pid_t     processId = 0;
         const int spawnRc   = posix_spawnp( &processId, argv.front(), nullptr, nullptr, argv.data(), environ );
         if( spawnRc != 0 )
         {
            return util::unexpected{ fmt::format( "posix_spawnp failed for {}: {}", actionName, std::strerror( spawnRc ) ) };
         }

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

} // namespace pace