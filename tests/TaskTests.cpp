#include <catch2/catch_test_macros.hpp>

#include "util/Task.hpp"

#include <coroutine>
#include <fmt/base.h>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
   util::Task<int> intTask( int value )
   {
      co_return value;
   }

   util::Task<std::string> stringTask( std::string value )
   {
      co_return value;
   }

   util::Task<bool> boolTask( bool value )
   {
      co_return value;
   }

   util::Task<bool> runWhenAll( int& outInt, std::string& outString )
   {
      const auto [ value, text ] = co_await util::when_all( intTask( 7 ), stringTask( "pace" ) );
      outInt                     = value;
      outString                  = text;
      co_return true;
   }

   util::Task<bool> runAll( bool& out )
   {
      out = co_await util::all( boolTask( true ), boolTask( true ), boolTask( false ) );
      co_return true;
   }

   util::Task<bool> runAllRange( bool& out )
   {
      std::vector<util::Task<bool>> tasks;
      tasks.emplace_back( boolTask( true ) );
      tasks.emplace_back( boolTask( true ) );
      tasks.emplace_back( boolTask( false ) );

      out = co_await util::all( std::move( tasks ) );
      co_return true;
   }

} // namespace

TEST_CASE( "when_all returns task results", "[task]" )
{
   int         resultInt = 0;
   std::string resultString;

   util::sync_wait( runWhenAll( resultInt, resultString ) );

   CHECK( resultInt == 7 );
   CHECK( resultString == "pace" );
}

TEST_CASE( "all returns combined bool result", "[task]" )
{
   bool result = true;

   util::sync_wait( runAll( result ) );

   CHECK_FALSE( result );
}

TEST_CASE( "all accepts task collections", "[task]" )
{
   bool result = true;

   util::sync_wait( runAllRange( result ) );

   CHECK_FALSE( result );
}
