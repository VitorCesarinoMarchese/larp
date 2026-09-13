#pragma once
#include <cstdint>
#include <memory>
#include <string>
namespace larp {
class PortalSession {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    PortalSession();
    ~PortalSession();
    int open_remote();
    std::uint32_t node() const;
    std::string serial() const;
    bool closed();
};
} // namespace larp
