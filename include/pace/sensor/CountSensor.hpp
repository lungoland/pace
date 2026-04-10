#pragma once

#include "pace/sensor/TypedSensor.hpp"

namespace pace::sensor
{
   /// @brief A sensor that returns a count value.
   class CountSensor : public TypedSensor<int>
   {
      public:

         using TypedSensor::TypedSensor;

         std::string name() const override
         {
            return "count";
         }

         int fetch() const override
         {
            static int count = 0;
            return count++;
         }
   };
} // namespace pace::sensor
