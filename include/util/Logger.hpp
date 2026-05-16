#pragma once

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace util
{
   using Logger = std::shared_ptr<spdlog::logger>;

   inline Logger getLogger( const std::string& name )
   {
      if( auto logger = spdlog::get( name ); logger != nullptr )
      {
         return logger;
      }
      else
      {
         auto newLogger = spdlog::stdout_color_mt( name );
         return newLogger;
      }
   }
} // namespace util