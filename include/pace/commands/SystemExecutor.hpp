#pragma once

#include "util/expected.hpp"

#include <string>
#include <string_view>

namespace pace::commands
{
   enum class SystemAction
   {
      Lock,
      Sleep,
      Reboot,
      Shutdown,
   };

   std::string_view actionName( SystemAction action );

   namespace impl
   {
      util::expected<bool, std::string> executeSystemAction( SystemAction action );

      util::expected<bool, std::string> killProcessByName( const std::string& processName );

      util::expected<bool, std::string> sendNotification( const std::string& message );

   } // namespace impl
} // namespace pace::commands