#pragma once

#include "pace/commands/BaseCommand.hpp"

namespace pace::commands
{
   /// @brief Legacy placeholder kept to make arbitrary shell execution unavailable by default.
   class ExecCommand : public BaseCommand<NoResponse, std::string>
   {
      public:

         using BaseCommand::BaseCommand;

         std::string name() const override
         {
            return "exec";
         }

         util::Task<ResponseType> execute( std::string payload ) const override
         {
            (void)payload;
            co_return util::unexpected{ "generic exec is disabled; use predefined commands" };
         }
   };
} // namespace pace::commands
