#pragma once

#include <fmt/format.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace util
{
   enum class ErrorCode : std::uint8_t
   {
      Internal,
      InvalidArgument,
      InvalidPayload,
      ParseFailure,
      NotFound,
      NotConnected,
      SubscriptionFailure,
      PublishFailure,
      CommandFailure,
      PollFailure,
      ConfigurationFailure,
      EntityLifecycleFailure,
      PlatformFailure,
      Unsupported,
   };

   struct Error
   {
         ErrorCode   code{ ErrorCode::Internal };
         std::string message{};

         Error() = default;

         Error( ErrorCode newCode, std::string newMessage )
            : code( newCode )
            , message( std::move( newMessage ) )
         {}

         Error( std::string newMessage )
            : message( std::move( newMessage ) )
         {}

         Error( const char* newMessage )
            : message( newMessage )
         {}

         [[nodiscard]] std::string_view what() const noexcept
         {
            return message;
         }
   };

   template <typename... Args>
   [[nodiscard]] inline Error makeError( ErrorCode code, fmt::format_string<Args...> fmtString, Args&&... args )
   {
      return Error{ code, fmt::format( fmtString, std::forward<Args>( args )... ) };
   }

} // namespace util

template <>
struct fmt::formatter<util::Error>
{
      constexpr auto parse( format_parse_context& ctx )
      {
         return ctx.begin();
      }

      template <typename FormatContext>
      auto format( const util::Error& error, FormatContext& ctx ) const
      {
         return fmt::format_to( ctx.out(), "{}", error.message );
      }
};
