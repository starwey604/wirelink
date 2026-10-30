// SPDX-License-Identifier: Apache-2.0
profile version 2;
endpoint { envelope = native_packet; rpc_role = both; }
rpc Execute { request = Request; response = Response; }
send Event { delivery = unreliable; }
direct Event { delivery = unreliable; }
