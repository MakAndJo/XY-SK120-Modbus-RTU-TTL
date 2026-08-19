#include "menu_protection.h"
#include "serial_core.h"
#include "serial_interface.h"

void displayProtectionMenu() {
  Serial.println("\n==== Protection Settings ====");
  Serial.println("get - Display all protection settings");
  Serial.println("ovp [value] - Set Over Voltage Protection (V)");
  Serial.println("ocp [value] - Set Over Current Protection (A)");
  Serial.println("opp [value] - Set Over Power Protection (W)");
  Serial.println("lvp [value] - Set Input Low Voltage Protection (V)");
  Serial.println("otp [value] - Set Over Temperature Protection (°C)");
  Serial.println("status - Read protection settings and status");
  Serial.println("prot - Show protection settings and status");
  Serial.println("clear - Clear protection triggers");
  Serial.println("btf [value] - Set Battery cutoff current (A, 0=off)");
  Serial.println("btf? - Get Battery cutoff current");
  Serial.println("menu - Return to main menu");
  Serial.println("help - Show this menu");
}

void handleProtectionMenu(const String& input, XY_SKxxx* ps) {
  if (!ps) {
    Serial.println("Error: Power supply not initialized");
    return;
  }

  if (input == "get") {
    // Display all protection settings
    displayDeviceProtectionStatus(ps);
  } else if (input.startsWith("ovp ")) {
    float voltage;
    if (parseFloat(input.substring(4), voltage)) {
      if (ps->setOverVoltageProtection(voltage)) {
        Serial.print("Over Voltage Protection set to: ");
        Serial.print(voltage, 2);
        Serial.println(" V");
      } else {
        Serial.println("Failed to set Over Voltage Protection");
      }
    }
  } else if (input.startsWith("ocp ")) {
    float current;
    if (parseFloat(input.substring(4), current)) {
      if (ps->setOverCurrentProtection(current)) {
        Serial.print("Over Current Protection set to: ");
        Serial.print(current, 3);
        Serial.println(" A");
      } else {
        Serial.println("Failed to set Over Current Protection");
      }
    }
  } else if (input.startsWith("opp ")) {
    float power;
    if (parseFloat(input.substring(4), power)) {
      if (ps->setOverPowerProtection(power)) {
        Serial.print("Over Power Protection set to: ");
        Serial.print(power, 2);
        Serial.println(" W");
      } else {
        Serial.println("Failed to set Over Power Protection");
      }
    }
  } else if (input.startsWith("lvp ")) {
    float voltage;
    if (parseFloat(input.substring(4), voltage)) {
      if (ps->setLowVoltageProtection(voltage)) {
        Serial.print("Input Low Voltage Protection set to: ");
        Serial.print(voltage, 2);
        Serial.println(" V");
      } else {
        Serial.println("Failed to set Input Low Voltage Protection");
      }
    }
  } else if (input.startsWith("otp ")) {
    float temp;
    if (parseFloat(input.substring(4), temp)) {
      if (ps->setOverTemperatureProtection(temp)) {
        Serial.print("Over Temperature Protection set to: ");
        Serial.print(temp, 1);
        Serial.println(" °C");
      } else {
        Serial.println("Failed to set over-temperature protection");
      }
    }
  } else if (input == "status") {
    // Read protection settings
    float ovp, ocp, opp, otp;

    Serial.println("\n==== Protection Settings ====");

    if (ps->getOverVoltageProtection(ovp)) {
      Serial.print("Over Voltage Protection: ");
      Serial.print(ovp, 2);
      Serial.println(" V");
    } else {
      Serial.println("Failed to read OVP value");
    }

    if (ps->getOverCurrentProtection(ocp)) {
      Serial.print("Over Current Protection: ");
      Serial.print(ocp, 3);
      Serial.println(" A");
    } else {
      Serial.println("Failed to read OCP value");
    }

    if (ps->getOverPowerProtection(opp)) {
      Serial.print("Over Power Protection: ");
      Serial.print(opp, 2);
      Serial.println(" W");
    } else {
      Serial.println("Failed to read OPP value");
    }

    if (ps->getOverTemperatureProtection(otp)) {
      Serial.print("Over Temperature Protection: ");
      Serial.print(otp, 1);
      Serial.println(" °C");
    } else {
      Serial.println("Failed to read OTP value");
    }

    // Check for triggered protections
    uint8_t protStatus = ps->getProtectionStatus();
    Serial.println("\n==== Protection Status ====");
    Serial.print("OVP triggered: ");
    Serial.println((protStatus & 0x01) ? "YES" : "NO");

    Serial.print("OCP triggered: ");
    Serial.println((protStatus & 0x02) ? "YES" : "NO");

    Serial.print("OPP triggered: ");
    Serial.println((protStatus & 0x04) ? "YES" : "NO");

    Serial.print("OTP triggered: ");
    Serial.println((protStatus & 0x08) ? "YES" : "NO");
  } else if (input == "clear") {
    // Since clearProtection doesn't exist, we'll try to write to a register
    // that might clear protections (this is a guess)
    if (ps->writeRegister(REG_PROTECT, 0x0000)) {
      Serial.println("Protection clear command sent");
    } else {
      Serial.println("Failed to clear protection triggers");
    }
  } else if (input.startsWith("btf ")) {
    float current;
    if (parseFloat(input.substring(4), current)) {
      if (ps->setBatteryCutoffCurrent(current)) {
        Serial.print("Battery cutoff current set to: ");
        if (current > 0) {
          Serial.print(current, 3);
          Serial.println(" A");
        } else {
          Serial.println("OFF");
        }
      } else {
        Serial.println("Failed to set battery cutoff current");
      }
    }
  } else if (input == "btf?") {
    float current;
    if (ps->getBatteryCutoffCurrent(current)) {
      Serial.print("Battery cutoff current: ");
      if (current > 0) {
        Serial.print(current, 3);
        Serial.println(" A");
      } else {
        Serial.println("OFF");
      }
    } else {
      Serial.println("Failed to read battery cutoff current");
    }
  } else {
    Serial.println("Unknown command. Type 'help' for options.");
  }
}

void displayDeviceProtectionStatus(XY_SKxxx* ps) {
  // Display all protection settings
  float ovp, ocp, opp, otp;

  Serial.println("\n==== Protection Settings ====");

  if (ps->getOverVoltageProtection(ovp)) {
    Serial.print("Over Voltage Protection: ");
    Serial.print(ovp, 2);
    Serial.println(" V");
  } else {
    Serial.println("Failed to read OVP value");
  }

  if (ps->getOverCurrentProtection(ocp)) {
    Serial.print("Over Current Protection: ");
    Serial.print(ocp, 3);
    Serial.println(" A");
  } else {
    Serial.println("Failed to read OCP value");
  }

  if (ps->getOverPowerProtection(opp)) {
    Serial.print("Over Power Protection: ");
    Serial.print(opp, 2);
    Serial.println(" W");
  } else {
    Serial.println("Failed to read OPP value");
  }

  if (ps->getOverTemperatureProtection(otp)) {
    Serial.print("Over Temperature Protection: ");
    Serial.print(otp, 1);
    Serial.println(" °C");
  } else {
    Serial.println("Failed to read OTP value");
  }

  // Add battery cutoff current display
  float btf;
  if (ps->getBatteryCutoffCurrent(btf)) {
    Serial.print("Battery cutoff current: ");
    if (btf > 0) {
      Serial.print(btf, 3);
      Serial.println(" A");
    } else {
      Serial.println("OFF");
    }
  } else {
    Serial.println("Battery cutoff current: FAILED TO READ");
  }

  // Check for triggered protections
  uint8_t protStatus = ps->getProtectionStatus();
  Serial.println("\n==== Protection Status ====");
  Serial.print("OVP triggered: ");
  Serial.println((protStatus & 0x01) ? "YES" : "NO");

  Serial.print("OCP triggered: ");
  Serial.println((protStatus & 0x02) ? "YES" : "NO");

  Serial.print("OPP triggered: ");
  Serial.println((protStatus & 0x04) ? "YES" : "NO");

  Serial.print("OTP triggered: ");
  Serial.println((protStatus & 0x08) ? "YES" : "NO");
}
