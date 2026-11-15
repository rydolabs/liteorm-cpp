#include "backend.hpp"
#include <stdexcept>
namespace liteorm { namespace detail {
std::unique_ptr<Backend> open_postgresql(const std::string&) {
    throw std::runtime_error("PostgreSQL support was disabled when compiling liteorm");
}
}} // namespace liteorm::detail
