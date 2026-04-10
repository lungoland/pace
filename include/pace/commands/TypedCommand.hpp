#pragma once

#include "pace/commands/BaseCommand.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <concepts>
#include <string>
#include <type_traits>

namespace pace::commands
{
   /// Sentinel type for commands that produce no response.
   struct NoResponse
   {};

   /// @brief Typed middle layer between BaseCommand and concrete commands.
   ///
   /// Concrete commands derive from TypedCommand<TRequest, TResponse> and
   /// implement only execute().  The subscribe/execute dispatch and
   /// payload (de)serialization are handled here.
   ///
   /// TRequest: parsed input type, or NoArgs for commands with no payload.
   /// TResponse: response type (std::string, nlohmann-serializable, or NoResponse).

   /// Sentinel type for commands that take no input payload.
   struct NoArgs
   {};

   template <typename TResponse = NoResponse, typename TRequest = NoArgs>
   class TypedCommand : public BaseCommand
   {
      public:

         using BaseCommand::BaseCommand;
         using ResponseType = util::expected<TResponse, std::string>;

         util::Task<BaseCommand::ResponseType> execute( DataType payload ) const final
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

         static BaseCommand::ResponseType toResponseType( const ResponseType& response )
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
