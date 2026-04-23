#pragma once

#include "pace/commands/BaseCommand.hpp"
#include "pace/commands/SystemExecutor.hpp"

namespace pace::commands
{
   /// @brief Shows a toast notification.
   class NotifyCommand : public BaseCommand<NoResponse, std::string>
   {
      public:

         using BaseCommand::BaseCommand;

         std::string name() const override
         {
            return "notify";
         }

         util::Task<ResponseType> execute( std::string message ) const override
         {
            impl::sendNotification( message );
            co_return {};
         }
   };
} // namespace pace::commands
