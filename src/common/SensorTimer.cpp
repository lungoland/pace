#include "pace/SensorTimer.hpp"

#include "util/PeriodicScheduler.hpp"
#include "util/Task.hpp"

#include <coroutine>
#include <fmt/base.h>
#include <functional>

namespace pace
{
   namespace
   {
      std::vector<util::PeriodicScheduler::Job> buildSensorJobs( const std::vector<sensor::BaseSensorPtr>& sensors )
      {
         std::vector<util::PeriodicScheduler::Job> jobs;
         jobs.reserve( sensors.size() );

         for( const auto& sensor : sensors )
         {
            const auto* rawSensor = sensor.get();
            jobs.push_back( util::PeriodicScheduler::Job{
               .name     = rawSensor->name(),
               .interval = rawSensor->interval(),
               .execute  = [ rawSensor ]() -> util::Task<bool> { co_return co_await rawSensor->publish(); },
            } );
         }

         return jobs;
      }
   } // namespace

   SensorTimer::SensorTimer( const std::vector<sensor::BaseSensorPtr>& sensors_ )
      : scheduler( std::make_unique<util::PeriodicScheduler>( buildSensorJobs( sensors_ ) ) )
   {}

   SensorTimer::~SensorTimer()
   {
      util::sync_wait( stop() );
   }

   util::Task<bool> SensorTimer::stop()
   {
      return scheduler->stop();
   }

} // namespace pace
