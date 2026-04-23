#include "pace/sensors/SensorProvider.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

namespace pace::sensors::impl
{
   namespace
   {
      std::optional<long long> readIntegerFile( const std::filesystem::path& path )
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

      long long parseMeminfoValue( const std::string& line )
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
   }

   util::expected<double, std::string> readCpuTemperatureC()
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
            if( const auto milliC = readIntegerFile( tempPath ); milliC )
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
               if( const auto milliC = readIntegerFile( tempPath ) )
               {
                  return static_cast<double>( *milliC ) / 1000.0;
               }
            }
         }
      }

      return util::unexpected{ "cpu temperature source not found" };
   }

   util::expected<double, std::string> readMemoryUsagePercent()
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
            {
               continue;
            }
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

   std::vector<std::uint32_t> findPidsByName( const std::string& processName )
   {
      namespace fs = std::filesystem;

      std::vector<std::uint32_t> pids;
      for( const auto& entry : fs::directory_iterator( "/proc" ) )
      {
         const auto filename = entry.path().filename().string();
         if( ! std::ranges::all_of( filename, ::isdigit ) )
            continue;

         std::ifstream commFile( entry.path() / "comm" );
         if( ! commFile )
         {
            continue;
         }

         std::string comm;
         std::getline( commFile, comm );
         if( comm == processName )
         {
            pids.push_back( static_cast<std::uint32_t>( std::stoi( filename ) ) );
         }
      }
      return pids;
   }

   std::set<std::string> procsWithLoaded3DLibs()
   {
      namespace fs = std::filesystem;

      static constexpr std::string_view kGpuLibs[] = {
         "libGL.so", "libGLX.so", "libGLESv2.so", "libEGL.so", "libvulkan.so",
      };

      std::set<std::string> procs;
      for( const auto& procEntry : fs::directory_iterator( "/proc" ) )
      {
         const auto pid = procEntry.path().filename().string();
         if( ! std::ranges::all_of( pid, ::isdigit ) )
         {
            continue;
         }

         std::ifstream maps{ procEntry.path() / "maps" };
         if( ! maps )
         {
            continue;
         }

         std::string line;
         while( std::getline( maps, line ) )
         {
            for( const auto& lib : kGpuLibs )
            {
               if( line.contains( lib ) )
               {
                  /// TODO Should consider refactoring these calls
                  std::ifstream commFile( procEntry.path() / "comm" );
                  if( ! commFile )
                  {
                     continue;
                  }

                  std::string comm;
                  std::getline( commFile, comm );

                  // spdlog::debug( "Found game process: {} (PID {}) with GPU library {}", comm, std::stoi( pid ), lib );
                  procs.insert( comm );
               }
            }
         }
      }
      return procs;
   }

} // namespace pace::sensors
