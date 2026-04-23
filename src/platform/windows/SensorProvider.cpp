#include "pace/sensors/SensorProvider.hpp"

#include "WinHandle.hpp"

#include <cstdint>
#include <string>
#include <vector>

// clang-format off
#include <psapi.h>
#include <tlhelp32.h>
// clang-format on

#include <spdlog/spdlog.h>

namespace pace::sensors::impl
{
   using pace::win::UniqueHandle;

   util::expected<double, std::string> readCpuTemperatureC()
   {
      return util::unexpected{ "cpu temperature is not implemented on windows" };
   }

   util::expected<double, std::string> readMemoryUsagePercent()
   {
      return util::unexpected{ "memory usage is not implemented on windows" };
   }

   std::vector<std::uint32_t> findPidsByName( const std::string& processName )
   {
      std::vector<std::uint32_t> pids;

      UniqueHandle snapshot{ CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 ) };
      if( snapshot.get() == INVALID_HANDLE_VALUE )
         return pids;

      PROCESSENTRY32 entry{};
      entry.dwSize = sizeof( entry );

      if( Process32First( snapshot.get(), &entry ) )
      {
         do
         {
            // TODO: Find a better way to implicitly include .exe
            if( processName == entry.szExeFile || ( processName + ".exe" ) == entry.szExeFile )
            {
               pids.push_back( static_cast<std::uint32_t>( entry.th32ProcessID ) );
            }
         }
         while( Process32Next( snapshot.get(), &entry ) );
      }

      return pids;
   }

   std::set<std::string> procsWithLoaded3DLibs()
   {
      static constexpr const char* kGpuDlls[] = {
         "d3d9.dll", "d3d11.dll", "d3d12.dll", "opengl32.dll", "vulkan-1.dll",
      };

      UniqueHandle snapshot{ CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 ) };
      if( snapshot.get() == INVALID_HANDLE_VALUE )
      {
         return {};
      }

      PROCESSENTRY32 entry{};
      entry.dwSize = sizeof( entry );

      std::set<std::string> foundProcs;
      if( Process32First( snapshot.get(), &entry ) )
      {
         do
         {
            UniqueHandle process{ OpenProcess( PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID ) };
            if( ! process )
            {
               continue;
            }

            HMODULE modules[ 1024 ];
            DWORD   needed = 0;
            if( EnumProcessModules( process.get(), modules, sizeof( modules ), &needed ) )
            {
               const DWORD count = needed / sizeof( HMODULE );
               for( DWORD i = 0; i < count; ++i )
               {
                  char modName[ MAX_PATH ]{};
                  if( GetModuleBaseNameA( process.get(), modules[ i ], modName, MAX_PATH ) )
                  {
                     for( const auto* dll : kGpuDlls )
                     {
                        if( _stricmp( modName, dll ) == 0 )
                        {
                           // spdlog::debug( "Found game process: {} (PID {}) with GPU module {}", entry.szExeFile, entry.th32ProcessID, dll );
                           foundProcs.insert( entry.szExeFile );
                           break;
                        }
                     }
                  }
               }
            }
         }
         while( Process32Next( snapshot.get(), &entry ) );
      }

      return foundProcs;
   }

} // namespace pace::sensors::impl
