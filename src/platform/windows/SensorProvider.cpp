#include "pace/sensors/SensorProvider.hpp"

#include "WinHandle.hpp"

#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// clang-format off
#include <psapi.h>
#include <tlhelp32.h>
// clang-format on

namespace pace::sensors::impl
{
   using pace::win::UniqueHandle;

   util::Result<double> readCpuTemperatureC()
   {
      return util::unexpected{ util::makeError( util::ErrorCode::Unsupported, "cpu temperature is not implemented on windows" ) };
   }

   util::Result<double> readMemoryUsagePercent()
   {
      return util::unexpected{ util::makeError( util::ErrorCode::Unsupported, "memory usage is not implemented on windows" ) };
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

   std::vector<std::uint32_t> findPidsByProcessGroup( const std::int64_t processGroupId )
   {
      if( processGroupId <= 0 )
      {
         return {};
      }

      const auto rootPid = static_cast<std::uint32_t>( processGroupId );

      UniqueHandle snapshot{ CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 ) };
      if( snapshot.get() == INVALID_HANDLE_VALUE )
      {
         return {};
      }

      PROCESSENTRY32 entry{};
      entry.dwSize = sizeof( entry );

      std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> childrenByParent;
      bool                                                          rootExists = false;

      if( Process32First( snapshot.get(), &entry ) )
      {
         do
         {
            const auto pid      = static_cast<std::uint32_t>( entry.th32ProcessID );
            const auto parentId = static_cast<std::uint32_t>( entry.th32ParentProcessID );

            if( pid == rootPid )
            {
               rootExists = true;
            }

            childrenByParent[ parentId ].push_back( pid );
         }
         while( Process32Next( snapshot.get(), &entry ) );
      }

      if( ! rootExists )
      {
         return {};
      }

      std::vector<std::uint32_t> groupPids;
      std::set<std::uint32_t>    visited;
      std::deque<std::uint32_t>  pending;

      pending.push_back( rootPid );
      while( ! pending.empty() )
      {
         const auto current = pending.front();
         pending.pop_front();

         if( ! visited.insert( current ).second )
         {
            continue;
         }

         groupPids.push_back( current );
         if( const auto childrenIt = childrenByParent.find( current ); childrenIt != childrenByParent.end() )
         {
            for( const auto childPid : childrenIt->second )
            {
               pending.push_back( childPid );
            }
         }
      }

      return groupPids;
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
