#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace util
{
   using Logger = std::shared_ptr<spdlog::logger>;

   inline const spdlog::sink_ptr fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>( "pace.log", true );

   inline Logger getLogger( const std::string& name )
   {
      if( auto logger = spdlog::get( name ); logger != nullptr )
      {
         return logger;
      }

      auto newLogger = spdlog::stdout_color_mt( name );
      newLogger->sinks().push_back( fileSink );
      return newLogger;
   }
} // namespace util
