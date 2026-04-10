#pragma once

#include "pace/SystemExecutor.hpp"
#include "pace/commands/TypedCommand.hpp"

#include <string>
#include <utility>

namespace pace::commands
{
   template <SystemAction Action>
   class SystemActionCommand : public TypedCommand<>
   {
      public:

         using TypedCommand::TypedCommand;

         std::string name() const override
         {
            return std::string{ pace::actionName( Action ) };
         }

         util::Task<ResponseType> execute( NoArgs ) const override
         {
            auto ok = pace::executeSystemAction( Action );
            if( ! ok )
            {
               co_return util::unexpected{ ok.error() };
            }
            co_return {};
         }
   };

   using LockCommand     = SystemActionCommand<SystemAction::Lock>;
   using SleepCommand    = SystemActionCommand<SystemAction::Sleep>;
   using RebootCommand   = SystemActionCommand<SystemAction::Reboot>;
   using ShutdownCommand = SystemActionCommand<SystemAction::Shutdown>;
} // namespace pace::commands
