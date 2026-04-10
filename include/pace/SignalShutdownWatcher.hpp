#pragma once

#include <functional>
#include <thread>

namespace pace
{
   class SignalShutdownWatcher
   {
      public:

         using SignalHandler = std::function<void( int )>;

         explicit SignalShutdownWatcher( SignalHandler onSignal );
         ~SignalShutdownWatcher();

         SignalShutdownWatcher( const SignalShutdownWatcher& )            = delete;
         SignalShutdownWatcher& operator=( const SignalShutdownWatcher& ) = delete;
         SignalShutdownWatcher( SignalShutdownWatcher&& )                 = delete;
         SignalShutdownWatcher& operator=( SignalShutdownWatcher&& )      = delete;

      private:

         SignalHandler onSignalCallback;
         std::jthread  watcherThread{};
   };

} // namespace pace