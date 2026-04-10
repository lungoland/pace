#pragma once

#include "pace/sensor/BaseSensor.hpp"

#include <string>
#include <type_traits>

namespace pace::sensor
{
   /// @brief A sensor that returns a string value.
   template <typename T>
   class TypedSensor : public BaseSensor
   {
      public:

         using BaseSensor::BaseSensor;

         std::string fetch_() const override
         {
            if constexpr( std::is_same_v<T, std::string> )
            {
               return fetch();
            }
            return std::to_string( fetch() );
         }

         virtual T fetch() const = 0;
   };
} // namespace pace::sensor
