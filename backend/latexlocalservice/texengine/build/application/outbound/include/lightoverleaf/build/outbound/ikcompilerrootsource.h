#pragma once

#include <string>
#include <vector>

namespace lightoverleaf::build
{
class IKCompilerRootSource
{
public:
    virtual ~IKCompilerRootSource() = default;
    virtual std::vector<std::string> roots() const = 0;
};
}
