#pragma once

#include <string>

namespace pace
{

   struct Config
   {
         std::string brokerUri{ "tcp://localhost:1883" };
         std::string clientId{ "pace-agent" };
         std::string nodeId{ "pace-node" };
         std::string username{ "" };
         std::string password{ "" };
         int         qos{ 1 };
         bool        dryRun{ true };

         static Config fromEnvironment();
   };

} // namespace pace
