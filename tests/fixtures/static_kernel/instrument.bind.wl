// SPDX-License-Identifier: Apache-2.0
profile version 2;
endpoint { envelope = native_packet; rpc_role = both; }
direct Reading { delivery = unreliable; }
send Reading { delivery = unreliable; }
