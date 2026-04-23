#include "pace/commands/SystemExecutor.hpp"

namespace pace::commands
{

   std::string_view actionName( SystemAction action )
   {
      switch( action )
      {
         case SystemAction::Lock : return "lock";
         case SystemAction::Sleep : return "sleep";
         case SystemAction::Reboot : return "reboot";
         case SystemAction::Shutdown : return "shutdown";
      }

      return "unknown";
   }

} // namespace pace
