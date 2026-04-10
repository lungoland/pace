#include <catch2/catch_test_macros.hpp>
#include <stdlib.h>

#include "pace/Config.hpp"

namespace
{

   void setEnv( const char* key, const char* value )
   {
#ifdef _WIN32
      _putenv_s( key, value );
#else
      setenv( key, value, 1 );
#endif
   }

   void clearEnv( const char* key )
   {
#ifdef _WIN32
      _putenv_s( key, "" );
#else
      unsetenv( key );
#endif
   }

} // namespace

TEST_CASE( "config keeps dry-run by default", "[config]" )
{
   clearEnv( "PACE_ENABLE_SYSTEM_ACTIONS" );

   const auto config = pace::Config::fromEnvironment();

   CHECK( config.dryRun );
}

TEST_CASE( "config toggles dry-run off when enabled", "[config]" )
{
   setEnv( "PACE_ENABLE_SYSTEM_ACTIONS", "true" );

   const auto config = pace::Config::fromEnvironment();

   CHECK_FALSE( config.dryRun );

   clearEnv( "PACE_ENABLE_SYSTEM_ACTIONS" );
}
