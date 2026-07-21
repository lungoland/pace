#pragma once

#include "pace/commands/BaseCommand.hpp"

namespace pace::commands
{
   /// @brief Simple command that stops the service.
   class StopCommand : public BaseCommand<StopCommand>
   {
      public:

         static constexpr std::string_view kType = "stop";

         StopCommand( MqttService& mqttService, pace::Pace& pace )
            : BaseCommand( mqttService )
            , service( pace )
         {}

         std::string name() const override
         {
            return "stop";
         }

         util::Task<ResponseType> execute( NoArgs ) const override
         {
            auto result = co_await service.stop();
            if( ! result )
            {
               co_return util::unexpected{ result.error() };
            }
            co_return {};
         }

      private:

         pace::Pace& service;
   };
} // namespace pace::commands
