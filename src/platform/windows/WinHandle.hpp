#pragma once

#include <memory>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace pace::win
{
   struct HandleDeleter
   {
         using pointer = HANDLE;
         void operator()( HANDLE h ) const
         {
            if( h && h != INVALID_HANDLE_VALUE )
               CloseHandle( h );
         }
   };
   using UniqueHandle = std::unique_ptr<void, HandleDeleter>;
} // namespace pace::win
