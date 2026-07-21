#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "util/Logger.hpp"
#include "util/Task.hpp"
#include "util/expected.hpp"

namespace pace
{
   class MqttService;
   class Pace;

   class EntityFactory
   {
      public:

         using OperationResult = util::VoidResult;

         explicit EntityFactory( Pace& pace, MqttService& mqtt );

         /// @brief Subscribes to entity config topics and creates/updates entities.
         /// Topic formats:
         /// - entity/{entityName}/config (full config as JSON object)
         /// - entity/{entityName}/config/{configNode} (individual field updates)
         util::Task<OperationResult> subscribe();

      private:

         util::Task<OperationResult> onFullConfig( const std::string& name, nlohmann::json config );

         util::Logger logger = util::getLogger( "EntityFactory" );

         Pace&        pace;
         MqttService& mqtt;

         /// @brief In-memory storage of partially built or complete entity configs by entity name.
         std::map<std::string, nlohmann::json> entityConfigs{};
   };
}
