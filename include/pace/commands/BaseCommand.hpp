#pragma once

#include "util/Task.hpp"
#include "util/expected.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

namespace pace
{
   class MqttService;
}

namespace pace::commands
{
   /// @brief Base class for MQTT commands.
   ///
   /// CommandInterface handles MQTT subscription and response publishing.
   /// Concrete commands derive from CommandInterface and implement name() and execute().
   /// The command topic is "command/{name()}/set" and the response topic is "command/{name()}/status".
   class CommandInterface
   {
      public:

         using ResponseType = util::expected<std::optional<std::string>, std::string>;
         using DataType     = std::optional<std::string>;

         explicit CommandInterface( MqttService& mqttService );
         virtual ~CommandInterface() = default;

         util::Task<bool> subscribe() const;

         virtual std::string              name() const                      = 0;
         virtual util::Task<ResponseType> execute( DataType payload ) const = 0;

      private:

         util::Task<bool> handleResponse( ResponseType response ) const;

         MqttService& mqtt;
   };
   using CommandPtr = std::unique_ptr<CommandInterface>;

   /// Sentinel type for commands that produce no response.
   struct NoResponse
   {};

   /// @brief Typed middle layer between CommandInterface and concrete commands.
   ///
   /// Concrete commands derive from BaseCommand<TRequest, TResponse> and
   /// implement only execute().  The subscribe/execute dispatch and
   /// payload (de)serialization are handled here.
   ///
   /// TRequest: parsed input type, or NoArgs for commands with no payload.
   /// TResponse: response type (std::string, nlohmann-serializable, or NoResponse).

   /// Sentinel type for commands that take no input payload.
   struct NoArgs
   {};

   template <typename TResponse = NoResponse, typename TRequest = NoArgs>
   class BaseCommand : public CommandInterface
   {
      public:

         using CommandInterface::CommandInterface;
         using ResponseType = util::expected<TResponse, std::string>;

         util::Task<CommandInterface::ResponseType> execute( DataType payload ) const final
         {
            if constexpr( std::same_as<TRequest, NoArgs> )
            {
               co_return toResponseType( co_await execute( NoArgs{} ) );
            }
            else if constexpr( std::same_as<TRequest, std::string> )
            {
               auto response = co_await execute( std::move( *payload ) );
               co_return toResponseType( response );
            }
            else
            {
               /// TODO: with this here, I guess the specialized subscribe can be removed
               auto json = nlohmann::json::parse( *payload, nullptr, false );
               if( json.is_discarded() )
               {
                  /// TODO: Test null json
                  co_return util::unexpected{ "invalid JSON payload" };
               }
               auto data     = json.get<TRequest>();
               auto response = co_await execute( std::move( data ) );
               co_return toResponseType( response );
            }
         }

         virtual util::Task<ResponseType> execute( TRequest request ) const = 0;

      private:

         static CommandInterface::ResponseType toResponseType( const ResponseType& response )
         {
            if( ! response )
            {
               return util::unexpected{ response.error() };
            }

            if constexpr( std::same_as<TResponse, NoResponse> )
            {
               return std::nullopt;
            }
            else if constexpr( std::same_as<TResponse, std::string> )
            {
               return std::optional<std::string>{ response.value() };
            }
            else
            {
               return std::optional<std::string>{ nlohmann::json( response.value() ).dump() };
            }
         }
   };
} // namespace pace::commands
