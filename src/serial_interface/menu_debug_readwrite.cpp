#include "menu_debug.h"
#include "serial_core.h"

void handleDebugReadWrite(const String& input, XY_SKxxx* ps) {
  // Basic read commands
  if (input.startsWith("read ")) {
    handleDebugRead(input, ps);
  } 
  else if (input.startsWith("readhex ")) {
    handleDebugRead(input, ps);
  } 
  // Basic write commands
  else if (input.startsWith("write ") || input.startsWith("writehex ")) {
    handleDebugWrite(input, ps);
  }
  // Multi-write commands
  else if (input.startsWith("mwrite ") || input.startsWith("mwritehex ")) {
    handleDebugMultiWrite(input, ps);
  }
  // Block write command (single FC16/0x10 request)
  else if (input.startsWith("wblockhex ") || input.startsWith("wblock ")) {
    handleDebugBlockWrite(input, ps);
  }
  // Raw read command
  else if (input.startsWith("raw ")) {
    handleDebugRaw(input, ps);
  }
}

bool handleDebugRead(const String& input, XY_SKxxx* ps) {
  bool isHex = input.startsWith("readhex ");
  int cmdLen = isHex ? 8 : 5;
  uint16_t reg;
  
  if (isHex) {
    if (!parseHex(input.substring(cmdLen), reg)) {
      Serial.println("Invalid hex register address");
      return false;
    }
  } else {
    if (!parseUInt16(input.substring(cmdLen), reg)) {
      Serial.println("Invalid register address");
      return false;
    }
  }
  
  uint16_t value;
  if (ps->readRegisters(reg, 1, &value)) {
    if (isHex) {
      Serial.print("Register 0x");
      Serial.print(reg, HEX);
      Serial.print(" (");
      Serial.print(reg);
      Serial.print("): 0x");
      Serial.print(value, HEX);
    } else {
      Serial.print("Register ");
      Serial.print(reg);
      Serial.print(" (0x");
      Serial.print(reg, HEX);
      Serial.print("): ");
      Serial.print(value);
      Serial.print(" (0x");
      Serial.print(value, HEX);
    }
    Serial.print(" (");
    Serial.print(value);
    Serial.println(")");
    return true;
  } else {
    Serial.println("Failed to read register");
    return false;
  }
}

bool handleDebugReadInput(const String& input, XY_SKxxx* ps) {
  bool isHex = input.startsWith("read4hex ");
  int cmdLen = isHex ? 9 : 6;
  uint16_t reg;
  
  if (isHex) {
    if (!parseHex(input.substring(cmdLen), reg)) {
      Serial.println("Invalid hex register address");
      return false;
    }
  } else {
    if (!parseUInt16(input.substring(cmdLen), reg)) {
      Serial.println("Invalid register address");
      return false;
    }
  }
  
  uint16_t value;
  if (ps->readInputRegisters(reg, 1, &value)) {
    if (isHex) {
      Serial.print("Input register 0x");
      Serial.print(reg, HEX);
      Serial.print(" (");
      Serial.print(reg);
      Serial.print("): 0x");
      Serial.print(value, HEX);
    } else {
      Serial.print("Input register ");
      Serial.print(reg);
      Serial.print(" (0x");
      Serial.print(reg, HEX);
      Serial.print("): ");
      Serial.print(value);
      Serial.print(" (0x");
      Serial.print(value, HEX);
    }
    Serial.print(" (");
    Serial.print(value);
    Serial.println(")");
    return true;
  } else {
    Serial.println("Failed to read input register");
    return false;
  }
}

bool handleDebugWrite(const String& input, XY_SKxxx* ps) {
  bool isHex = input.startsWith("writehex ");
  int cmdLen = isHex ? 9 : 6;
  int spacePos = input.indexOf(' ', cmdLen);
  
  if (spacePos <= 0) {
    Serial.println("Invalid format. Use: " + String(isHex ? "writehex" : "write") + " [register] [value]");
    return false;
  }
  
  uint16_t reg, value;
  if (isHex) {
    if (!parseHex(input.substring(cmdLen, spacePos), reg) ||
        !parseHex(input.substring(spacePos + 1), value)) {
      Serial.println("Invalid hex values");
      return false;
    }
  } else {
    if (!parseUInt16(input.substring(cmdLen, spacePos), reg) ||
        !parseUInt16(input.substring(spacePos + 1), value)) {
      Serial.println("Invalid values");
      return false;
    }
  }
  
  if (ps->writeRegister(reg, value)) {
    if (isHex) {
      Serial.print("Register 0x");
      Serial.print(reg, HEX);
      Serial.print(" written with value: 0x");
      Serial.println(value, HEX);
    } else {
      Serial.print("Register ");
      Serial.print(reg);
      Serial.print(" written with value: ");
      Serial.println(value);
    }
    return true;
  } else {
    Serial.println("Failed to write register");
    return false;
  }
}

bool handleDebugMultiWrite(const String& input, XY_SKxxx* ps) {
  bool isHex = input.startsWith("mwritehex ");
  String args = input.substring(isHex ? 10 : 7);
  args.trim();
  
  int maxPairs = 10; // Maximum number of register-value pairs to process
  uint16_t registers[maxPairs];
  uint16_t values[maxPairs];
  int pairCount = 0;
  
  int index = 0;
  while (args.length() > 0 && pairCount < maxPairs) {
    int spacePos = args.indexOf(' ');
    String token;
    
    if (spacePos > 0) {
      token = args.substring(0, spacePos);
      args = args.substring(spacePos + 1);
    } else {
      token = args;
      args = "";
    }
    
    token.trim();
    if (token.length() == 0) continue;
    
    if (index % 2 == 0) {
      // Register address
      if (isHex) {
        if (!parseHex(token, registers[pairCount])) {
          Serial.println("Invalid hex register address: " + token);
          return false;
        }
      } else {
        if (!parseUInt16(token, registers[pairCount])) {
          Serial.println("Invalid register address: " + token);
          return false;
        }
      }
    } else {
      // Register value
      if (isHex) {
        if (!parseHex(token, values[pairCount])) {
          Serial.println("Invalid hex register value: " + token);
          return false;
        }
      } else {
        if (!parseUInt16(token, values[pairCount])) {
          Serial.println("Invalid register value: " + token);
          return false;
        }
      }
      pairCount++;
    }
    
    index++;
  }
  
  if (index % 2 != 0 || pairCount == 0) {
    Serial.println("Invalid format. Need register-value pairs.");
    return false;
  }
  
  Serial.print("Writing to ");
  Serial.print(pairCount);
  Serial.println(isHex ? " registers (hex):" : " registers:");
  
  // Write each register-value pair
  bool allSuccess = true;
  for (int i = 0; i < pairCount; i++) {
    if (ps->writeRegister(registers[i], values[i])) {
      if (isHex) {
        Serial.print("Register 0x");
        Serial.print(registers[i], HEX);
        Serial.print(" = 0x");
        Serial.println(values[i], HEX);
      } else {
        Serial.print("Register ");
        Serial.print(registers[i]);
        Serial.print(" (0x");
        Serial.print(registers[i], HEX);
        Serial.print(") = ");
        Serial.print(values[i]);
        Serial.print(" (0x");
        Serial.print(values[i], HEX);
        Serial.println(")");
      }
    } else {
      if (isHex) {
        Serial.print("Failed to write register 0x");
        Serial.println(registers[i], HEX);
      } else {
        Serial.print("Failed to write register ");
        Serial.println(registers[i]);
      }
      allSuccess = false;
    }
    
    // Small delay between writes
    delay(50);
  }
  
  if (allSuccess) {
    Serial.println("All registers written successfully.");
  } else {
    Serial.println("Some registers failed to write.");
  }
  
  return allSuccess;
}

bool handleDebugBlockWrite(const String& input, XY_SKxxx* ps) {
  bool isHex = input.startsWith("wblockhex ");
  String args = input.substring(isHex ? 10 : 7);
  args.trim();
  
  int space1 = args.indexOf(' ');
  int space2 = args.indexOf(' ', space1 + 1);
  if (space1 <= 0 || space2 <= 0) {
    Serial.println("Invalid format. Use: wblockhex [start] [count] [v1 v2 ...]");
    return false;
  }
  
  uint16_t startAddr, count;
  if (isHex) {
    if (!parseHex(args.substring(0, space1), startAddr) ||
        !parseHex(args.substring(space1 + 1, space2), count)) {
      Serial.println("Invalid hex start address or count");
      return false;
    }
  } else {
    if (!parseUInt16(args.substring(0, space1), startAddr) ||
        !parseUInt16(args.substring(space1 + 1, space2), count)) {
      Serial.println("Invalid start address or count");
      return false;
    }
  }
  
  if (count < 1 || count > 50) {
    Serial.println("Count must be between 1 and 50");
    return false;
  }
  
  String vals = args.substring(space2 + 1);
  vals.trim();
  
  uint16_t values[50];
  int n = 0;
  while (vals.length() > 0 && n < count) {
    int sp = vals.indexOf(' ');
    String token = sp > 0 ? vals.substring(0, sp) : vals;
    if (sp > 0) vals = vals.substring(sp + 1); else vals = "";
    token.trim();
    if (token.length() == 0) continue;
    if (isHex) {
      if (!parseHex(token, values[n])) {
        Serial.println("Invalid hex value: " + token);
        return false;
      }
    } else {
      if (!parseUInt16(token, values[n])) {
        Serial.println("Invalid value: " + token);
        return false;
      }
    }
    n++;
  }
  
  if (n != count) {
    Serial.print("Expected ");
    Serial.print(count);
    Serial.print(" values, got ");
    Serial.println(n);
    return false;
  }
  
  if (ps->writeRegisters(startAddr, count, values)) {
    Serial.print("Block write OK: 0x");
    Serial.print(startAddr, HEX);
    Serial.print(" count ");
    Serial.print(count);
    Serial.println(" (single FC16/0x10 request)");
    return true;
  } else {
    Serial.println("Block write FAILED");
    return false;
  }
}

bool handleDebugRaw(const String& input, XY_SKxxx* ps) {
  int space1 = input.indexOf(' ', 4);
  int space2 = input.indexOf(' ', space1 + 1);
  
  if (space1 <= 0 || space2 <= 0) {
    Serial.println("Invalid format. Use: raw [function] [register] [count]");
    return false;
  }
  
  uint8_t function;
  uint16_t reg, count;
  
  if (!parseUInt8(input.substring(4, space1), function) ||
      !parseUInt16(input.substring(space1 + 1, space2), reg) ||
      !parseUInt16(input.substring(space2 + 1), count)) {
    Serial.println("Invalid format. Use: raw [function] [register] [count]");
    return false;
  }
  
  // Limit count to prevent buffer overflow
  if (count > 20) {
    count = 20;
    Serial.println("Limited count to 20 registers");
  }
  
  uint16_t* results = new uint16_t[count];
  
  bool ok = false;
  if (function == 0x04) {
    ok = ps->readInputRegisters(reg, count, results);
    Serial.println("Using function code 0x04 (input registers)");
  } else if (function == 0x03) {
    ok = ps->readRegisters(reg, count, results);
    Serial.println("Using function code 0x03 (holding registers)");
  } else {
    Serial.println("Unsupported function code. Only 0x03 and 0x04 are supported.");
    delete[] results;
    return false;
  }
  
  if (ok) {
    Serial.print("Read registers starting at: ");
    Serial.print(reg);
    Serial.print(", count: ");
    Serial.println(count);
    
    for (uint16_t i = 0; i < count; i++) {
      Serial.print(reg + i);
      Serial.print(" (0x");
      Serial.print(reg + i, HEX);
      Serial.print("): ");
      Serial.print(results[i]);
      Serial.print(" (0x");
      Serial.print(results[i], HEX);
      Serial.println(")");
    }
    
    delete[] results;
    return true;
  } else {
    Serial.println("Failed to read registers");
    delete[] results;
    return false;
  }
}
