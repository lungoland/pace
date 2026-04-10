#pragma once

#include "pace/commands/TypedCommand.hpp"

namespace pace::commands
{
   /// @brief No-op command for testing and as a template for new commands.
   class NullCommand : public TypedCommand<>
   {
      public:

         using TypedCommand::TypedCommand;

         std::string name() const override
         {
            return "null";
         }

         util::Task<ResponseType> execute( NoArgs ) const override
         {
            co_return {};
         }
   };
} // namespace pace::commands
