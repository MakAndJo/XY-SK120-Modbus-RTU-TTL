#include "menu_debug.h"
#include "serial_core.h"
#include "serial_interface.h"

void displayDebugMenu() {
  Serial.println("\n==== Debug Menu (Register R/W) ====");
  Serial.println("read [register] - Read register (decimal)");
  Serial.println("readhex [register] - Read register (hex)");
  Serial.println("write [register] [value] - Write register (decimal)");
  Serial.println("writehex [register] [value] - Write register (hex)");
  Serial.println("writerange [start] [end] [value] [delay_ms] - Write value to range of registers");
  Serial.println("mwrite [reg1] [val1] [reg2] [val2] ... - Write multiple registers (decimal)");
  Serial.println("mwritehex [reg1] [val1] [reg2] [val2] ... - Write multiple registers (hex)");
  Serial.println("wblock [start] [count] [v1 v2 ...] - Write contiguous block via FC16 (decimal)");
  Serial.println("wblockhex [start] [count] [v1 v2 ...] - Write contiguous block via FC16 (hex)");
  Serial.println("writetrial [register] [start] [end] [delay_ms] - Try writing range of values to register");
  Serial.println("raw [function] [register] [count] - Read raw register block");
  Serial.println("scan [start] [end] - Scan register range");
  Serial.println("sniff [seconds] - Listen on Modbus bus for PSU-initiated frames");
  Serial.println("read4 [register] - Read input register via FC04 (decimal)");
  Serial.println("read4hex [register] - Read input register via FC04 (hex)");
  Serial.println("scan4 [start] [end] - Scan input register range via FC04");
  Serial.println("compare [start] [end] - Scan and compare register values before/after changing settings");
  Serial.println("menu - Return to main menu");
  Serial.println("help - Show this menu");
}

void handleDebugMenu(const String& input, XY_SKxxx* ps) {
  if (!ps) {
    Serial.println("Error: Power supply not initialized");
    return;
  }
  
  // Handle basic read/write commands
  if (input.startsWith("read4")) {
    handleDebugReadInput(input, ps);
    return;
  }
  
  if (input.startsWith("read") || input.startsWith("write") || 
      input.startsWith("mwrite") || input.startsWith("raw")) {
    handleDebugReadWrite(input, ps);
    return;
  }
  
  if (input.startsWith("wblock")) {
    handleDebugReadWrite(input, ps);
    return;
  }
  
  // Handle scan and compare commands
  if (input.startsWith("scan ")) {
    handleDebugScan(input, ps);
    return;
  }
  
  if (input.startsWith("sniff")) {
    handleDebugSniff(input, ps);
    return;
  }
  
  if (input.startsWith("scan4 ")) {
    handleDebugScanInput(input, ps);
    return;
  }
  
  if (input.startsWith("compare ")) {
    handleDebugCompare(input, ps);
    return;
  }
  
  // Handle write trial command
  if (input.startsWith("writetrial ")) {
    handleDebugWriteTrial(input, ps);
    return;
  }
  
  // Handle write range command
  if (input.startsWith("writerange ")) {
    handleDebugWriteRange(input, ps);
    return;
  }
  
  // Handle help or unknown command
  if (input == "help") {
    displayDebugMenu();
  } else {
    Serial.println("Unknown command. Type 'help' for options.");
  }
}
