#pragma once
#include <string>
#include <variant>

namespace lightoverleaf
{
enum class KErrorCode
{
    InvalidArgument, NotFound, Conflict, Cancelled, Unavailable, ResourceExhausted, InvalidEncoding, Internal
};
struct KError
{
    KErrorCode m_code = KErrorCode::Internal;
    std::string m_messageKey;
    bool m_retryable = false;
};
template<class T> using KResult = std::variant<T, KError>;
}
