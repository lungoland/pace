#pragma once

#include "pace/sensor/BaseSensor.hpp"
#include "util/Task.hpp"

#include <memory>
#include <vector>

namespace util
{
   class PeriodicScheduler;
}

namespace pace
{
   class SensorTimer
   {
      public:

         explicit SensorTimer( const std::vector<sensor::BaseSensorPtr>& sensors );
         ~SensorTimer();

         SensorTimer( const SensorTimer& )            = delete;
         SensorTimer& operator=( const SensorTimer& ) = delete;
         SensorTimer( SensorTimer&& )                 = delete;
         SensorTimer& operator=( SensorTimer&& )      = delete;

         util::Task<bool> stop();

      private:

         std::unique_ptr<util::PeriodicScheduler> scheduler;
   };

} // namespace pace
