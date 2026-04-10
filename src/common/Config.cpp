#include "pace/Config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace pace
{
   namespace
   {

      std::string trimCopy( std::string_view text )
      {
         auto begin = text.begin();
         auto end   = text.end();

         while( begin != end && std::isspace( static_cast<unsigned char>( *begin ) ) )
         {
            ++begin;
         }

         while( end != begin && std::isspace( static_cast<unsigned char>( *( end - 1 ) ) ) )
         {
            --end;
         }

         return std::string( begin, end );
      }

      std::string stripBalancedQuotes( std::string value )
      {
         if( value.size() >= 2 )
         {
            const char first = value.front();
            const char last  = value.back();
            if( ( first == '"' && last == '"' ) || ( first == '\'' && last == '\'' ) )
            {
               value = value.substr( 1, value.size() - 2 );
            }
         }
         return value;
      }

      std::optional<std::pair<std::string, std::string>> parseDotEnvLine( std::string line )
      {
         line = trimCopy( line );
         if( line.empty() || line.starts_with( '#' ) )
         {
            return std::nullopt;
         }

         constexpr std::string_view exportPrefix{ "export " };
         if( line.starts_with( exportPrefix ) )
         {
            line.erase( 0, exportPrefix.size() );
            line = trimCopy( line );
         }

         const auto equalPos = line.find( '=' );
         if( equalPos == std::string::npos )
         {
            return std::nullopt;
         }

         auto key   = trimCopy( std::string_view{ line }.substr( 0, equalPos ) );
         auto value = trimCopy( std::string_view{ line }.substr( equalPos + 1 ) );
         if( key.empty() )
         {
            return std::nullopt;
         }

         value = stripBalancedQuotes( std::move( value ) );
         return std::pair{ std::move( key ), std::move( value ) };
      }

      void setEnvIfMissing( const std::string& key, const std::string& value )
      {
         if( std::getenv( key.c_str() ) != nullptr )
         {
            return;
         }

#ifdef _WIN32
         _putenv_s( key.c_str(), value.c_str() );
#else
         setenv( key.c_str(), value.c_str(), 0 );
#endif
      }

      void loadDotEnvIfPresent( const std::string& path = ".env" )
      {
         std::ifstream in{ path };
         if( ! in )
         {
            return;
         }

         std::string line;
         while( std::getline( in, line ) )
         {
            const auto parsed = parseDotEnvLine( std::move( line ) );
            if( ! parsed )
            {
               continue;
            }
            setEnvIfMissing( parsed->first, parsed->second );
         }
      }

      std::string readEnv( const char* name, const std::string& fallback = {} )
      {
         if( const char* value = std::getenv( name ); value != nullptr )
         {
            return { value };
         }
         return fallback;
      }

      bool parseBool( const std::string& value, bool fallback )
      {
         std::string normalized = value;
         std::transform( normalized.begin(), normalized.end(), normalized.begin(),
                         []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );

         if( normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on" )
         {
            return true;
         }
         if( normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off" )
         {
            return false;
         }
         return fallback;
      }

      int parseInt( const std::string& value, int fallback )
      {
         if( value.empty() )
         {
            return fallback;
         }

         try
         {
            return std::stoi( value );
         }
         catch( ... )
         {
            return fallback;
         }
      }

   } // namespace

   Config Config::fromEnvironment()
   {
      loadDotEnvIfPresent();

      Config cfg{};
      cfg.brokerUri = readEnv( "PACE_MQTT_BROKER", cfg.brokerUri );
      cfg.clientId  = readEnv( "PACE_MQTT_CLIENT_ID", cfg.clientId );
      cfg.nodeId    = readEnv( "PACE_MQTT_NODE_ID", cfg.nodeId );
      cfg.username  = readEnv( "PACE_MQTT_USERNAME", cfg.username );
      cfg.password  = readEnv( "PACE_MQTT_PASSWORD", cfg.password );
      cfg.qos       = parseInt( readEnv( "PACE_MQTT_QOS" ), cfg.qos );
      cfg.dryRun    = ! parseBool( readEnv( "PACE_ENABLE_SYSTEM_ACTIONS" ), false );
      return cfg;
   }

} // namespace pace
