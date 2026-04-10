#pragma once

#include "pace/commands/TypedCommand.hpp"

namespace pace::commands
{
   /// @brief Simple command that responds to "ping" with "pong".
   class PingCommand : public TypedCommand<std::string, std::string>
   {
      public:

         using TypedCommand::TypedCommand;

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
