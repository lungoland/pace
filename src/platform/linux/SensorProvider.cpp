#include "pace/sensors/SensorProvider.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

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

      std::optional<std::int64_t> readProcessGroupId( const std::filesystem::path& statPath )
      {
         std::ifstream statFile{ statPath };
         if( ! statFile )
         {
            return std::nullopt;
         }

         std::string statLine;
         std::getline( statFile, statLine );
         if( statLine.empty() )
         {
            return std::nullopt;
         }

         const auto rightParen = statLine.rfind( ") " );
         if( rightParen == std::string::npos )
         {
            return std::nullopt;
         }

         // /proc/<pid>/stat fields after ") " start with: state, ppid, pgrp, ...
         std::istringstream tail{ statLine.substr( rightParen + 2 ) };
         char               state = '\0';
         std::int64_t       ppid  = 0;
         std::int64_t       pgrp  = 0;
         if( ! ( tail >> state >> ppid >> pgrp ) )
         {
            return std::nullopt;
         }

         return pgrp;
      }
   }

   util::Result<double> readCpuTemperatureC()
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

      return util::unexpected{ util::makeError( util::ErrorCode::NotFound, "cpu temperature source not found" ) };
   }

   util::Result<double> readMemoryUsagePercent()
   {
      std::ifstream meminfo{ "/proc/meminfo" };
      if( ! meminfo )
      {
         return util::unexpected{ util::makeError( util::ErrorCode::PlatformFailure, "failed to open /proc/meminfo" ) };
      }

      long long   totalKb     = 0;
      long long   availableKb = 0;
      std::string line;
      while( std::getline( meminfo, line ) )
      {
         if( line.starts_with( "MemTotal:" ) )
         {
            totalKb = parseMeminfoValue( line );
         }
         if( line.starts_with( "MemAvailable:" ) )
         {
            availableKb = parseMeminfoValue( line );
         }
      }

      if( totalKb <= 0 || availableKb < 0 )
      {
         return util::unexpected{ util::makeError( util::ErrorCode::ParseFailure, "failed to parse memory usage from /proc/meminfo" ) };
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

   std::vector<std::uint32_t> findPidsByProcessGroup( const std::int64_t processGroupId )
   {
      namespace fs = std::filesystem;

      if( processGroupId <= 0 )
      {
         return {};
      }

      std::vector<std::uint32_t> pids;
      for( const auto& entry : fs::directory_iterator( "/proc" ) )
      {
         const auto filename = entry.path().filename().string();
         if( ! std::ranges::all_of( filename, ::isdigit ) )
         {
            continue;
         }

         const auto pgrp = readProcessGroupId( entry.path() / "stat" );
         if( pgrp && *pgrp == processGroupId )
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
                  procs.insert( comm );
               }
            }
         }
      }
      return procs;
   }

} // namespace pace::sensors
