profile version 1;
direct BulkCommand { delivery = reliable; }
send BulkCommand { delivery = reliable; }
direct BulkStatus { delivery = unreliable; }
send BulkStatus { delivery = unreliable; }
rpc Reboot { request = RebootRequest; response = RebootResponse; }
