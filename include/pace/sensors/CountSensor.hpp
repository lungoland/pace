#pragma once

#include "pace/sensors/BaseSensor.hpp"

namespace pace::sensors
{
   /// @brief A sensor that returns a count value.
   class CountSensor : public BaseSensor<int, config::BaseSensorConfig>
   {
      public:

         using BaseSensor::BaseSensor;

         util::Task<int> fetch() const override
         {
            static int count = 0;
            co_return count++;
         }
   };
} // namespace pace::sensors
