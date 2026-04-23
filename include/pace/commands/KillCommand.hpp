#pragma once

#include "pace/commands/BaseCommand.hpp"
#include "pace/commands/SystemExecutor.hpp"

namespace pace::commands
{
   /// @brief Kills a process by name.
   class KillCommand : public BaseCommand<NoResponse, std::string>
   {
      public:

         using BaseCommand::BaseCommand;

         std::string name() const override
         {
            return "kill";
         }

         util::Task<ResponseType> execute( std::string proc ) const override
         {
            impl::killProcessByName( proc );
            co_return {};
         }
   };
} // namespace pace::commands
