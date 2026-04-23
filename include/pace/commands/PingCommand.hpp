#pragma once

#include "pace/commands/BaseCommand.hpp"

namespace pace::commands
{
   /// @brief Simple command that responds to "ping" with "pong".
   class PingCommand : public BaseCommand<std::string, std::string>
   {
      public:

         using BaseCommand::BaseCommand;

         std::string name() const override
         {
            return "ping";
         }

         util::Task<ResponseType> execute( std::string request ) const override
         {
            if( request == "ping" )
            {
               co_return "pong";
            }
            else
            {
               co_return util::unexpected{ fmt::format( "unexpected payload: {}", request ) };
            }
         }
   };
} // namespace pace::commands
