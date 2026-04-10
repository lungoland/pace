#include "pace/sensor/SensorProvider.hpp"

namespace pace::sensor
{
   namespace
   {
      class WindowsSensorProvider final : public SensorProvider
      {
         public:

            util::expected<double, std::string> readCpuTemperatureC() const override
            {
               return util::unexpected{ "cpu temperature is not implemented on windows" };
            }

            util::expected<double, std::string> readMemoryUsagePercent() const override
            {
               return util::unexpected{ "memory usage is not implemented on windows" };
            }
      };

   } // namespace

   std::unique_ptr<SensorProvider> createSensorProvider()
   {
      return std::make_unique<WindowsSensorProvider>();
   }

} // namespace pace::sensor
