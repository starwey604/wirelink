profile version 2;
direct BulkCommand { delivery = unreliable; }
send BulkCommand { delivery = unreliable; }
direct BulkStatus { delivery = unreliable; }
send BulkStatus { delivery = unreliable; }
rpc Reboot { request = RebootRequest; response = RebootResponse; }
