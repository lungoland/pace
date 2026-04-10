#pragma once

#include <string>

// C++23 and later provide std::expected in <expected>
#if __cplusplus >= 202302L

#include <expected>

namespace util
{

   template <typename T, typename E>
   using expected = std::expected<T, E>;

   using std::unexpected;

} // namespace util

#elif defined( __has_include )

#if __has_include( <expected>)
#include <expected>

namespace util
{

   template <typename T, typename E>
   using expected = std::expected<T, E>;

   using std::unexpected;

} // namespace util

#elif __has_include( <experimental/expected>)

#include <experimental/expected>

namespace util
{

   template <typename T, typename E>
   using expected = std::experimental::expected<T, E>;

   using std::experimental::unexpected;

} // namespace util

#else

#error "This toolchain does not provide <expected> or <experimental/expected>."

#endif

#else

#error "This toolchain does not provide <expected> or <experimental/expected>."

#endif
