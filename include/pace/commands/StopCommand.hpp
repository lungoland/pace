#pragma once

#include "pace/commands/TypedCommand.hpp"

namespace pace::commands
{
   /// @brief Simple command that stops the service.
   class StopCommand : public TypedCommand<>
   {
      public:

         StopCommand( MqttService& mqttService, pace::Pace& pace )
            : TypedCommand( mqttService )
            , service( pace )
         {}

         std::string name() const override
         {
            return "stop";
         }

         util::Task<ResponseType> execute( NoArgs ) const override
         {
            co_await service.stop();
            co_return {};
         }

      private:

         pace::Pace& service;
   };
} // namespace pace::commands
