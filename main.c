#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// uses 16-bit instruction memory and 8-bit data/registers
#define INSTRUCTION_MEMORY_SIZE 1024
#define DATA_MEMORY_SIZE 2048
#define REGISTER_COUNT 64 //without PC and SREG


// SREG bits: 7 6 5 4 3 2 1 0
//            0 0 0 C V N S Z
#define Z_FLAG 0
#define S_FLAG 1
#define N_FLAG 2
#define V_FLAG 3
#define C_FLAG 4

short int instructionMemory[INSTRUCTION_MEMORY_SIZE];
int8_t dataMemory[DATA_MEMORY_SIZE];
int8_t registers[REGISTER_COUNT]; //GPRs (R0-R63)
int8_t SREG;
short int PC =0 ; 
int instructionCount = 0;
// Pipeline register between fetch and decode
typedef struct {
    short int instruction;
    short int pc;
    int valid;
} IF_ID;
// Pipeline register between decode and execute
typedef struct {
    short int pc;
    int opcode;
    int r1;
    int r2OrImm;
    int valid;
} ID_EX;

IF_ID if_id = {0, 0, 0};
ID_EX id_ex = { 0, 0, 0, 0, 0};
// Convert instruction name to the opcode used in our ISA
int getOpcode(char mnemonic[]) {
    if (strcmp(mnemonic, "ADD") == 0) return 0;
    if (strcmp(mnemonic, "SUB") == 0) return 1;
    if (strcmp(mnemonic, "MUL") == 0) return 2;
    if (strcmp(mnemonic, "MOVI") == 0) return 3;
    if (strcmp(mnemonic, "BEQZ") == 0) return 4;
    if (strcmp(mnemonic, "ANDI") == 0) return 5;
    if (strcmp(mnemonic, "EOR") == 0) return 6;
    if (strcmp(mnemonic, "BR") == 0) return 7;
    if (strcmp(mnemonic, "SLC") == 0) return 8;
    if (strcmp(mnemonic, "SRC") == 0) return 9;
    if (strcmp(mnemonic, "LDR") == 0) return 10;
    if (strcmp(mnemonic, "STR") == 0) return 11;

    return -1;
}
// Register format should be R0 to R63
int getRegisterNumber(char reg[]) {
    if (reg[0] != 'R') {
        return -1;
    }
    int regNumber = atoi(reg + 1);
    if (regNumber < 0 || regNumber > 63) {
        return -1;
    }
    return regNumber;
}
// Negative immediates are allowed; they are handled later using 6-bit encoding
int getImmediateValue(char imm[]) {
    int value = atoi(imm);
    return value;
}

void printBinary16(short int value) {
    unsigned short instruction = (unsigned short) value;
    for (int i = 15; i >= 0; i--) {
        printf("%d", (instruction >> i) & 1);
        if (i == 12 || i == 6) {
            printf(" ");
        }
    }
}
// Build the 16-bit instruction: opcode | R1 | R2/Immediate
short int  encodeInstruction(int opcode, int operand1, int operand2) {
    short int instruction = 0;
    instruction = instruction | ((opcode & 0xF) << 12); // opcode: upper 4 bits
    instruction = instruction | ((operand1 & 0x3F) << 6); // R1: middle 6 bits
    instruction = instruction | (operand2 & 0x3F); // R2/imm: lower 6 bits
    return instruction;
}
// Read the assembly file line by line and store encoded instructions in memory
void loadProgram(char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        printf("Error: could not open %s\n", filename);
        return;
    }
    char line[100];
    int instructionIndex = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        // Split the line into instruction name and two operands
        char *instructionName = strtok(line, " ,\t\n");
        if (instructionName == NULL) {
            continue;
        }
        char *operand1String = strtok(NULL, " ,\t\n");
        char *operand2String = strtok(NULL, " ,\t\n");
        if (operand1String == NULL || operand2String == NULL) {
            printf("Error: missing operand in instruction %s\n", instructionName);
            continue;
        }
        int opcode = getOpcode(instructionName);
        if (opcode == -1) {
            printf("Error: unknown instruction %s\n", instructionName);
            continue;
        }
        int operand1 = getRegisterNumber(operand1String);
        int operand2;
        if (operand1 == -1) {
            printf("Error: invalid first operand %s in instruction %s\n",
                   operand1String,
                   instructionName);
            continue;
        }
        // These instructions use a register as the second operand
        if (opcode == 0 || opcode == 1 || opcode == 2 || opcode == 6 || opcode == 7) {
            operand2 = getRegisterNumber(operand2String);
            if (operand2 == -1) {
                printf("Error: invalid second register %s in instruction %s\n",
                       operand2String,
                       instructionName);
                continue;
            }
            // The rest use an immediate, address, offset, or shift amount
        } else {
            operand2 = getImmediateValue(operand2String);

            if ((opcode == 8 || opcode == 9) && operand2 < 0) {
                printf("Error: shift amount cannot be negative in instruction %s\n",
                       instructionName);
                continue;
            }
            if (opcode != 8 && opcode != 9 && opcode != 10 && opcode != 11) {
                if (operand2 < -32 || operand2 > 31) {
                    printf("Warning: immediate %d is outside signed 6-bit range (-32 to 31)\n",
                           operand2);
                }
            }
            if ((opcode == 8 || opcode == 9 || opcode == 10 || opcode == 11) &&
                (operand2 < 0 || operand2 > 63)) {
                printf("Warning: operand %d may not fit in 6 bits (0 to 63)\n",
                       operand2);
            }
        }
        // Store the encoded instruction only; decoding will happen later in the pipeline
        instructionMemory[instructionIndex] =
            encodeInstruction(opcode, operand1, operand2);
        printf("Loaded instruction %d: %s encoded as %d\n",
               instructionIndex,
               instructionName,
               instructionMemory[instructionIndex]);
        instructionIndex++;
    }
    fclose(file);
    instructionCount = instructionIndex;
    printf("Program loaded successfully. Instructions count = %d\n",
           instructionCount);
}
// Convert 6-bit signed immediate to a normal signed int
int signExtend6(int value) {
    // If bit 5 is 1, the immediate is negative
    if (value & 0x20) {
        return value | 0xFFFFFFC0;
    }
    return value;
}
// Fetch the next instruction and place it in IF/ID
void fetchInstruction() {
    printf("FETCH INPUT: PC before fetch = %d\n", PC);
    short int instruction = instructionMemory[PC];
    if_id.instruction=instruction;
    printf("\nFetched instruction from PC = %d : ",
           PC);
    // Save PC before incrementing because branches need the instruction's original address
    if_id.pc=PC;
    PC++;
    if_id.valid=1;
    printf("\n");
    printf("FETCH OUTPUT: IF/ID.instruction = ");
    printBinary16(instruction);
    printf(", IF/ID.pc = %d, PC after increment = %d\n", if_id.pc, PC);
    printf("\n");

}
// Decode the instruction from IF/ID and move its fields to ID/EX
void decodeInstruction() {
     if (if_id.valid) {
        printf("DECODE INPUT: IF/ID.instruction = ");
        printBinary16(if_id.instruction);
        printf(", IF/ID.pc = %d\n", if_id.pc);
        unsigned short unsignedInstruction = (unsigned short) if_id.instruction;
         // Extract opcode, R1, and the last 6-bit field from the encoded instruction
        id_ex.opcode = (unsignedInstruction >> 12) & 0xF;
        id_ex.r1 = (unsignedInstruction >> 6) & 0x3F;
        id_ex.r2OrImm = unsignedInstruction & 0x3F;
        id_ex.pc = if_id.pc;
        id_ex.valid = 1;
         // IF/ID is now empty because the instruction moved to ID/EX
        if_id.valid = 0;
       int r2 = id_ex.r2OrImm;
       int imm = signExtend6(id_ex.r2OrImm);

    printf("DECODE OUTPUT:\n");
    printf("R-format: opcode=%d, R1=%d, R2=%d\n", id_ex.opcode, id_ex.r1, r2);
    printf("I-format: opcode=%d, R1=%d, IMM(raw)=%d, IMM(signed)=%d\n",
       id_ex.opcode, id_ex.r1, id_ex.r2OrImm, imm);
    }
}
// Update one flag bit and keep unused SREG bits cleared
void setFlag(int flag, int value) {
    if (value) {
        SREG = SREG | (1 << flag);
    } else {
        SREG = SREG & ~(1 << flag);
    }
    // keep bits 7, 6, 5 always zero
    SREG = SREG & 0x1F;
}

int getFlag(int flag) {
    return (SREG >> flag) & 1;
}
// N is set for negative results, Z is set for zero results
void updateNZFlags(int8_t result) {
    setFlag(N_FLAG, result < 0);
    setFlag(Z_FLAG, result == 0);
}
// ADD affects C, V, N, Z, and S
void updateADDFlags(int8_t oldValue, int8_t operand, int8_t result) {
    // Carry is checked using unsigned 8-bit addition
    int unsignedSum = (oldValue & 0xFF) + (operand & 0xFF);
    setFlag(C_FLAG, (unsignedSum & 0x100) != 0);
   // Overflow happens when same-sign operands give opposite-sign result
    setFlag(V_FLAG, 
        ((oldValue >= 0 && operand >= 0 && result < 0) ||
         (oldValue < 0 && operand < 0 && result >= 0))
    );
    // Negative and Zero
    updateNZFlags(result);
    // Sign = N XOR V
    setFlag(S_FLAG, getFlag(N_FLAG) ^ getFlag(V_FLAG));
}
// SUB affects V, N, Z, and S
void updateSUBFlags(int8_t oldValue, int8_t operand, int8_t result) {
    // Overflow: operands have different signs, result has same sign as subtrahend
    setFlag(V_FLAG,
        ((oldValue >= 0 && operand < 0 && result < 0) ||
         (oldValue < 0 && operand >= 0 && result >= 0))
    );
    // Negative and Zero
    updateNZFlags(result);
    // Sign = N XOR V
    setFlag(S_FLAG, getFlag(N_FLAG) ^ getFlag(V_FLAG));
}
// Execute the instruction currently stored in ID/EX
int executeDecodedInstruction() {
    // Empty execute stage, happens while pipeline is filling or draining
    if (!id_ex.valid) {
        printf("EXECUTE: empty\n");
        return 0 ;
    }

    int flush =0;
    // Local copies make the switch cases easier to read
    int opcode=id_ex.opcode;
    int r1= id_ex.r1;
    int r2OrImm = id_ex.r2OrImm;
    int instructionPC= id_ex.pc;
    int imm = signExtend6(r2OrImm);
    printf("EXECUTE INPUT: opcode=%d, r1=%d, r2OrImm=%d, signedImm=%d, instructionPC=%d\n",
       opcode, r1, r2OrImm, imm, instructionPC);
    switch (opcode) {
        case 0: { // ADD
            int8_t oldValue = registers[r1];
            int8_t operand = registers[r2OrImm];
            int8_t result = oldValue + operand;

            registers[r1] = result;
            printf("REGISTER UPDATE in EX stage: R%d changed to %d\n", r1, registers[r1]);
            updateADDFlags(oldValue, operand, result);

            printf("Executed ADD: R%d = %d\n", r1, registers[r1]);
            break;
        }
        case 1: { // SUB
            int8_t oldValue = registers[r1];
            int8_t operand = registers[r2OrImm];
            int8_t result = oldValue - operand;

            registers[r1] = result;
            printf("REGISTER UPDATE in EX stage: R%d changed to %d\n", r1, registers[r1]);
            updateSUBFlags(oldValue, operand, result);

            printf("Executed SUB: R%d = %d\n", r1, registers[r1]);
            break;
        }
        case 2: // MUL
            registers[r1] = registers[r1] * registers[r2OrImm];
            printf("REGISTER UPDATE in EX stage: R%d changed to %d\n", r1, registers[r1]);
            updateNZFlags(registers[r1]);
            printf("Executed MUL: R%d = %d\n", r1, registers[r1]);
            break;
        case 3: // MOVI
            registers[r1] = imm;
            printf("REGISTER UPDATE in EX stage: R%d changed to %d\n", r1, registers[r1]);
            printf("Executed MOVI: R%d = %d\n", r1, registers[r1]);
            break;

        case 4: // BEQZ
        printf("register of branch : %d", registers[r1]);
            if (registers[r1] == 0) {
               // Branch target uses the saved branch PC, not the current PC
                PC = instructionPC +1+ imm;
                printf("PC UPDATE in EX stage: PC changed to %d\n", PC);
                // Since PC changed, the instruction fetched after the branch may be wrong
                flush=1;
                printf("Executed BEQZ: branch taken, PC = %d\n", PC);
            } else {
                printf("Executed BEQZ: branch not taken\n");
            }
            break;

        case 5: { // ANDI
            int8_t result = registers[r1] & imm;

            registers[r1] = result;
            printf("REGISTER UPDATE in EX stage: R%d changed to %d\n",
       r1, registers[r1]);
            updateNZFlags(result);

            printf("Executed ANDI: R%d = %d\n", r1, registers[r1]);
            break;
        }
        case 6: { // EOR
            int8_t result = registers[r1] ^ registers[r2OrImm];

            registers[r1] = result;
            printf("REGISTER UPDATE in EX stage: R%d changed to %d\n",
       r1, registers[r1]);
            updateNZFlags(result);

            printf("Executed EOR: R%d = %d\n", r1, registers[r1]);
            break;
        }
        case 7: { // BR
            uint16_t highByte = (uint8_t)registers[r1];
            uint16_t lowByte = (uint8_t)registers[r2OrImm];
            flush=1;
            // Build a 16-bit address from two 8-bit registers
            PC = (highByte << 8) | lowByte;
            printf("PC UPDATE in EX stage: PC changed to %d\n", PC);
            printf("Executed BR: PC = %d\n", PC);
            break;
        }

        case 8: { // SLC
            int shift = r2OrImm % 8;
            // Use uint8_t so the circular shift works on exactly 8 bits
            uint8_t value = (uint8_t)registers[r1];
            uint8_t result;
            if (shift == 0)
                result = value;
            else
                result = (value << shift) | (value >> (8 - shift));
            registers[r1] = (int8_t)result;
            printf("REGISTER UPDATE in EX stage: R%d changed to %d\n",
       r1, registers[r1]);
            updateNZFlags(registers[r1]);
            printf("Executed SLC: R%d = %d\n", r1, registers[r1]);
            break;
        }

        case 9: { // SRC
            int shift = r2OrImm % 8;
            uint8_t value = (uint8_t)registers[r1];
            uint8_t result;
            if (shift == 0)
                result = value;
            else
                result = (value >> shift) | (value << (8 - shift));
            registers[r1] = (int8_t)result;
            printf("REGISTER UPDATE in EX stage: R%d changed to %d\n",
       r1, registers[r1]);
            updateNZFlags(registers[r1]);
            printf("Executed SRC: R%d = %d\n", r1, registers[r1]);
            break;
        }

        case 10: // LDR
            // Load one byte from data memory into R1
            registers[r1] = dataMemory[r2OrImm];
            printf("REGISTER UPDATE in EX stage: R%d changed to %d\n",
       r1, registers[r1]);
            printf("Executed LDR: R%d = DataMemory[%d] = %d\n",
                   r1, r2OrImm, registers[r1]);
            break;

        case 11: // STR
            // Store one register value into data memory
            dataMemory[r2OrImm] = registers[r1];
            printf("DATA MEMORY UPDATE in EX stage: DataMemory[%d] changed to %d\n",
       r2OrImm, dataMemory[r2OrImm]);
            printf("Executed STR: DataMemory[%d] = %d\n",
                   r2OrImm, dataMemory[r2OrImm]);
            break;

        default:
            printf("Unknown opcode = %d\n", opcode);
    }
    // Instruction finished execute, so ID/EX becomes empty
     id_ex.valid = 0;
     return flush;
}
void printSREG() {
    printf("SREG = ");
    for (int i = 7; i >= 0; i--) {
        printf("%d", (SREG >> i) & 1);
    }
    printf("\n");
    printf("C=%d V=%d N=%d S=%d Z=%d\n",
           getFlag(C_FLAG),
           getFlag(V_FLAG),
           getFlag(N_FLAG),
           getFlag(S_FLAG),
           getFlag(Z_FLAG));
}

void printInstructionMemory() { //non zero elements for now
    printf("\n========== Instruction Memory ==========\n");
    for (int i = 0; i < instructionCount; i++) {
        printf("InstructionMemory[%d] = %d = ",
               i,
               instructionMemory[i]);
        printBinary16(instructionMemory[i]);
        printf("\n");
    }
}

void printDataMemory() { //non zero elements for now
    printf("\n========== Data Memory ==========\n");
    for (int i = 0; i < DATA_MEMORY_SIZE; i++) {
        if (dataMemory[i] != 0) {
            printf("DataMemory[%d] = %d\n",
                   i,
                   dataMemory[i]); 
            printf("\n");
        }
    }
}

void printRegisters() {
    printf("\n========== Registers ==========\n");

    for (int i = 0; i < REGISTER_COUNT; i++) {
     
            printf("R%d = %d\n", i, registers[i]);
        
    }

    printf("PC = %d\n", PC);
    printf("SREG = %d\n", SREG);
}
// Remove the wrong-path instruction after a taken branch or BR
void flushAfterBranchOrJump() {
    printf("FLUSH: removing wrong instruction from IF/ID\n");

    if_id.instruction = 0;
    if_id.pc = 0;
    if_id.valid = 0;
}
// Returns true if this instruction writes a result to R1
int instructionWritesToRegister(int opcode) { //return 1 if instruction writes to reg
    return opcode == 0 ||  // ADD
           opcode == 1 ||  // SUB
           opcode == 2 ||  // MUL
           opcode == 3 ||  // MOVI
           opcode == 5 ||  // ANDI
           opcode == 6 ||  // EOR
           opcode == 8 ||  // SLC
           opcode == 9 ||  // SRC
           opcode == 10;   // LDR
}
// Checks if the instruction needs to read a specific register
int instructionReadsRegister(short int instruction, int reg) { 
    unsigned short u = (unsigned short) instruction;

    int opcode = (u >> 12) & 0xF;
    int r1 = (u >> 6) & 0x3F;
    int r2OrImm = u & 0x3F;

    switch (opcode) {
        case 0: // ADD R1 R2
        case 1: // SUB R1 R2
        case 2: // MUL R1 R2
        case 6: // EOR R1 R2
        case 7: // BR R1 R2
            return r1 == reg || r2OrImm == reg;

        case 4:  // BEQZ R1 IMM
        case 5:  // ANDI R1 IMM
        case 8:  // SLC R1 IMM
        case 9:  // SRC R1 IMM
        case 11: // STR R1 ADDRESS
            return r1 == reg;

        case 3:  // MOVI R1 IMM
        case 10: // LDR R1 ADDRESS
            return 0;

        default:
            return 0;
    }
}

int hasDataHazard() { //checks whether their exists data hazards or not 
    if (!id_ex.valid || !if_id.valid) {
        return 0;
    }

    int producerOpcode = id_ex.opcode;
// If EX does not write to a register, there is no register hazard
    if (!instructionWritesToRegister(producerOpcode)) { 
        return 0;
    }

    int destinationRegister = id_ex.r1;
// If the next instruction reads the register EX will write, stall
    if (instructionReadsRegister(if_id.instruction, destinationRegister)) {
        printf("DATA HAZARD: instruction in EX writes R%d, instruction in ID needs R%d\n",
            destinationRegister,
            destinationRegister);
        return 1;
    }

    return 0;
}
// Main clock-cycle loop for the 3-stage pipeline
void simulatePipeline() {
    int cycle = 1;
    PC = 0;
    if_id.valid = 0;
    id_ex.valid = 0;
    // Continue until no instructions are left to fetch and the pipeline is empty
    while (PC < instructionCount || if_id.valid || id_ex.valid) {
        printf("\n====================\n");
        printf("Clock Cycle %d\n", cycle);
        printf("====================\n");
        // Check hazard before execute because execute clears ID/EX
        int stall = hasDataHazard();
        printf("\n--- Execute Stage ---\n");
        // Run stages backwards so old pipeline registers are used first
        int flush = executeDecodedInstruction();
        // Control hazard: flush and fetch from the new PC next cycle
        if (flush) {
            printf("\nCONTROL HAZARD: branch/jump changed PC\n");
            flushAfterBranchOrJump();
            printf("\n--- Decode Stage ---\n");
            printf("DECODE: flushed / skipped this cycle\n");
            printf("\n--- Fetch Stage ---\n");
            printf("FETCH: skipped this cycle, will fetch from new PC next cycle\n");
            printSREG();
            cycle++;
            continue;
        }
        // Data hazard: let EX finish, but keep IF/ID and PC unchanged
        if (stall) {
            printf("\n--- Decode Stage ---\n");
            printf("DECODE: stalled because of data hazard\n");

            printf("\n--- Fetch Stage ---\n");
            printf("FETCH: stalled, PC stays the same\n");

            printSREG();

            cycle++;
            continue;
        }

        printf("\n--- Decode Stage ---\n");
        // Normal pipeline movement: decode then fetch
        if (if_id.valid) {
            decodeInstruction();
        } else {
            printf("DECODE: empty\n");
        }

        printf("\n--- Fetch Stage ---\n");
        if (PC < instructionCount) {
            fetchInstruction();
        } else {
            printf("FETCH: empty\n");
        }

        printSREG();

        cycle++;
    }

    printf("\n====================\n");
    printf("Program Finished\n");
    printf("====================\n");

    printInstructionMemory();
    printDataMemory();
    printRegisters();
    printSREG();
}
// Load program first, then start the pipeline simulation
int main() {
    loadProgram("program.txt");
    simulatePipeline();
    return 0;

}
