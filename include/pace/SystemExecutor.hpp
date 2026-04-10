#pragma once

#include "util/expected.hpp"

#include <string>
#include <string_view>

namespace pace
{
   enum class SystemAction
   {
      Lock,
      Sleep,
      Reboot,
      Shutdown,
   };

   util::expected<bool, std::string> executeSystemAction( SystemAction action );

   std::string_view actionName( SystemAction action );

} // namespace pace