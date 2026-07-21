#pragma once

#include "util/Error.hpp"
#include "util/Task.hpp"
#include "util/expected.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace pace::commands
{
   enum class SystemAction : std::uint8_t
   {
      Lock,
      Sleep,
      Reboot,
      Shutdown,
   };

   std::string_view actionName( SystemAction action );

   namespace impl
   {
      /// @brief Handles actions like lock, reboot and shutdown.
      /// @param action The system action to perform.
      /// @return Expected result indicating success or failure with an error message.
      util::VoidResult executeSystemAction( SystemAction action );

      /// @brief Spawns a new process with the given image path.
      /// @param imagePath The path to the executable to run.
      /// @return Expected result indicating success or failure with an error message.
      util::VoidResult spawnNewProcess( const std::string& imagePath );

      /// @brief Spawns a new process in its own process group.
      /// @param imagePath The path to the executable to run.
      /// @return Expected process group id on success or an error message.
      util::Result<std::int64_t> spawnNewProcessGroup( const std::string& imagePath );

      /// @brief Kills all processes matching the given name.
      /// @param processName The name of the process to kill.
      /// @return Expected result indicating success or failure with an error message.
      util::Task<util::VoidResult> killProcessByName( const std::string& processName );

      /// @brief Kills a process group by its pgid.
      /// @param processGroupId The process group id to kill.
      /// @return Expected result indicating success or failure with an error message.
      util::Task<util::VoidResult> killProcessGroup( std::int64_t processGroupId );

      /// @brief Shows a "toast" notification with the given message.
      /// @param message The message to display in the notification.
      /// @return Expected result indicating success or failure with an error message.
      util::VoidResult sendNotification( const std::string& message );

   } // namespace impl
} // namespace pace::commands