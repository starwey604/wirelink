// [docs:telemetry_bind]
profile version 2;
endpoint { envelope=native_packet; }

direct Telemetry {
  delivery = unreliable;
}

send Telemetry { delivery=unreliable; }
// [/docs:telemetry_bind]
