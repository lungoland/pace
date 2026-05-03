#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "util/Task.hpp"

namespace pace
{
   class MqttService;
   class Pace;

   class EntityFactory
   {
      public:

         explicit EntityFactory( Pace& p, MqttService& m );

         /// @brief Subscribes to entity config topics and creates/updates entities.
         /// Topic formats:
         /// - entity/{entityName}/config (full config as JSON object)
         /// - entity/{entityName}/config/{configNode} (individual field updates)
         util::Task<bool> subscribe();

      private:

         util::Task<bool> onFullConfig( const std::string& name, const std::string& data );
         util::Task<bool> onFullConfig( const std::string& name, nlohmann::json config );
         util::Task<bool> onPartialConfig( const std::string& name, const std::string& node, const std::string& data );

         Pace&        pace;
         MqttService& mqtt;

         /// @brief In-memory storage of partially built or complete entity configs by entity name.
         std::map<std::string, nlohmann::json> entityConfigs;
   };
}
