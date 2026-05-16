#pragma once

#include "pace/commands/BaseCommand.hpp"
#include "pace/commands/SystemExecutor.hpp"

namespace pace::commands
{
   /// @brief Shows a toast notification.
   class NotifyCommand : public BaseCommand<NotifyCommand, NoResponse, std::string>
   {
      public:

         static constexpr std::string_view kType = "notify";

         using BaseCommand::BaseCommand;

         [[nodiscard]] entities::EntityType type() const override
         {
            return entities::EntityType::Notify;
         }

         [[nodiscard]] std::string name() const override
         {
            return "notify";
         }

         [[nodiscard]] util::Task<ResponseType> execute( std::string message ) const override
         {
            impl::sendNotification( message );
            co_return {};
         }
   };
} // namespace pace::commands
