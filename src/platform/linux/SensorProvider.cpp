#include "pace/sensor/SensorProvider.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

namespace pace::sensor
{
   namespace
   {
      class LinuxSensorProvider final : public SensorProvider
      {
         public:

            util::expected<double, std::string> readCpuTemperatureC() const override
            {
               namespace fs = std::filesystem;

               const fs::path thermalRoot{ "/sys/class/thermal" };
               if( fs::exists( thermalRoot ) )
               {
                  for( const auto& entry : fs::directory_iterator( thermalRoot ) )
                  {
                     if( ! entry.is_directory() )
                     {
                        continue;
                     }

                     const auto dirName = entry.path().filename().string();
                     if( ! dirName.starts_with( "thermal_zone" ) )
                     {
                        continue;
                     }

                     const auto tempPath = entry.path() / "temp";
                     if( const auto milliC = readIntegerFile( tempPath.string() ); milliC )
                     {
                        return static_cast<double>( *milliC ) / 1000.0;
                     }
                  }
               }

               const fs::path hwmonRoot{ "/sys/class/hwmon" };
               if( fs::exists( hwmonRoot ) )
               {
                  for( const auto& entry : fs::directory_iterator( hwmonRoot ) )
                  {
                     if( ! entry.is_directory() )
                     {
                        continue;
                     }

                     for( int index = 1; index <= 10; ++index )
                     {
                        const auto tempPath = entry.path() / fmt::format( "temp{}_input", index );
                        if( const auto milliC = readIntegerFile( tempPath.string() ); milliC )
                        {
                           return static_cast<double>( *milliC ) / 1000.0;
                        }
                     }
                  }
               }

               return util::unexpected{ "cpu temperature source not found" };
            }

            util::expected<double, std::string> readMemoryUsagePercent() const override
            {
               std::ifstream meminfo{ "/proc/meminfo" };
               if( ! meminfo )
               {
                  return util::unexpected{ "failed to open /proc/meminfo" };
               }

               long long   totalKb     = 0;
               long long   availableKb = 0;
               std::string line;
               while( std::getline( meminfo, line ) )
               {
                  if( line.starts_with( "MemTotal:" ) )
                  {
                     totalKb = parseMeminfoValue( line );
                     continue;
                  }

                  if( line.starts_with( "MemAvailable:" ) )
                  {
                     availableKb = parseMeminfoValue( line );
                  }
               }

               if( totalKb <= 0 || availableKb < 0 )
               {
                  return util::unexpected{ "failed to parse memory usage from /proc/meminfo" };
               }

               const auto usedKb = std::max<long long>( 0, totalKb - availableKb );
               return static_cast<double>( usedKb ) * 100.0 / static_cast<double>( totalKb );
            }

         private:

            static std::optional<long long> readIntegerFile( const std::string& path )
            {
               std::ifstream in{ path };
               if( ! in )
               {
                  return std::nullopt;
               }

               long long value = 0;
               in >> value;
               if( in.fail() )
               {
                  return std::nullopt;
               }

               return value;
            }

            static long long parseMeminfoValue( const std::string& line )
            {
               std::string digits;
               digits.reserve( 32 );

               for( const auto c : line )
               {
                  if( std::isdigit( static_cast<unsigned char>( c ) ) )
                  {
                     digits.push_back( c );
                  }
               }

               if( digits.empty() )
               {
                  return -1;
               }

               return std::stoll( digits );
            }
      };

   } // namespace

   std::unique_ptr<SensorProvider> createSensorProvider()
   {
      return std::make_unique<LinuxSensorProvider>();
   }

} // namespace pace::sensor
