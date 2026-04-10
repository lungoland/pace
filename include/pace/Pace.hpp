#pragma once

#include "pace/Config.hpp"
#include "pace/MqttService.hpp"
#include "pace/SensorTimer.hpp"

#include "pace/commands/BaseCommand.hpp"
#include "pace/sensor/BaseSensor.hpp"

#include "util/AsyncTaskDispatcher.hpp"
#include "util/Task.hpp"

namespace pace
{
   class Pace
   {
      public:

         explicit Pace( const Config& cfg, util::AsyncTaskDispatcher& dispatcher );
         ~Pace() = default;

         util::Task<bool> start();
         util::Task<bool> stop();

      private:

         Config config;

         util::AsyncTaskDispatcher&   dispatcher;
         MqttService                  mqtt;
         std::unique_ptr<SensorTimer> sensorTimer;

         std::vector<pace::commands::BaseCommandPtr> commands;
         std::vector<pace::sensor::BaseSensorPtr>    sensors;
   };
} // namespace pace
