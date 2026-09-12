#pragma once
#include <krpccontract.h>
#include <krpcprotocol.h>
#include <lightoverleaf/system/inbound/ikgetcapabilities.h>

namespace lightoverleaf::rpc
{
// Ping is side-effect-free and returns its validated request as an acknowledgement.
bool acceptPing(const KValue& request);
KValue makeErrorResponse(const KValue& request, const std::string& code);

// V2 system methods are immediate, read-only and non-cancellable.
// Repeating a completed ID is allowed: there are no mutations to replay.
KValue dispatchSystem(const KValue& request, const IKGetCapabilities& capabilities);
}
