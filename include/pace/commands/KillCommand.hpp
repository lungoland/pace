#pragma once

#include "pace/commands/BaseCommand.hpp"
#include "pace/commands/SystemExecutor.hpp"

namespace pace::commands
{
   /// @brief Kills a process by name.
   /// TODO unfiy with proc sensor and move to switch
   class KillCommand : public BaseCommand<KillCommand, NoResponse, std::string>
   {
      public:

         static constexpr std::string_view kType = "kill";

         using BaseCommand::BaseCommand;

         [[nodiscard]] std::string name() const override
         {
            return "kill";
         }

         [[nodiscard]] util::Task<ResponseType> execute( std::string proc ) const override
         {
            auto result = co_await impl::killProcessByName( proc );
            if( ! result )
            {
               co_return util::unexpected{ result.error() };
            }
            co_return {};
         }
   };
} // namespace pace::commands
