#pragma once

#include "util/Task.hpp"
#include "util/expected.hpp"

#include <memory>
#include <optional>
#include <string>

namespace pace
{
   class MqttService;
}

namespace pace::commands
{
   /// @brief Base class for MQTT commands.
   ///
   /// BaseCommand handles MQTT subscription and response publishing.
   /// Concrete commands derive from BaseCommand and implement name() and execute().
   /// The command topic is "command/{name()}/set" and the response topic is "command/{name()}/status".
   class BaseCommand
   {
      public:

         using ResponseType = util::expected<std::optional<std::string>, std::string>;
         using DataType     = std::optional<std::string>;

         explicit BaseCommand( MqttService& mqttService );
         virtual ~BaseCommand() = default;

         util::Task<bool> subscribe() const;

         virtual std::string              name() const                      = 0;
         virtual util::Task<ResponseType> execute( DataType payload ) const = 0;

      private:

         util::Task<bool> handleResponse( ResponseType response ) const;

         MqttService& mqtt;
   };
   using BaseCommandPtr = std::unique_ptr<BaseCommand>;
} // namespace pace::commands
