// Explicit physical-lease fixture for lifecycle and owner-clock tests.
import "../calculator.wl";
node calculator {
  io = external;
  handlers = dynamic;
  call Add;
  serve Add;
}
