#include "pace/commands/BaseCommand.hpp"
#include "pace/MqttService.hpp"

#include <fmt/base.h>
#include <fmt/format.h>
#include <mqtt/message.h>
#include <spdlog/spdlog.h>

#include <coroutine>
#include <string>

namespace pace::commands
{
   CommandInterface::CommandInterface( MqttService& mqttService )
      : mqtt( mqttService )
   {}

   util::Task<bool> CommandInterface::subscribe() const
   {
      co_await mqtt.subscribe( fmt::format( "command/{}/set", name() ),
                               [ this ]( mqtt::const_message_ptr msg ) -> util::Task<bool>
                               {
                                  auto response = co_await execute( msg->get_payload_str() );
                                  co_return co_await handleResponse( response );
                               } );
      co_return true;
   }

   util::Task<bool> CommandInterface::handleResponse( ResponseType response ) const
   {
      if( ! response )
      {
         spdlog::error( "Command {} execution failed: {}", name(), response.error() );
         // error object??
         // co_await mqtt.publish( fmt::format( "command/{}/status", name() ), response->value() );
         co_return false;
      }

      if( response->has_value() )
      {
         co_await mqtt.publish( fmt::format( "command/{}/status", name() ), response->value() );
      }
      co_return true;
   }
} // namespace pace::commands
