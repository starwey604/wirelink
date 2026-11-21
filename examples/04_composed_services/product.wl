// [docs:composed_product_schema]
version 1;
import "arm.wl";
import "upgrade.wl";

node product {
  send Telemetry; recv Telemetry;
  send BulkCommand; recv BulkCommand;
  send BulkStatus; recv BulkStatus;
  call Query; serve Query;
  call Reboot; serve Reboot;
}
// [/docs:composed_product_schema]
