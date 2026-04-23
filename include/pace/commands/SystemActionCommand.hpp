#pragma once

#include "pace/commands/BaseCommand.hpp"
#include "pace/commands/SystemExecutor.hpp"

#include <string>
#include <utility>

namespace pace::commands
{
   /// @brief Executes a system action such as lock, sleep, reboot, or shutdown.
   template <SystemAction Action>
   class SystemActionCommand : public BaseCommand<>
   {
      public:

         using BaseCommand::BaseCommand;

         std::string name() const override
         {
            return std::string{ actionName( Action ) };
         }

         util::Task<ResponseType> execute( NoArgs ) const override
         {
            auto ok = impl::executeSystemAction( Action );
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
