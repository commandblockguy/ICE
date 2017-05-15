#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tice.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>

#include <fileioc.h>
#include <graphx.h>

#include "parse.h"
#include "main.h"
#include "errors.h"
#include "output.h"
#include "operator.h"
#include "stack.h"
#include "functions.h"

extern void (*operatorFunctions[280])(void);
const char operators[] = {tStore, tAnd, tXor, tOr, tEQ, tLT, tGT, tLE, tGE, tNE, tMul, tDiv, tAdd, tSub};
const uint8_t operatorPrecedence[] = {0, 1, 2, 2, 3, 3, 3, 3, 3, 3, 5, 5, 4, 4};

static element_t *entry1;
static element_t *entry2;
static uint24_t entry1_operand;
static uint24_t entry2_operand;
static uint8_t oper;

uint8_t getIndexOfOperator(uint8_t operator) {
    char *index;
    if ((index = strchr(operators, operator))) {
        return index - operators + 1;
    }
    return 0;
}

uint24_t executeOperator(uint24_t operand1, uint24_t operand2, uint8_t operator) {
    switch (operator) {
        case tAdd:
            return operand1 + operand2;
        case tSub:
            return operand1 - operand2;
        case tMul:
            return operand1 * operand2;
        case tDiv:
            return operand1 / operand2;
        case tNE:
            return operand1 != operand2;
        case tGE:
            return operand1 >= operand2;
        case tLE:
            return operand1 <= operand2;
        case tGT:
            return operand1 > operand2;
        case tLT:
            return operand1 < operand2;
        case tEQ:
            return operand1 == operand2;
        case tOr:
            return operand1 || operand2;
        case tXor:
            return !operand1 != !operand2;
        case tAnd:
            return operand1 && operand2;
        default:
            return operand1;
    }
}

static void getEntryOperands() {
    entry1_operand = entry1->operand;
    entry2_operand = entry2->operand;
}

static void swapEntries() {
    element_t *temp;
    temp = entry1;
    entry1 = entry2;
    entry2 = temp;
    getEntryOperands();
}

uint8_t parseOperator(element_t *outputPrevPrev, element_t *outputPrev, element_t *outputCurr) {
    uint8_t typeMasked1 = outputPrevPrev->type & 15;
    uint8_t typeMasked2 = outputPrev->type & 15;
    oper = (uint8_t)outputCurr->operand;

    // Only call the function if both types are valid
    if ( (typeMasked1 == typeMasked2 && (typeMasked1 == TYPE_NUMBER || typeMasked1 == TYPE_CHAIN_ANS)) ||
         (typeMasked1 > TYPE_CHAIN_PUSH && typeMasked2 > TYPE_CHAIN_ANS) ||
         (oper == tStore && typeMasked2 != TYPE_VARIABLE) ) {
        return E_SYNTAX;
    }
    
    // If you have something like "A or 1", the output is always 1, so we can remove the "ld hl, (A)"
    ice.programPtrBackup = ice.programPtr;
    
    // Call the right function!
    entry1 = outputPrevPrev;
    entry2 = outputPrev;
    getEntryOperands();

    // Swap operands for optimizations
    if (oper == tLE || oper == tLT) {
        swapEntries();
    }
    (*operatorFunctions[((getIndexOfOperator(oper) - 1) * 20) + (typeMasked1 * 4) + typeMasked2])();
    return VALID;
}

void insertFunctionReturn(uint24_t function, uint8_t outputRegister, bool needPush) {
    if ((uint8_t)function == tRand) {
        // We need to save a register before using the routine
        if (needPush) {
            if (outputRegister == OUTPUT_IN_HL) {
                PUSH_DE();
            } else {
                PUSH_HL();
            }
        }
        
        // Store the pointer to the call to the stack, to replace later
        ice.dataOffsetStack[ice.dataOffsetElements++] = (uint24_t*)(ice.programPtr + 1);
        
        // We need to add the rand routine to the data section
        if (!ice.usedAlreadyRand) {
            ice.randAddr = (uint24_t)ice.programDataPtr;
            memcpy(ice.programDataPtr, RandRoutine, 54);
            ice.programDataPtr += 54;
            ice.usedAlreadyRand = true;
        }
        
        CALL(ice.randAddr);
        
        // Store the value to the right register
        if (outputRegister == OUTPUT_IN_DE) {
            EX_DE_HL();
        } else if (outputRegister == OUTPUT_IN_BC) {
            PUSH_HL();
            POP_BC();
        }
        
        // And restore the register of course
        if (needPush) {
            if (outputRegister == OUTPUT_IN_HL) {
                POP_DE();
            } else {
                POP_HL();
            }
        }
    } else {
        // Check if the getKey has a fast direct key argument; if so, the second byte is 1
        if ((uint8_t)(function >> 8)) {
            uint8_t key = function >> 16;
            uint8_t keyAddress = 0x1E - (((key-1) >> 2) & 14);
            uint8_t keyBit = 1, a;
            
            // Get the right bit for the keypress
            if ((key - 1) & 7) {
                for (a = 0; a < ((key - 1) & 7); a++) {
                    keyBit = keyBit << 1;
                }
            }
            
            LD_B(keyAddress);
            LD_C(keyBit);
            
            // Check if we need to preserve HL
            if (NEED_PUSH && outputRegister != OUTPUT_IN_HL) {
                PUSH_HL();
            }
            
            
            // Store the pointer to the call to the stack, to replace later
            ice.dataOffsetStack[ice.dataOffsetElements++] = (uint24_t*)(ice.programPtr + 1);
            
            // We need to add the getKeyFast routine to the data section
            if (!ice.usedAlreadyGetKeyFast) {
                ice.getKeyFastAddr = (uint24_t)ice.programDataPtr;
                memcpy(ice.programDataPtr, KeypadRoutine, 19);
                ice.programDataPtr += 19;
                ice.usedAlreadyGetKeyFast = true;
            }
            
            CALL(ice.getKeyFastAddr);
            
            // Store the keypress in the right register
            if (outputRegister == OUTPUT_IN_DE) {
                EX_DE_HL();
            } else if (outputRegister == OUTPUT_IN_BC) {
                PUSH_HL();
                POP_BC();
            }
            
            // Check if we need to preserve HL
            if (NEED_PUSH && outputRegister != OUTPUT_IN_HL) {
                POP_HL();
            }
        }
        
        // Else, a standalone "getKey"
        else {
            // The key should be in HL
            if (outputRegister == OUTPUT_IN_HL) {
                CALL(_os_GetCSC);
            }
            
            // The key should be in DE
            else if (outputRegister == OUTPUT_IN_DE) {
                // HL may not be destroyed
                if (needPush) {
                    PUSH_HL();
                    CALL(_GetCSC);
                    POP_HL();
                } else {
                    CALL(_GetCSC);
                }
                LD_DE_IMM(0);
                LD_E_A();
            }
            
            // The key should be in BC
            else if (outputRegister == OUTPUT_IN_BC) {
                // HL may not be destroyed
                if (needPush) {
                    PUSH_HL();
                    CALL(_GetCSC);
                    POP_HL();
                } else {
                    CALL(_GetCSC);
                }
                LD_BC_IMM(0);
                LD_C_A();
            }
        }
    }
}

void LD_HL_NUMBER(uint24_t number) {
    if (!number) {
        OR_A_A();
        SBC_HL_HL();
    } else {
        LD_HL_IMM(number);
    }
}

void OperatorError(void) {
    // This *should* never be triggered
    displayError(E_ICE_ERROR);
}
void StoChainAnsVariable(void) {
    LD_IX_OFF_IND_HL(entry2_operand);
}
void StoNumberVariable(void) {
    LD_HL_NUMBER(entry1_operand);
    StoChainAnsVariable();
}
void StoVariableVariable(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    StoChainAnsVariable();
}
void StoFunctionVariable(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    StoChainAnsVariable();
}
void StoChainPushVariable(void) {
    POP_HL();
    StoChainAnsVariable();
}
void AndInsert(void) {
    uint8_t *op = AndOrXorData + 10;
    if (oper == tAnd) {
        *op = OP_AND_A_D;
    } else if (oper == tOr) {
        *op = OP_OR_A_D;
    } else {
        *op = OP_XOR_A_D;
    }
    memcpy(ice.programPtr, AndOrXorData, sizeof AndOrXorData);
    ice.programPtr += 16;
}
void AndChainAnsNumber(void) {
    if (oper == tXor) {
        LD_DE_IMM(-1);
        ADD_HL_DE();
        if (!entry2_operand) {
            CCF();
        }
        SBC_HL_HL();
        INC_HL();
    } else if (oper == tAnd) {
        if (!entry2_operand) {
            ice.programPtr = ice.programPtrBackup;
            LD_HL_NUMBER(0);
        } else {
            LD_DE_IMM(-1);
            ADD_HL_DE();
            CCF();
            SBC_HL_HL();
            INC_HL();
        }
    } else {
        if (!entry2_operand) {
            LD_DE_IMM(-1);
            ADD_HL_DE();
            CCF();
            SBC_HL_HL();
            INC_HL();
        } else {
            ice.programPtr = ice.programPtrBackup;
            LD_HL_NUMBER(1);
        }
    }
}
void AndChainAnsVariable(void) {
    LD_DE_IND_IX_OFF(entry2_operand);
    AndInsert();
}
void AndChainAnsFunction(void) {
    EX_DE_HL();
    insertFunctionReturn(entry2_operand, OUTPUT_IN_HL, NEED_PUSH);
    AndInsert();
}
void AndFunctionNumber(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    AndChainAnsNumber();
}
void AndVariableNumber(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    AndChainAnsNumber();
}
void AndFunctionVariable(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    AndChainAnsVariable();
}
void AndNumberVariable(void) {
    swapEntries();
    AndVariableNumber();
}
void AndNumberFunction(void) {
    swapEntries();
    AndFunctionNumber();
}
void AndNumberChainAns(void) {
    swapEntries();
    AndChainAnsNumber();
}
void AndVariableVariable(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    AndChainAnsVariable();
}
void AndVariableFunction(void) {
    swapEntries();
    AndFunctionVariable();
}
void AndVariableChainAns() {
    swapEntries();
    AndChainAnsVariable();
}
void AndFunctionFunction(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_DE, NO_PUSH);
    insertFunctionReturn(entry2_operand, OUTPUT_IN_HL, NEED_PUSH);
    AndInsert();
}
void AndFunctionChainAns(void) {
    swapEntries();
    AndChainAnsFunction();
}
void AndChainPushNumber(void) {
    POP_HL();
    AndChainAnsNumber();
}
void AndChainPushVariable(void) {
    POP_HL();
    AndChainAnsVariable();
}
void AndChainPushFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_HL, NO_PUSH);
    POP_DE();
    AndInsert();
}
void AndChainPushChainAns(void) {
    POP_DE();
    AndInsert();
}

#define XorNumberVariable    AndNumberVariable   
#define XorNumberFunction    AndNumberFunction   
#define XorNumberChainAns    AndNumberChainAns   
#define XorVariableNumber    AndVariableNumber   
#define XorVariableVariable  AndVariableVariable 
#define XorVariableFunction  AndVariableFunction 
#define XorVariableChainAns  AndVariableChainAns 
#define XorFunctionNumber    AndFunctionNumber   
#define XorFunctionVariable  AndFunctionVariable 
#define XorFunctionFunction  AndFunctionFunction 
#define XorFunctionChainAns  AndFunctionChainAns 
#define XorChainAnsNumber    AndChainAnsNumber   
#define XorChainAnsVariable  AndChainAnsVariable 
#define XorChainAnsFunction  AndChainAnsFunction 
#define XorChainPushNumber   AndChainPushNumber  
#define XorChainPushVariable AndChainPushVariable
#define XorChainPushFunction AndChainPushFunction
#define XorChainPushChainAns AndChainPushChainAns

#define OrNumberVariable    AndNumberVariable   
#define OrNumberFunction    AndNumberFunction   
#define OrNumberChainAns    AndNumberChainAns   
#define OrVariableNumber    AndVariableNumber   
#define OrVariableVariable  AndVariableVariable 
#define OrVariableFunction  AndVariableFunction 
#define OrVariableChainAns  AndVariableChainAns 
#define OrFunctionNumber    AndFunctionNumber   
#define OrFunctionVariable  AndFunctionVariable 
#define OrFunctionFunction  AndFunctionFunction 
#define OrFunctionChainAns  AndFunctionChainAns 
#define OrChainAnsNumber    AndChainAnsNumber   
#define OrChainAnsVariable  AndChainAnsVariable 
#define OrChainAnsFunction  AndChainAnsFunction 
#define OrChainPushNumber   AndChainPushNumber  
#define OrChainPushVariable AndChainPushVariable
#define OrChainPushFunction AndChainPushFunction
#define OrChainPushChainAns AndChainPushChainAns

void EQInsert() {
    OR_A_A();
    SBC_HL_DE();
    LD_HL_IMM(0);
    if (oper == tEQ) {
        JR_NZ(1);
    } else {
        JR_Z(1);
    }
    INC_HL();
}
void EQChainAnsNumber(void) {
    uint24_t number = entry2_operand;
    if (number && number < 6) {
        do {
            DEC_HL();
        } while (--number);
    }
    if (!number) {
        LD_DE_IMM(-1);
        ADD_HL_DE();
        if (oper == tNE) {
            CCF();
        }
        SBC_HL_HL();
        INC_HL();
    } else {
        LD_DE_IMM(number);
        EQInsert();
    }
}
void EQChainAnsFunction(void) {
    EX_DE_HL();
    insertFunctionReturn(entry2_operand, OUTPUT_IN_HL, NEED_PUSH);
    EQInsert();
}
void EQVariableNumber(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    EQChainAnsNumber();
}
void EQChainAnsVariable(void) {
    LD_DE_IND_IX_OFF(entry2_operand);
    EQInsert();
}
void EQFunctionNumber(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    EQChainAnsNumber();
}
void EQFunctionVariable(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    EQChainAnsVariable();
}
void EQNumberVariable(void) {
    swapEntries();
    EQVariableNumber();
}
void EQNumberFunction(void) {
    swapEntries();
    EQFunctionNumber();
}
void EQNumberChainAns(void) {
    swapEntries();
    EQChainAnsNumber();
}
void EQVariableVariable(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    EQChainAnsVariable();
}
void EQVariableFunction(void) {
    swapEntries();
    EQFunctionVariable();
}
void EQVariableChainAns(void) {
    swapEntries();
    EQChainAnsVariable();
}
void EQFunctionFunction(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_DE, NO_PUSH);
    insertFunctionReturn(entry2_operand, OUTPUT_IN_HL, NEED_PUSH);
    EQInsert();
}
void EQFunctionChainAns(void) {
    swapEntries();
    EQChainAnsFunction();
}
void EQChainPushNumber(void) {
    POP_HL();
    EQChainAnsNumber();
}
void EQChainPushVariable(void) {
    POP_HL();
    EQChainAnsVariable();
}
void EQChainPushFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_HL, NO_PUSH);
    POP_DE();
    EQInsert();
}
void EQChainPushChainAns(void) {
    POP_DE();
    EQInsert();
}
void GEInsert() {
    if (oper == tGE || oper == tLE) {
        OR_A_A();
    } else {
        SCF();
    }
    SBC_HL_DE();
    SBC_HL_HL();
    INC_HL();
}
void GEChainAnsNumber(void) {
    LD_DE_IMM(entry2_operand);
    GEInsert();
}
void GEChainAnsVariable(void) {
    LD_DE_IND_IX_OFF(entry2_operand);
    GEInsert();
}
void GENumberVariable(void) {
    LD_HL_NUMBER(entry1_operand);
    GEChainAnsVariable();
}
void GENumberFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_DE, NO_PUSH);
    LD_HL_NUMBER(entry1_operand);
    GEInsert();
}
void GENumberChainAns(void) {
    EX_DE_HL();
    LD_HL_NUMBER(entry1_operand);
    GEInsert();
}
void GEVariableNumber(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    GEChainAnsNumber();
}
void GEVariableVariable(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    GEChainAnsVariable();
}
void GEVariableFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_DE, NO_PUSH);
    LD_HL_IND_IX_OFF(entry1_operand);
    GEInsert();
}
void GEVariableChainAns(void) {
    EX_DE_HL();
    LD_HL_IND_IX_OFF(entry1_operand);
    GEInsert();
}
void GEFunctionNumber(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    GEChainAnsNumber();
}
void GEFunctionVariable(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    GEChainAnsVariable();
}
void GEFunctionFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_DE, NO_PUSH);
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NEED_PUSH);
    GEInsert();
}
void GEFunctionChainAns(void) {
    EX_DE_HL();
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NEED_PUSH);
}
void GEChainAnsFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_DE, NEED_PUSH);
    GEInsert();
}
void GEChainPushNumber(void) {
    POP_HL();
    GEChainAnsNumber();
}
void GEChainPushVariable(void) {
    POP_HL();
    GEChainAnsVariable();
}
void GEChainPushFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_DE, NO_PUSH);
    POP_HL();
    GEInsert();
}
void GEChainPushChainAns(void) {
    EX_DE_HL();
    POP_HL();
    GEInsert();
}

#define GTNumberVariable    GENumberVariable   
#define GTNumberFunction    GENumberFunction   
#define GTNumberChainAns    GENumberChainAns   
#define GTVariableNumber    GEVariableNumber   
#define GTVariableVariable  GEVariableVariable 
#define GTVariableFunction  GEVariableFunction 
#define GTVariableChainAns  GEVariableChainAns 
#define GTFunctionNumber    GEFunctionNumber   
#define GTFunctionVariable  GEFunctionVariable 
#define GTFunctionFunction  GEFunctionFunction 
#define GTFunctionChainAns  GEFunctionChainAns 
#define GTChainAnsNumber    GEChainAnsNumber   
#define GTChainAnsVariable  GEChainAnsVariable 
#define GTChainAnsFunction  GEChainAnsFunction 
#define GTChainPushNumber   GEChainPushNumber  
#define GTChainPushVariable GEChainPushVariable
#define GTChainPushFunction GEChainPushFunction
#define GTChainPushChainAns GEChainPushChainAns

#define LTNumberVariable   GTVariableNumber
#define LTNumberFunction   GTFunctionNumber
#define LTNumberChainAns   GTChainAnsNumber
#define LTVariableNumber   GTNumberVariable
#define LTVariableVariable GTVariableVariable
#define LTVariableFunction GTFunctionVariable
#define LTVariableChainAns GTChainAnsVariable
#define LTFunctionNumber   GTNumberFunction
#define LTFunctionVariable GTVariableFunction
#define LTFunctionFunction GTFunctionFunction
#define LTFunctionChainAns GTChainAnsFunction
#define LTChainAnsNumber   GTNumberChainAns
#define LTChainAnsVariable GTVariableChainAns
#define LTChainAnsFunction GTFunctionChainAns

void LTChainPushNumber(void) {
    POP_HL();
    GTNumberChainAns();
}
void LTChainPushVariable(void) {
    POP_HL();
    GTVariableChainAns();
}
void LTChainPushChainAns(void) {
    POP_DE();
    EX_DE_HL();
    SCF();
    SBC_HL_DE();
    SBC_HL_HL();
    INC_HL();
}
void LTChainPushFunction(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    LTChainPushChainAns();
}

#define LENumberVariable   GEVariableNumber
#define LENumberFunction   GEFunctionNumber
#define LENumberChainAns   GEChainAnsNumber
#define LEVariableNumber   GENumberVariable
#define LEVariableVariable GEVariableVariable
#define LEVariableFunction GEFunctionVariable
#define LEVariableChainAns GEChainAnsVariable
#define LEFunctionNumber   GENumberFunction
#define LEFunctionVariable GEVariableFunction
#define LEFunctionFunction GEFunctionFunction
#define LEFunctionChainAns GEChainAnsFunction
#define LEChainAnsNumber   GENumberChainAns
#define LEChainAnsVariable GEVariableChainAns
#define LEChainAnsFunction GEFunctionChainAns

void LEChainPushNumber(void) {
    POP_HL();
    GENumberChainAns();
}
void LEChainPushVariable(void) {
    POP_HL();
    GEVariableChainAns();
}
void LEChainPushChainAns(void) {
    POP_DE();
    EX_DE_HL();
    OR_A_A();
    SBC_HL_DE();
    SBC_HL_HL();
    INC_HL();
}
void LEChainPushFunction(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    LEChainPushChainAns();
}

#define NENumberVariable    EQVariableNumber
#define NENumberFunction    EQFunctionNumber
#define NENumberChainAns    EQChainAnsNumber
#define NEVariableNumber    EQNumberVariable
#define NEVariableVariable  EQVariableVariable
#define NEVariableFunction  EQFunctionVariable
#define NEVariableChainAns  EQChainAnsVariable
#define NEFunctionNumber    EQNumberFunction
#define NEFunctionVariable  EQVariableFunction
#define NEFunctionFunction  EQFunctionFunction
#define NEFunctionChainAns  EQChainAnsFunction
#define NEChainAnsNumber    EQNumberChainAns
#define NEChainAnsVariable  EQVariableChainAns
#define NEChainAnsFunction  EQFunctionChainAns
#define NEChainPushNumber   EQChainPushNumber
#define NEChainPushVariable EQChainPushVariable
#define NEChainPushFunction EQChainPushFunction
#define NEChainPushChainAns EQChainPushChainAns

void MulChainAnsNumber(void) {
    uint24_t number = entry2_operand;
    if (number == 0) {
        ice.programPtr = ice.programPtrBackup;
        LD_HL_NUMBER(0);
    } else {
        MultWithNumber(number, (uint24_t *)&ice.programPtr);
    }
}
void MulVariableNumber(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    MulChainAnsNumber();
}
void MulFunctionNumber(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    MulChainAnsNumber();
}
void MulChainAnsVariable(void) {
    LD_BC_IND_IX_OFF(entry2_operand);
    CALL(__imuls);
}
void MulFunctionVariable(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    MulChainAnsVariable();
}
void MulNumberVariable(void) {
    swapEntries();
    MulVariableNumber();
}
void MulNumberFunction(void) {
    swapEntries();
    MulFunctionNumber();
}
void MulNumberChainAns(void) {
    swapEntries();
    MulChainAnsNumber();
}
void MulVariableVariable(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    MulChainAnsVariable();
}
void MulVariableFunction(void) {
    swapEntries();
    MulFunctionVariable();
}
void MulVariableChainAns(void) {
    swapEntries();
    MulChainAnsVariable();
}
void MulFunctionFunction(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    PUSH_HL();
    insertFunctionReturn(entry2_operand, OUTPUT_IN_HL, NO_PUSH);
    POP_BC();
    CALL(__imuls);
}
void MulChainAnsFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_BC, NEED_PUSH);
    CALL(__imuls);
}
void MulFunctionChainAns(void) {
    swapEntries();
    MulChainAnsFunction();
}
void MulChainPushNumber(void) {
    POP_HL();
    MulChainAnsNumber();
}
void MulChainPushVariable(void) {
    POP_HL();
    MulChainAnsVariable();
}
void MulChainPushFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_HL, NO_PUSH);
    POP_BC();
    CALL(__imuls);
}
void MulChainPushChainAns(void) {
    POP_BC();
    CALL(__imuls);
}
void DivChainAnsNumber(void) {
    LD_BC_IMM(entry2_operand);
    CALL(__idvrmu);
    EX_DE_HL();
}
void DivChainAnsVariable(void) {
    LD_BC_IND_IX_OFF(entry2_operand);
    CALL(__idvrmu);
    EX_DE_HL();
}
void DivNumberVariable(void) {
    LD_HL_NUMBER(entry1_operand);
    DivChainAnsVariable();
}
void DivNumberFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_BC, NO_PUSH);
    LD_HL_NUMBER(entry1_operand);
    CALL(__idvrmu);
    EX_DE_HL();
}
void DivNumberChainAns(void) {
    PUSH_HL();
    POP_BC();
    LD_HL_NUMBER(entry1_operand);
    CALL(__idvrmu);
    EX_DE_HL();
}
void DivVariableNumber(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    DivChainAnsNumber();
}
void DivVariableVariable(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    DivChainAnsVariable();
}
void DivVariableFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_BC, NO_PUSH);
    LD_HL_IND_IX_OFF(entry1_operand);
    CALL(__idvrmu);
    EX_DE_HL();
}
void DivVariableChainAns(void) {
    PUSH_HL();
    POP_BC();
    LD_HL_IND_IX_OFF(entry1_operand);
    CALL(__idvrmu);
    EX_DE_HL();
}
void DivFunctionNumber(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    DivChainAnsNumber();
}
void DivFunctionVariable(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    DivChainAnsVariable();
}
void DivFunctionFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_HL, NO_PUSH);
    PUSH_HL();
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    POP_BC();
    CALL(__idvrmu);
    EX_DE_HL();
}
void DivFunctionChainAns(void) {
    PUSH_HL();
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    POP_BC();
    CALL(__idvrmu);
    EX_DE_HL();
}
void DivChainAnsFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_BC, NEED_PUSH);
    CALL(__idvrmu);
    EX_DE_HL();
}
void DivChainPushNumber(void) {
    POP_HL();
    DivChainAnsNumber();
}
void DivChainPushVariable(void) {
    POP_HL();
    DivChainAnsVariable();
}
void DivChainPushFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_BC, NO_PUSH);
    POP_HL();
    CALL(__idvrmu);
    EX_DE_HL();
}
void DivChainPushChainAns(void) {
    PUSH_HL();
    POP_BC();
    POP_HL();
    CALL(__idvrmu);
    EX_DE_HL();
}
void AddChainAnsNumber(void) {
    uint24_t number = entry2_operand;
    if (number < 5) {
        uint8_t a;
        for (a = 0; a < (uint8_t)number; a++) {
            INC_HL();
        }
    } else {
        LD_DE_IMM(number);
        ADD_HL_DE();
    }
}
void AddVariableNumber(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    AddChainAnsNumber();
}
void AddFunctionNumber(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    AddChainAnsNumber();
}
void AddChainAnsVariable(void) {
    LD_DE_IND_IX_OFF(entry2_operand);
    ADD_HL_DE();
}
void AddFunctionVariable(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    AddChainAnsVariable();
}
void AddNumberVariable(void) {
    swapEntries();
    AddVariableNumber();
}
void AddNumberFunction(void) {
    swapEntries();
    AddFunctionNumber();
}
void AddNumberChainAns(void) {
    swapEntries();
    AddChainAnsNumber();
}
void AddVariableVariable(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    if (entry1_operand == entry2_operand) {
        ADD_HL_HL();
    } else {
        AddChainAnsVariable();
    }
}
void AddVariableFunction(void) {
    swapEntries();
    AddFunctionVariable();
}
void AddVariableChainAns(void) {
    swapEntries();
    AddChainAnsVariable();
}
void AddFunctionFunction(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_DE, NO_PUSH);
    insertFunctionReturn(entry2_operand, OUTPUT_IN_HL, NEED_PUSH);
    ADD_HL_DE();
}
void AddChainAnsFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_DE, NEED_PUSH);
    ADD_HL_DE();
}
void AddFunctionChainAns(void) {
    swapEntries();
    AddChainAnsFunction();
}
void AddChainPushNumber(void) {
    POP_HL();
    AddChainAnsNumber();
}
void AddChainPushVariable(void) {
    POP_HL();
    AddChainAnsVariable();
}
void AddChainPushFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_HL, NO_PUSH);
    POP_DE();
    ADD_HL_DE();
}
void AddChainPushChainAns(void) {
    POP_DE();
    ADD_HL_DE();
}
void SubChainAnsNumber(void) {
    uint24_t number = entry2_operand;
    if (number < 5) {
        uint8_t a;
        for (a = 0; a < (uint8_t)number; a++) {
            DEC_HL();
        }
    } else {
        LD_DE_IMM(0x1000000 - number);
        ADD_HL_DE();
    }
}
void SubChainAnsVariable(void) {
    LD_DE_IND_IX_OFF(entry2_operand);
    SBC_HL_DE();
}
void SubNumberVariable(void) {
    LD_HL_NUMBER(entry1_operand);
    SubChainAnsVariable();
}
void SubNumberFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_DE, NO_PUSH);
    LD_HL_NUMBER(entry1_operand);
    SBC_HL_DE();
}
void SubNumberChainAns(void) {
    EX_DE_HL();
    LD_HL_NUMBER(entry1_operand);
    SBC_HL_DE();
}
void SubVariableNumber(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    SubChainAnsNumber();
}
void SubVariableVariable(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    SubChainAnsVariable();
}
void SubVariableFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_DE, NO_PUSH);
    LD_HL_IND_IX_OFF(entry1_operand);
}
void SubVariableChainAns(void) {
    EX_DE_HL();
    LD_HL_IND_IX_OFF(entry1_operand);
    SBC_HL_DE();
}
void SubFunctionNumber(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    SubChainAnsNumber();
}
void SubFunctionVariable(void) {
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NO_PUSH);
    SubChainAnsVariable();
}
void SubFunctionFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_DE, NO_PUSH);
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NEED_PUSH);
    SBC_HL_DE();
}
void SubFunctionChainAns(void) {
    EX_DE_HL();
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NEED_PUSH);
    SBC_HL_DE();
}
void SubChainAnsFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_DE, NEED_PUSH);
    SBC_HL_DE();
}
void SubChainPushNumber(void) {
    POP_HL();
    SubChainAnsNumber();
}
void SubChainPushVariable(void) {
    POP_HL();
    SubChainAnsVariable();
}
void SubChainPushFunction(void) {
    insertFunctionReturn(entry2_operand, OUTPUT_IN_DE, NO_PUSH);
    POP_HL();
    SBC_HL_DE();
}
void SubChainPushChainAns(void) {
    EX_DE_HL();
    POP_HL();
    SBC_HL_DE();
}

void (*operatorFunctions[280])(void) = {
    OperatorError,
    StoNumberVariable,
    OperatorError,
    OperatorError,
    OperatorError,
    StoVariableVariable,
    OperatorError,
    OperatorError,
    OperatorError,
    StoFunctionVariable,
    OperatorError,
    OperatorError,
    OperatorError,
    StoChainAnsVariable,
    OperatorError,
    OperatorError,
    OperatorError,
    StoChainPushVariable,
    OperatorError,
    OperatorError,
    
    OperatorError,
    AndNumberVariable,
    AndNumberFunction,
    AndNumberChainAns,
    AndVariableNumber,
    AndVariableVariable,
    AndVariableFunction,
    AndVariableChainAns,
    AndFunctionNumber,
    AndFunctionVariable,
    AndFunctionFunction,
    AndFunctionChainAns,
    AndChainAnsNumber,
    AndChainAnsVariable,
    AndChainAnsFunction,
    OperatorError,
    AndChainPushNumber,
    AndChainPushVariable,
    AndChainPushFunction,
    AndChainPushChainAns,
    
    OperatorError,
    XorNumberVariable,
    XorNumberFunction,
    XorNumberChainAns,
    XorVariableNumber,
    XorVariableVariable,
    XorVariableFunction,
    XorVariableChainAns,
    XorFunctionNumber,
    XorFunctionVariable,
    XorFunctionFunction,
    XorFunctionChainAns,
    XorChainAnsNumber,
    XorChainAnsVariable,
    XorChainAnsFunction,
    OperatorError,
    XorChainPushNumber,
    XorChainPushVariable,
    XorChainPushFunction,
    XorChainPushChainAns,
    
    OperatorError,
    OrNumberVariable,
    OrNumberFunction,
    OrNumberChainAns,
    OrVariableNumber,
    OrVariableVariable,
    OrVariableFunction,
    OrVariableChainAns,
    OrFunctionNumber,
    OrFunctionVariable,
    OrFunctionFunction,
    OrFunctionChainAns,
    OrChainAnsNumber,
    OrChainAnsVariable,
    OrChainAnsFunction,
    OperatorError,
    OrChainPushNumber,
    OrChainPushVariable,
    OrChainPushFunction,
    OrChainPushChainAns,
    
    OperatorError,
    EQNumberVariable,
    EQNumberFunction,
    EQNumberChainAns,
    EQVariableNumber,
    EQVariableVariable,
    EQVariableFunction,
    EQVariableChainAns,
    EQFunctionNumber,
    EQFunctionVariable,
    EQFunctionFunction,
    EQFunctionChainAns,
    EQChainAnsNumber,
    EQChainAnsVariable,
    EQChainAnsFunction,
    OperatorError,
    EQChainPushNumber,
    EQChainPushVariable,
    EQChainPushFunction,
    EQChainPushChainAns,
    
    OperatorError,
    LTNumberVariable,
    LTNumberFunction,
    LTNumberChainAns,
    LTVariableNumber,
    LTVariableVariable,
    LTVariableFunction,
    LTVariableChainAns,
    LTFunctionNumber,
    LTFunctionVariable,
    LTFunctionFunction,
    LTFunctionChainAns,
    LTChainAnsNumber,
    LTChainAnsVariable,
    LTChainAnsFunction,
    OperatorError,
    LTChainPushNumber,
    LTChainPushVariable,
    LTChainPushFunction,
    LTChainPushChainAns,
    
    OperatorError,
    GTNumberVariable,
    GTNumberFunction,
    GTNumberChainAns,
    GTVariableNumber,
    GTVariableVariable,
    GTVariableFunction,
    GTVariableChainAns,
    GTFunctionNumber,
    GTFunctionVariable,
    GTFunctionFunction,
    GTFunctionChainAns,
    GTChainAnsNumber,
    GTChainAnsVariable,
    GTChainAnsFunction,
    OperatorError,
    GTChainPushNumber,
    GTChainPushVariable,
    GTChainPushFunction,
    GTChainPushChainAns,
    
    OperatorError,
    LENumberVariable,
    LENumberFunction,
    LENumberChainAns,
    LEVariableNumber,
    LEVariableVariable,
    LEVariableFunction,
    LEVariableChainAns,
    LEFunctionNumber,
    LEFunctionVariable,
    LEFunctionFunction,
    LEFunctionChainAns,
    LEChainAnsNumber,
    LEChainAnsVariable,
    LEChainAnsFunction,
    OperatorError,
    LEChainPushNumber,
    LEChainPushVariable,
    LEChainPushFunction,
    LEChainPushChainAns,
    
    OperatorError,
    GENumberVariable,
    GENumberFunction,
    GENumberChainAns,
    GEVariableNumber,
    GEVariableVariable,
    GEVariableFunction,
    GEVariableChainAns,
    GEFunctionNumber,
    GEFunctionVariable,
    GEFunctionFunction,
    GEFunctionChainAns,
    GEChainAnsNumber,
    GEChainAnsVariable,
    GEChainAnsFunction,
    OperatorError,
    GEChainPushNumber,
    GEChainPushVariable,
    GEChainPushFunction,
    GEChainPushChainAns,
    
    OperatorError,
    NENumberVariable,
    NENumberFunction,
    NENumberChainAns,
    NEVariableNumber,
    NEVariableVariable,
    NEVariableFunction,
    NEVariableChainAns,
    NEFunctionNumber,
    NEFunctionVariable,
    NEFunctionFunction,
    NEFunctionChainAns,
    NEChainAnsNumber,
    NEChainAnsVariable,
    NEChainAnsFunction,
    OperatorError,
    NEChainPushNumber,
    NEChainPushVariable,
    NEChainPushFunction,
    NEChainPushChainAns,
    
    OperatorError,
    MulNumberVariable,
    MulNumberFunction,
    MulNumberChainAns,
    MulVariableNumber,
    MulVariableVariable,
    MulVariableFunction,
    MulVariableChainAns,
    MulFunctionNumber,
    MulFunctionVariable,
    MulFunctionFunction,
    MulFunctionChainAns,
    MulChainAnsNumber,
    MulChainAnsVariable,
    MulChainAnsFunction,
    OperatorError,
    MulChainPushNumber,
    MulChainPushVariable,
    MulChainPushFunction,
    MulChainPushChainAns,
    
    OperatorError,
    DivNumberVariable,
    DivNumberFunction,
    DivNumberChainAns,
    DivVariableNumber,
    DivVariableVariable,
    DivVariableFunction,
    DivVariableChainAns,
    DivFunctionNumber,
    DivFunctionVariable,
    DivFunctionFunction,
    DivFunctionChainAns,
    DivChainAnsNumber,
    DivChainAnsVariable,
    DivChainAnsFunction,
    OperatorError,
    DivChainPushNumber,
    DivChainPushVariable,
    DivChainPushFunction,
    DivChainPushChainAns,
    
    OperatorError,
    AddNumberVariable,
    AddNumberFunction,
    AddNumberChainAns,
    AddVariableNumber,
    AddVariableVariable,
    AddVariableFunction,
    AddVariableChainAns,
    AddFunctionNumber,
    AddFunctionVariable,
    AddFunctionFunction,
    AddFunctionChainAns,
    AddChainAnsNumber,
    AddChainAnsVariable,
    AddChainAnsFunction,
    OperatorError,
    AddChainPushNumber,
    AddChainPushVariable,
    AddChainPushFunction,
    AddChainPushChainAns,
    
    OperatorError,
    SubNumberVariable,
    SubNumberFunction,
    SubNumberChainAns,
    SubVariableNumber,
    SubVariableVariable,
    SubVariableFunction,
    SubVariableChainAns,
    SubFunctionNumber,
    SubFunctionVariable,
    SubFunctionFunction,
    SubFunctionChainAns,
    SubChainAnsNumber,
    SubChainAnsVariable,
    SubChainAnsFunction,
    OperatorError,
    SubChainPushNumber,
    SubChainPushVariable,
    SubChainPushFunction,
    SubChainPushChainAns,
};

