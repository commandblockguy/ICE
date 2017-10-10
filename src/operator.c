#include "defines.h"
#include "parse.h"
#include "operator.h"

#include "main.h"
#include "functions.h"
#include "errors.h"
#include "stack.h"
#include "output.h"
#include "routines.h"

#ifdef COMPUTER_ICE
#define INCBIN_PREFIX
#include "incbin.h"
INCBIN(And, "src/asm/and.bin");
INCBIN(Or, "src/asm/or.bin");
INCBIN(Xor, "src/asm/xor.bin");
INCBIN(Rand, "src/asm/rand.bin");
INCBIN(Keypad, "src/asm/keypad.bin");
#endif

extern void (*operatorFunctions[272])(void);
extern void (*operatorChainPushChainAnsFunctions[17])(void);
const char operators[]              = {tStore, tDotIcon, tCrossIcon, tBoxIcon, tAnd, tXor, tOr, tEQ, tLT, tGT, tLE, tGE, tNE, tMul, tDiv, tAdd, tSub};
const uint8_t operatorPrecedence[]  = {0, 6, 8, 8, 2, 1, 1, 3, 3, 3, 3, 3, 3, 5, 5, 4, 4};
const uint8_t operatorPrecedence2[] = {9, 6, 8, 8, 2, 1, 1, 3, 3, 3, 3, 3, 3, 5, 5, 4, 4};
const uint8_t operatorCanSwap[]     = {0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 0, 1, 0}; // Used for operators which can swap the operands, i.e. A*B = B*A

static element_t *entry0;
static element_t *entry1;
static element_t *entry2;
static uint24_t entry0_operand;
static uint24_t entry1_operand;
static uint24_t entry2_operand;
static uint8_t oper;

#ifdef COMPUTER_ICE
static uint8_t clz(uint24_t x) {
    uint8_t n = 0;
    if (!x) {
        return 24;
    }
    while (!(x & (1 << 23))) {
        n++;
        x <<= 1;
    }
    return n;
}

void MultWithNumber(uint24_t num, uint8_t *programPtr, bool ChangeRegisters) {
    (void)programPtr;
    uint24_t bit;
    uint8_t po2 = !(num & (num - 1));
    
    if (24 - clz(num) + __builtin_popcount(num) - 2 * po2 < 10) {
        if(!po2) {
            if (!ChangeRegisters) {
                PUSH_HL();
                POP_DE();
            } else {
                PUSH_DE();
                POP_HL();
            }
        }
        for (bit = 1 << (22 - clz(num)); bit; bit >>= 1) {
            ADD_HL_HL();
            if(num & bit) {
                ADD_HL_DE();
            }
        }
    } else if (num < 0x100) {
        if (ChangeRegisters) {
            EX_DE_HL();
        }
        LD_A(num);
        CALL(__imul_b);
    } else {
        if (ChangeRegisters) {
            EX_DE_HL();
        }
        LD_BC_IMM(num);
        CALL(__imuls);
    }
}
#endif

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
        case tDotIcon:
            return operand1 & operand2;
        case tCrossIcon:
            return operand1 | operand2;
        case tBoxIcon:
            return operand1 ^ operand2;
        default:
            return operand1 && operand2;
    }
}

static void getEntryOperands() {
    entry0_operand = entry0->operand;
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

uint8_t parseOperator(element_t *outputPrevPrevPrev, element_t *outputPrevPrev, element_t *outputPrev, element_t *outputCurr) {
    uint8_t type1 = outputPrevPrev->type;
    uint8_t type1Masked = type1 & 0x7F;
    uint8_t type2 = outputPrev->type;
    
    oper = outputCurr->operand;
    
    // Store to a pointer
    if (oper == tStore && type2 == TYPE_FUNCTION) {
        type2 = TYPE_CHAIN_ANS;
        type1 = outputPrevPrevPrev->type;
        type1Masked = type1 & 0x7F;
    }
    
    // Get the right arguments
    entry0 = outputPrevPrevPrev;
    entry1 = outputPrevPrev;
    entry2 = outputPrev;
    getEntryOperands();
    
    expr.outputReturnRegister = OUTPUT_IN_HL;
    expr.AnsSetZeroFlag = expr.AnsSetZeroFlagReversed = expr.AnsSetCarryFlag = expr.AnsSetCarryFlagReversed = false;
    
    if (type1 >= TYPE_STRING && type2 == TYPE_OS_STRING && oper == tStore) {
        StoStringString();
    } else if (type1 >= TYPE_STRING && type2 >= TYPE_STRING && oper == tAdd) {
        AddStringString();
    } else {
        // Only call the function if both types are valid
        if ((type1Masked == type2 && (type1Masked == TYPE_NUMBER || type1Masked == TYPE_CHAIN_ANS)) ||
            (oper == tStore && (type2 != TYPE_VARIABLE  && !(type2 == TYPE_FUNCTION && outputPrev->operand == 0x010108))) ||
            (type2 == TYPE_CHAIN_PUSH)
        ) {
            return E_SYNTAX;
        }
        
        if (type1Masked == TYPE_CHAIN_PUSH) {
            if (type2 != TYPE_CHAIN_ANS) {
                return E_ICE_ERROR;
            }
            // Call the right CHAIN_PUSH | CHAIN_ANS function
            (*operatorChainPushChainAnsFunctions[getIndexOfOperator(oper) - 1])();
        } else {
            // If you have something like "A or 1", the output is always 1, so we can remove the "ld hl, (A)"
            ice.programPtrBackup = ice.programPtr;
            ice.dataOffsetElementsBackup = ice.dataOffsetElements;

            // Swap operands for compiler optimizations
            if (oper == tLE || oper == tLT ||
                 (operatorCanSwap[getIndexOfOperator(oper) - 1] && 
                   (type1Masked == TYPE_NUMBER || type2 == TYPE_CHAIN_ANS || 
                     (type1Masked == TYPE_VARIABLE && type2 == TYPE_FUNCTION_RETURN)
                   )
                 )
               ) {
                uint8_t temp = type1Masked;
                
                type1Masked = type2;
                type2 = temp;
                swapEntries();
                if (oper == tLE) {
                    oper = tGE;
                } else if (oper == tLT) {
                    oper = tGT;
                }
            }
            
            // Call the right function!
            (*operatorFunctions[((getIndexOfOperator(oper) - 1) * 16) + (type1Masked * 4) + type2])();
        }
        
        // If the operator is /, the routine always ends with call __idvrmu \ expr.outputReturnRegister == OUTPUT_IN_DE
        if (oper == tDiv && !(expr.outputRegister == OUTPUT_IN_A && entry2_operand == 1)) {
            CALL(__idvrmu);
            expr.outputReturnRegister = OUTPUT_IN_DE;
        }
        
        // If the operator is *, and both operands not a number, it always ends with call __imuls
        if (oper == tMul && type1Masked != TYPE_NUMBER && type2 != TYPE_NUMBER && !(expr.outputRegister == OUTPUT_IN_A && entry2_operand < 256)) {
            CALL(__imuls);
        }
        
        if (expr.outputRegister != OUTPUT_IN_A && !(type2 == TYPE_NUMBER && entry2_operand < 256)) {
            if (oper == tDotIcon) {
                CALL(__iand);
            } else if (oper == tBoxIcon) {
                CALL(__ixor);
            } else if (oper == tCrossIcon) {
                CALL(__ior);
            }
        }
        
        // If the operator is -, and the second operand not a number, it always ends with or a, a \ sbc hl, de
        if (oper == tSub && type2 != TYPE_NUMBER) {
            OR_A_SBC_HL_DE();
        }
    }
    
    expr.outputRegister = expr.outputReturnRegister;
    return VALID;
}

void insertFunctionReturnNoPush(uint24_t function, uint8_t outputRegister) {
    insertFunctionReturn(function, outputRegister, NO_PUSH);
}

void insertFunctionReturnEntry1HLNoPush(void) {
    insertFunctionReturnNoPush(entry1_operand, OUTPUT_IN_HL);
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
        ProgramPtrToOffsetStack();
        
        // We need to add the rand routine to the data section
        if (!ice.usedAlreadyRand) {
            ice.randAddr = (uintptr_t)ice.programDataPtr;
            memcpy(ice.programDataPtr, RandData, SIZEOF_RAND_DATA);
            ice.programDataPtr += SIZEOF_RAND_DATA;
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
    }
    
    else {
        // Check if the getKey has a fast direct key argument; if so, the second byte is 1
        if ((uint8_t)(function >> 8)) {
            uint8_t key = function >> 8;
            uint8_t keyBit = 1;
            /* This is the same as 
                ((key-1)/8 & 7) * 2 = 
                (key-1)/4 & (7*2) = 
                (key-1) >> 2 & 14 
            */
            LD_B(0x1E - (((key - 1) >> 2) & 14));
            
            // Get the right bit for the keypress
            if ((key - 1) & 7) {
                uint8_t a;
                
                for (a = 0; a < ((key - 1) & 7); a++) {
                    keyBit = keyBit << 1;
                }
            }
            
            LD_C(keyBit);
            
            // Check if we need to preserve HL
            if (NEED_PUSH && outputRegister != OUTPUT_IN_HL) {
                PUSH_HL();
            }
            
            
            // Store the pointer to the call to the stack, to replace later
            ProgramPtrToOffsetStack();
            
            // We need to add the getKeyFast routine to the data section
            if (!ice.usedAlreadyGetKeyFast) {
                ice.getKeyFastAddr = (uintptr_t)ice.programDataPtr;
                memcpy(ice.programDataPtr, KeypadData, SIZEOF_KEYPAD_DATA);
                ice.programDataPtr += SIZEOF_KEYPAD_DATA;
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
            
            // This routine sets or resets the zero flag, which can be used to optimize conditions
            expr.AnsSetZeroFlag = true;
            expr.ZeroCarryFlagRemoveAmountOfBytes = 0;
        }
        
        // Else, a standalone "getKey"
        else {
            // The key should be in HL
            if (outputRegister == OUTPUT_IN_HL) {
                CALL(_os_GetCSC);
                ice.modifiedIY = false;
            }
            
            // The key should be in BC or DE
            else {
                if (needPush) {
                    PUSH_HL();
                }
                CALL(_GetCSC);
                if (needPush) {
                    POP_HL();
                }
                
                if (outputRegister == OUTPUT_IN_DE) {
                    LD_DE_IMM(0);
                    LD_E_A();
                } else if (outputRegister == OUTPUT_IN_BC) {
                    LD_BC_IMM(0);
                    LD_C_A();
                }
            }
        }
    }
}

void LD_HL_NUMBER(uint24_t number) {
    if (!number) {
        OR_A_A();
        SBC_HL_HL();
        expr.AnsSetZeroFlag = true;
        expr.ZeroCarryFlagRemoveAmountOfBytes = 0;
    } else {
        LD_HL_IMM(number);
    }
}

void LD_HL_STRING(uint24_t stringPtr) {
    if (stringPtr != ice.tempStrings[TempString1] && stringPtr != ice.tempStrings[TempString2]) {
        ProgramPtrToOffsetStack();
    }
    LD_HL_IMM(stringPtr);
}

void OperatorError(void) {
    // This *should* never be triggered
    displayError(E_ICE_ERROR);
}
void StoChainAnsVariable(void) {
    MaybeAToHL();
    if (expr.outputRegister == OUTPUT_IN_HL) {
        LD_IX_OFF_IND_HL(entry2_operand);
    } else {
        LD_IX_OFF_IND_DE(entry2_operand);
    }
}
void StoNumberVariable(void) {
    LD_HL_NUMBER(entry1_operand);
    StoChainAnsVariable();
}
void StoVariableVariable(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    StoChainAnsVariable();
}
void StoNumberChainAns(void) {
    uint8_t type = entry1->type;
    uint8_t mask = entry2->mask;
    
    if (type == TYPE_NUMBER) {
        if (mask == TYPE_MASK_U8) {
            LD_A(entry0_operand);
            LD_ADDR_A(entry1_operand);
        } else if (mask == TYPE_MASK_U16) {
            LD_HL_NUMBER(entry1_operand);
        } else {
            LD_HL_NUMBER(entry0_operand);
            LD_ADDR_HL(entry1_operand);
        }
    } else if (type == TYPE_VARIABLE) {
        LD_HL_IND_IX_OFF(entry1_operand);
    } else if (type == TYPE_FUNCTION_RETURN) {
        insertFunctionReturnEntry1HLNoPush();
    } else {
        AnsToHL();
    }
    if (type != TYPE_NUMBER) {
        if (mask == TYPE_MASK_U8) {
            LD_A(entry0_operand);
            LD_HL_A();
        } else if (mask == TYPE_MASK_U24) {
            LD_DE_IMM(entry0_operand);
            LD_HL_DE();
            expr.outputReturnRegister = OUTPUT_IN_DE;
        }
    }
    if (mask == TYPE_MASK_U16) {
        LD_DE_IMM(entry0_operand);
    }
    StoToChainAns();
}
void StoVariableChainAns(void) {
    uint8_t type = entry1->type;
    uint8_t mask = entry2->mask;
    
    if (type == TYPE_NUMBER) {
        if (mask == TYPE_MASK_U8) {
            LD_A_IND_IX_OFF(entry0_operand);
            LD_ADDR_A(entry1_operand);
        } else if (mask == TYPE_MASK_U16) {
            LD_HL_IND_IX_OFF(entry1_operand);
        } else {
            LD_HL_IND_IX_OFF(entry0_operand);
            LD_ADDR_HL(entry1_operand);
        }
    } else if (type == TYPE_VARIABLE) {
        LD_HL_IND_IX_OFF(entry1_operand);
    } else if (type == TYPE_FUNCTION_RETURN) {
        insertFunctionReturnEntry1HLNoPush();
    } else {
        AnsToHL();
    }
    if (type != TYPE_NUMBER) {
        if (mask == TYPE_MASK_U8) {
            LD_A_IND_IX_OFF(entry0_operand);
            LD_HL_A();
        } else if (mask == TYPE_MASK_U24) {
            LD_DE_IND_IX_OFF(entry0_operand);
            LD_HL_DE();
            expr.outputReturnRegister = OUTPUT_IN_DE;
        }
    }
    if (mask == TYPE_MASK_U16) {
        LD_DE_IND_IX_OFF(entry0_operand);
    }
    StoToChainAns();
}
void StoFunctionChainAns(void) {
    uint8_t type = entry1->type;
    uint8_t mask = entry2->mask;
    
    if (type == TYPE_NUMBER) {
        if (mask == TYPE_MASK_U8) {
            insertFunctionReturnNoPush(entry0_operand, OUTPUT_IN_HL);
            LD_A_L();
            LD_ADDR_A(entry1_operand);
        } else if (mask == TYPE_MASK_U16) {
            insertFunctionReturnNoPush(entry0_operand, OUTPUT_IN_DE);
            LD_HL_NUMBER(entry1_operand);
            StoToChainAns();
        } else {
            insertFunctionReturnNoPush(entry0_operand, OUTPUT_IN_HL);
            LD_ADDR_HL(entry1_operand);
        }
    } else if (type == TYPE_VARIABLE) {
        if (mask == TYPE_MASK_U8) {
            insertFunctionReturnNoPush(entry0_operand, OUTPUT_IN_HL);
            LD_A_L();
            LD_HL_IND_IX_OFF(entry1_operand);
            LD_HL_A();
        } else if (mask == TYPE_MASK_U16) {
            insertFunctionReturnNoPush(entry0_operand, OUTPUT_IN_DE);
            LD_HL_IND_IX_OFF(entry1_operand);
        } else {
            insertFunctionReturnNoPush(entry0_operand, OUTPUT_IN_DE);
            LD_HL_IND_IX_OFF(entry1_operand);
            LD_HL_DE();
            expr.outputRegister = OUTPUT_IN_DE;
        }
    } else if (type == TYPE_FUNCTION_RETURN) {
        insertFunctionReturnNoPush(entry1_operand, OUTPUT_IN_DE);
        if (mask == TYPE_MASK_U8) {
            LD_A_E();
            insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NEED_PUSH);
            LD_HL_A();
        } else if (mask == TYPE_MASK_U16) {
            insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NEED_PUSH);
        } else {
            insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NEED_PUSH);
            LD_HL_DE();
            expr.outputReturnRegister = OUTPUT_IN_DE;
        }
    } else {
        AnsToHL();
        insertFunctionReturn(entry0_operand, OUTPUT_IN_DE, NEED_PUSH);
        if (mask == TYPE_MASK_U8) {
            LD_A_E();
            LD_HL_A();
        } else if (mask == TYPE_MASK_U24) {
            LD_HL_DE();
            expr.outputReturnRegister = OUTPUT_IN_DE;
        }
    }
    if (type != TYPE_NUMBER) {
        StoToChainAns();
    }
}
void StoChainPushChainAns(void) {
    if (entry1->type == TYPE_CHAIN_ANS) {
        AnsToHL();
        POP_DE();
        if (entry2->mask == TYPE_MASK_U8) {
            LD_A_E();
            LD_HL_A();
        } else if (entry2->mask == TYPE_MASK_U24) {
            LD_HL_DE();
            expr.outputReturnRegister = OUTPUT_IN_DE;
        }
        StoToChainAns();
    }
}
void StoChainAnsChainAns(void) {
    uint8_t type = entry1->type;
    uint8_t mask = entry2->mask;
    
    if (type == TYPE_NUMBER) {
        if (mask == TYPE_MASK_U8) {
            if (expr.outputRegister == OUTPUT_IN_HL) {
                LD_A_L();
            } else if (expr.outputRegister == OUTPUT_IN_DE) {
                LD_A_E();
            }
            LD_ADDR_A(entry1_operand);
        } else if (mask == TYPE_MASK_U16) {
            AnsToDE();
            LD_HL_NUMBER(entry1_operand);
        } else {
            MaybeAToHL();
            if (expr.outputRegister == OUTPUT_IN_HL) {
                LD_ADDR_HL(entry1_operand);
            } else {
                LD_ADDR_DE(entry1_operand);
            }
            expr.outputReturnRegister = expr.outputRegister;
        }
    } else if (type == TYPE_VARIABLE) {
        if (mask == TYPE_MASK_U8) {
            if (expr.outputRegister == OUTPUT_IN_HL) {
                LD_A_L();
            } else if (expr.outputRegister == OUTPUT_IN_DE) {
                LD_A_E();
            }
            LD_HL_IND_IX_OFF(entry1_operand);
            LD_HL_A();
        } else {
            AnsToDE();
            LD_HL_IND_IX_OFF(entry1_operand);
            if (mask == TYPE_MASK_U24) {
                LD_HL_DE();
                expr.outputReturnRegister = OUTPUT_IN_DE;
            }
        }
    } else {
        // Chain Ans -> Function Return
        if (mask == TYPE_MASK_U8) {
            PushHLDE();
            insertFunctionReturnEntry1HLNoPush();
            POP_DE();
            LD_A_E();
            LD_HL_A();
        } else if (mask == TYPE_MASK_U16) {
            PushHLDE();
            insertFunctionReturnEntry1HLNoPush();
            POP_DE();
        } else {
            PushHLDE();
            insertFunctionReturnEntry1HLNoPush();
            POP_DE();
            LD_HL_DE();
            expr.outputReturnRegister = OUTPUT_IN_DE;
        }
    }
    StoToChainAns();
}
void StoToChainAns(void) {
    if (entry2->mask == TYPE_MASK_U8) {
        expr.outputReturnRegister = OUTPUT_IN_A;
    } else if (entry2->mask == TYPE_MASK_U16) {
        LD_HL_E();
        INC_HL();
        LD_HL_D();
        EX_S_DE_HL();
    }
}
void StoFunctionVariable(void) {
    insertFunctionReturnEntry1HLNoPush();
    StoChainAnsVariable();
}
void StoStringString(void) {
    LD_HL_STRING(entry1_operand);
    PUSH_HL();
    LD_HL_IMM(entry2_operand);
    PUSH_HL();
    CALL(__strcpy);
    POP_BC();
    POP_BC();
}
void BitAndChainAnsNumber(void) {
    if (expr.outputRegister == OUTPUT_IN_A) {
        if (oper == tDotIcon) {
            AND_A(entry2_operand);
        } else if (oper == tBoxIcon) {
            XOR_A(entry2_operand);
        } else {
            OR_A(entry2_operand);
        }
        expr.AnsSetZeroFlag = true;
        expr.outputReturnRegister = OUTPUT_IN_A;
        expr.ZeroCarryFlagRemoveAmountOfBytes = 0;
    } else {
        if (entry2_operand < 256) {
            if (expr.outputRegister == OUTPUT_IN_HL) {
                LD_A_L();
            } else {
                LD_A_E();
            }
            if (oper == tDotIcon) {
                AND_A(entry2_operand);
            } else if (oper == tBoxIcon) {
                XOR_A(entry2_operand);
            } else {
                OR_A(entry2_operand);
            }
            SBC_HL_HL();
            LD_L_A();
            expr.AnsSetZeroFlag = true;
            expr.ZeroCarryFlagRemoveAmountOfBytes = 3;
        } else {
            if (expr.outputRegister != OUTPUT_IN_HL) {
                EX_DE_HL();
            }
            LD_BC_IMM(entry2_operand);
        }
    }
}
void BitAndChainAnsVariable(void) {
    AnsToHL();
    LD_BC_IND_IX_OFF(entry2_operand);
}
void BitAndVariableNumber(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    BitAndChainAnsNumber();
}
void BitAndVariableVariable(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    BitAndChainAnsVariable();
}
void BitAndFunctionNumber(void) {
    insertFunctionReturnEntry1HLNoPush();
    BitAndChainAnsNumber();
}
void BitAndFunctionVariable(void) {
    insertFunctionReturnEntry1HLNoPush();
    BitAndChainAnsVariable();
}
void BitAndFunctionFunction(void) {
    insertFunctionReturnEntry1HLNoPush();
    insertFunctionReturn(entry2_operand, OUTPUT_IN_BC, NEED_PUSH);
}
void BitAndChainAnsFunction(void) {
    AnsToHL();
    insertFunctionReturn(entry2_operand, OUTPUT_IN_BC, NEED_PUSH);
}
void BitAndChainPushChainAns(void) {
    AnsToHL();
    POP_BC();
}

#define BitOrVariableNumber     BitAndVariableNumber
#define BitOrVariableVariable   BitAndVariableVariable
#define BitOrFunctionNumber     BitAndFunctionNumber
#define BitOrFunctionVariable   BitAndFunctionVariable
#define BitOrFunctionFunction   BitAndFunctionFunction
#define BitOrChainAnsNumber     BitAndChainAnsNumber
#define BitOrChainAnsVariable   BitAndChainAnsVariable
#define BitOrChainAnsFunction   BitAndChainAnsFunction
#define BitOrChainPushChainAns  BitAndChainPushChainAns

#define BitXorVariableNumber    BitAndVariableNumber
#define BitXorVariableVariable  BitAndVariableVariable
#define BitXorFunctionNumber    BitAndFunctionNumber
#define BitXorFunctionVariable  BitAndFunctionVariable
#define BitXorFunctionFunction  BitAndFunctionFunction
#define BitXorChainAnsNumber    BitAndChainAnsNumber
#define BitXorChainAnsVariable  BitAndChainAnsVariable
#define BitXorChainAnsFunction  BitAndChainAnsFunction
#define BitXorChainPushChainAns BitAndChainPushChainAns

void AndInsert(void) {
    if (oper == tOr) {
        memcpy(ice.programPtr, OrData, SIZEOF_OR_DATA);
        ice.programPtr += SIZEOF_OR_DATA;
    } else if (oper == tAnd) {
        memcpy(ice.programPtr, AndData, SIZEOF_AND_DATA);
        ice.programPtr += SIZEOF_AND_DATA;
    } else {
        memcpy(ice.programPtr, XorData, SIZEOF_XOR_DATA);
        ice.programPtr += SIZEOF_XOR_DATA;
    }
    expr.AnsSetCarryFlag = true;
    expr.ZeroCarryFlagRemoveAmountOfBytes = 3;
}
void AndChainAnsNumber(void) {
    if (expr.outputRegister == OUTPUT_IN_A && entry2_operand < 256) {
        expr.outputReturnRegister = OUTPUT_IN_A;
        if (oper == tXor) {
            if (entry2_operand) {
                ADD_A(255);
                SBC_A_A();
                INC_A();
            } else {
                goto NumberNotZero1;
            }
        } else if (oper == tAnd) {
            if (entry2_operand) {
                goto NumberNotZero1;
            } else {
                LD_HL_NUMBER(0);
                expr.outputReturnRegister = OUTPUT_IN_HL;
            }
        } else {
            if (!entry2_operand) {
NumberNotZero1:
                SUB_A(1);
                SBC_A_A();
                INC_A();
            } else {
                ice.programPtr = ice.programPtrBackup;
                ice.dataOffsetElements = ice.dataOffsetElementsBackup;
                LD_HL_IMM(1);
                expr.outputReturnRegister = OUTPUT_IN_HL;
            }
        }
    } else {
        MaybeAToHL();
        if (oper == tXor) {
            if (expr.outputRegister == OUTPUT_IN_HL) {
                LD_DE_IMM(-1);
            } else {
                LD_HL_IMM(-1);
            }
            ADD_HL_DE();
            if (!entry2_operand) {
                CCF();
                expr.AnsSetCarryFlagReversed = true;
            } else {
                expr.AnsSetCarryFlag = true;
            }
            SBC_HL_HL_INC_HL();
            expr.ZeroCarryFlagRemoveAmountOfBytes = 3 + !entry2_operand;
        } else if (oper == tAnd) {
            if (!entry2_operand) {
                ice.programPtr = ice.programPtrBackup;
                ice.dataOffsetElements = ice.dataOffsetElementsBackup;
                LD_HL_NUMBER(0);
            } else {
                goto numberNotZero2;
            }
        } else {
            if (!entry2_operand) {
numberNotZero2:
                MaybeAToHL();
                if (expr.outputRegister == OUTPUT_IN_HL) {
                    LD_DE_IMM(-1);
                } else {
                    LD_HL_IMM(-1);
                }
                ADD_HL_DE();
                CCF();
                SBC_HL_HL_INC_HL();
                expr.ZeroCarryFlagRemoveAmountOfBytes = 4;
                expr.AnsSetCarryFlagReversed = true;
            } else {
                ice.programPtr = ice.programPtrBackup;
                ice.dataOffsetElements = ice.dataOffsetElementsBackup;
                LD_HL_IMM(1);
            }
        }
    }
}
void AndChainAnsVariable(void) {
    MaybeAToHL();
    if (expr.outputRegister == OUTPUT_IN_HL) {
        LD_DE_IND_IX_OFF(entry2_operand);
    } else {
        LD_HL_IND_IX_OFF(entry2_operand);
    }
    AndInsert();
}
void AndChainAnsFunction(void) {
    MaybeAToHL();
    insertFunctionReturn(entry2_operand, (expr.outputRegister == OUTPUT_IN_HL) ? OUTPUT_IN_DE : OUTPUT_IN_HL, NEED_PUSH);
    AndInsert();
}
void AndFunctionNumber(void) {
    insertFunctionReturnEntry1HLNoPush();
    AndChainAnsNumber();
}
void AndVariableNumber(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    AndChainAnsNumber();
}
void AndFunctionVariable(void) {
    insertFunctionReturnEntry1HLNoPush();
    AndChainAnsVariable();
}
void AndVariableVariable(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    AndChainAnsVariable();
}
void AndFunctionFunction(void) {
    insertFunctionReturnNoPush(entry1_operand, OUTPUT_IN_DE);
    insertFunctionReturn(entry2_operand, OUTPUT_IN_HL, NEED_PUSH);
    AndInsert();
}
void AndChainPushChainAns(void) {
    MaybeAToHL();
    if (expr.outputRegister == OUTPUT_IN_HL) {
        POP_DE();
    } else {
        POP_HL();
    }
    AndInsert();
}

#define XorVariableNumber    AndVariableNumber
#define XorVariableVariable  AndVariableVariable
#define XorVariableFunction  AndVariableFunction
#define XorFunctionNumber    AndFunctionNumber
#define XorFunctionVariable  AndFunctionVariable
#define XorFunctionFunction  AndFunctionFunction
#define XorChainAnsNumber    AndChainAnsNumber
#define XorChainAnsVariable  AndChainAnsVariable
#define XorChainAnsFunction  AndChainAnsFunction
#define XorChainPushChainAns AndChainPushChainAns

#define OrVariableNumber     AndVariableNumber
#define OrVariableVariable   AndVariableVariable
#define OrVariableFunction   AndVariableFunction
#define OrFunctionNumber     AndFunctionNumber
#define OrFunctionVariable   AndFunctionVariable
#define OrFunctionFunction   AndFunctionFunction
#define OrChainAnsNumber     AndChainAnsNumber
#define OrChainAnsVariable   AndChainAnsVariable
#define OrChainAnsFunction   AndChainAnsFunction
#define OrChainPushChainAns  AndChainPushChainAns

void EQInsert() {
    OR_A_SBC_HL_DE();
    LD_HL_IMM(0);
    if (oper == tEQ) {
        JR_NZ(1);
        expr.AnsSetZeroFlagReversed = true;
    } else {
        JR_Z(1);
    }
    INC_HL();
    expr.AnsSetZeroFlag = true;
    expr.ZeroCarryFlagRemoveAmountOfBytes = 7;
}
void EQChainAnsNumber(void) {
    uint24_t number = entry2_operand;
    
    if (expr.outputRegister == OUTPUT_IN_A && entry2_operand < 256) {
        if (oper == tNE) {
            ADD_A(255 - entry2_operand);
            ADD_A(1);
        } else {
            SUB_A(entry2_operand);
            ADD_A(255);
            
        }
        SBC_A_A();
        INC_A();
        expr.AnsSetCarryFlag = true;
        expr.outputReturnRegister = OUTPUT_IN_A;
        expr.ZeroCarryFlagRemoveAmountOfBytes = 2;
    } else {
        MaybeAToHL();
        if (number && number < 6) {
            do {
                if (expr.outputRegister == OUTPUT_IN_HL) {
                    DEC_HL();
                } else {
                    DEC_DE();
                }
            } while (--number);
        }
        if (!number) {
            if (expr.outputRegister == OUTPUT_IN_HL) {
                LD_DE_IMM(-1);
            } else {
                LD_HL_IMM(-1);
            }
            ADD_HL_DE();
            expr.ZeroCarryFlagRemoveAmountOfBytes = 0;
            if (oper == tNE) {
                CCF();
                expr.ZeroCarryFlagRemoveAmountOfBytes++;
                expr.AnsSetCarryFlagReversed = true;
            } else {
                expr.AnsSetCarryFlag = true;
            }
            SBC_HL_HL_INC_HL();
            expr.ZeroCarryFlagRemoveAmountOfBytes += 3;
        } else {
            if (expr.outputRegister == OUTPUT_IN_HL) {
                LD_DE_IMM(number);
            } else {
                LD_HL_IMM(number);
            }
            EQInsert();
        }
    }
}
void EQChainAnsFunction(void) {
    MaybeAToHL();
    insertFunctionReturn(entry2_operand, (expr.outputRegister == OUTPUT_IN_HL) ? OUTPUT_IN_DE : OUTPUT_IN_HL, NEED_PUSH);
    EQInsert();
}
void EQVariableNumber(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    EQChainAnsNumber();
}
void EQChainAnsVariable(void) {
    MaybeAToHL();
    if (expr.outputRegister == OUTPUT_IN_HL) {
        LD_DE_IND_IX_OFF(entry2_operand);
    } else {
        LD_HL_IND_IX_OFF(entry2_operand);
    }
    EQInsert();
}
void EQFunctionNumber(void) {
    insertFunctionReturnEntry1HLNoPush();
    EQChainAnsNumber();
}
void EQFunctionVariable(void) {
    insertFunctionReturnEntry1HLNoPush();
    EQChainAnsVariable();
}
void EQVariableVariable(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    EQChainAnsVariable();
}
void EQFunctionFunction(void) {
    insertFunctionReturnNoPush(entry1_operand, OUTPUT_IN_DE);
    insertFunctionReturn(entry2_operand, OUTPUT_IN_HL, NEED_PUSH);
    EQInsert();
}
void EQChainPushChainAns(void) {
    MaybeAToHL();
    if (expr.outputRegister == OUTPUT_IN_HL) {
        POP_DE();
    } else {
        POP_HL();
    }
    EQInsert();
}
void GEInsert() {
    if (oper == tGE || oper == tLE) {
        OR_A_A();
    } else {
        SCF();
    }
    SBC_HL_DE();
    SBC_HL_HL_INC_HL();
    expr.AnsSetCarryFlag = true;
    expr.ZeroCarryFlagRemoveAmountOfBytes = 3;
}
void GEChainAnsNumber(void) {
    if (entry2_operand > 255) {
        MaybeAToHL();
    }
    if (expr.outputRegister == OUTPUT_IN_HL) {
        LD_DE_IMM(entry2_operand);
        GEInsert();
    } else if (expr.outputRegister == OUTPUT_IN_DE) {
        LD_HL_IMM(entry2_operand);
        GEInsert();
    } else {
        SUB_A(entry2_operand + (oper == tGT || oper == tLT));
        SBC_A_A();
        INC_A();
        expr.AnsSetCarryFlag = true;
        expr.outputReturnRegister = OUTPUT_IN_A;
        expr.ZeroCarryFlagRemoveAmountOfBytes = 2;
    }
}
void GEChainAnsVariable(void) {
    MaybeAToHL();
    if (expr.outputRegister == OUTPUT_IN_HL) {
        LD_DE_IND_IX_OFF(entry2_operand);
    } else {
        LD_HL_IND_IX_OFF(entry2_operand);
    }
    GEInsert();
}
void GENumberVariable(void) {
    LD_HL_NUMBER(entry1_operand);
    GEChainAnsVariable();
}
void GENumberFunction(void) {
    insertFunctionReturnNoPush(entry2_operand, OUTPUT_IN_DE);
    LD_HL_NUMBER(entry1_operand);
    GEInsert();
}
void GENumberChainAns(void) {
    if (entry1_operand > 255 || !entry1_operand) {
        MaybeAToHL();
    }
    if (expr.outputRegister == OUTPUT_IN_HL) {
        LD_DE_IMM(entry1_operand);
        GEInsert();
    } else if (expr.outputRegister == OUTPUT_IN_DE) {
        LD_HL_NUMBER(entry1_operand);
        GEInsert();
    } else {
        ADD_A(256 - entry1_operand - (oper == tGE || oper == tLE));
        SBC_A_A();
        INC_A();
        expr.AnsSetCarryFlag = true;
        expr.outputReturnRegister = OUTPUT_IN_A;
        expr.ZeroCarryFlagRemoveAmountOfBytes = 2;
    }
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
    insertFunctionReturnNoPush(entry2_operand, OUTPUT_IN_DE);
    LD_HL_IND_IX_OFF(entry1_operand);
    GEInsert();
}
void GEVariableChainAns(void) {
    AnsToDE();
    LD_HL_IND_IX_OFF(entry1_operand);
    GEInsert();
}
void GEFunctionNumber(void) {
    insertFunctionReturnEntry1HLNoPush();
    GEChainAnsNumber();
}
void GEFunctionVariable(void) {
    insertFunctionReturnEntry1HLNoPush();
    GEChainAnsVariable();
}
void GEFunctionFunction(void) {
    insertFunctionReturnNoPush(entry2_operand, OUTPUT_IN_DE);
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NEED_PUSH);
    GEInsert();
}
void GEFunctionChainAns(void) {
    AnsToDE();
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NEED_PUSH);
}
void GEChainAnsFunction(void) {
    AnsToHL();
    insertFunctionReturn(entry2_operand, OUTPUT_IN_DE, NEED_PUSH);
    GEInsert();
}
void GEChainPushChainAns(void) {
    AnsToDE();
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

void LTChainPushChainAns(void) {
    AnsToHL();
    POP_DE();
    SCF();
    SBC_HL_DE();
    SBC_HL_HL_INC_HL();
    expr.AnsSetCarryFlag = true;
    expr.ZeroCarryFlagRemoveAmountOfBytes = 3;
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

void LEChainPushChainAns(void) {
    AnsToHL();
    POP_DE();
    OR_A_SBC_HL_DE();
    SBC_HL_HL_INC_HL();
    expr.AnsSetCarryFlag = true;
    expr.ZeroCarryFlagRemoveAmountOfBytes = 3;
}

#define NEVariableNumber    EQVariableNumber
#define NEVariableVariable  EQVariableVariable
#define NEFunctionNumber    EQFunctionNumber
#define NEFunctionVariable  EQFunctionVariable
#define NEFunctionFunction  EQFunctionFunction
#define NEChainAnsNumber    EQChainAnsNumber
#define NEChainAnsVariable  EQChainAnsVariable
#define NEChainAnsFunction  EQChainAnsFunction
#define NEChainPushChainAns EQChainPushChainAns

void MulChainAnsNumber(void) {
    uint24_t number = entry2_operand;
    
    if (expr.outputRegister == OUTPUT_IN_A && entry2_operand < 256) {
        LD_L_A();
        LD_H(entry2_operand);
        MLT_HL();
    } else {
        MaybeAToHL();
        if (!number) {
            ice.programPtr = ice.programPtrBackup;
            ice.dataOffsetElements = ice.dataOffsetElementsBackup;
            LD_HL_NUMBER(0);
        } else {
            MultWithNumber(number, (uint8_t*)&ice.programPtr, 16*expr.outputRegister);
        }
    }
}
void MulVariableNumber(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    MulChainAnsNumber();
}
void MulFunctionNumber(void) {
    insertFunctionReturnEntry1HLNoPush();
    MulChainAnsNumber();
}
void MulChainAnsVariable(void) {
    AnsToHL();
    LD_BC_IND_IX_OFF(entry2_operand);
}
void MulFunctionVariable(void) {
    insertFunctionReturnEntry1HLNoPush();
    MulChainAnsVariable();
}
void MulVariableVariable(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    MulChainAnsVariable();
}
void MulFunctionFunction(void) {
    insertFunctionReturnEntry1HLNoPush();
    PUSH_HL();
    insertFunctionReturnNoPush(entry2_operand, OUTPUT_IN_HL);
    POP_BC();
}
void MulChainAnsFunction(void) {
    AnsToHL();
    insertFunctionReturn(entry2_operand, OUTPUT_IN_BC, NEED_PUSH);
}
void MulChainPushChainAns(void) {
    AnsToHL();
    POP_BC();
}
void DivChainAnsNumber(void) {
    if (expr.outputRegister == OUTPUT_IN_A && entry2_operand <= 64 && !(entry2_operand & (entry2_operand - 1))) {
        while (entry2_operand != 1) {
            SRL_A();
            entry2_operand /= 2;
        }
        expr.outputReturnRegister = OUTPUT_IN_A;
    } else {
        AnsToHL();
        LD_BC_IMM(entry2_operand);
    }
}
void DivChainAnsVariable(void) {
    AnsToHL();
    LD_BC_IND_IX_OFF(entry2_operand);
}
void DivNumberVariable(void) {
    LD_HL_NUMBER(entry1_operand);
    DivChainAnsVariable();
}
void DivNumberFunction(void) {
    insertFunctionReturnNoPush(entry2_operand, OUTPUT_IN_BC);
    LD_HL_NUMBER(entry1_operand);
}
void DivNumberChainAns(void) {
    PushHLDE();
    POP_BC();
    LD_HL_NUMBER(entry1_operand);
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
    insertFunctionReturnNoPush(entry2_operand, OUTPUT_IN_BC);
    LD_HL_IND_IX_OFF(entry1_operand);
}
void DivVariableChainAns(void) {
    PushHLDE();
    POP_BC();
    LD_HL_IND_IX_OFF(entry1_operand);
}
void DivFunctionNumber(void) {
    insertFunctionReturnEntry1HLNoPush();
    DivChainAnsNumber();
}
void DivFunctionVariable(void) {
    insertFunctionReturnEntry1HLNoPush();
    DivChainAnsVariable();
}
void DivFunctionFunction(void) {
    insertFunctionReturnNoPush(entry2_operand, OUTPUT_IN_HL);
    PUSH_HL();
    insertFunctionReturnEntry1HLNoPush();
    POP_BC();
}
void DivFunctionChainAns(void) {
    PushHLDE();
    insertFunctionReturnEntry1HLNoPush();
    POP_BC();
}
void DivChainAnsFunction(void) {
    AnsToHL();
    insertFunctionReturn(entry2_operand, OUTPUT_IN_BC, NEED_PUSH);
}
void DivChainPushChainAns(void) {
    PushHLDE();
    POP_BC();
    POP_HL();
}
void AddChainAnsNumber(void) {
    uint24_t number = entry2_operand;
    
    MaybeAToHL();
    if (number < 5) {
        uint8_t a;
        for (a = 0; a < (uint8_t)number; a++) {
            if (expr.outputRegister == OUTPUT_IN_HL) {
                INC_HL();
            } else {
                INC_DE();
            }
        }
    } else {
        if (expr.outputRegister == OUTPUT_IN_HL) {
            LD_DE_IMM(number);
        } else {
            LD_HL_IMM(number);
        }
        ADD_HL_DE();
    }
}
void AddVariableNumber(void) {
    if (entry2_operand < 128 && entry2_operand > 3) {
        LD_IY_IND_IX_OFF(entry1_operand);
        LEA_HL_IY_OFF(entry2_operand);
        ice.modifiedIY = true;
    } else {
        LD_HL_IND_IX_OFF(entry1_operand);
        AddChainAnsNumber();
    }
}
void AddFunctionNumber(void) {
    insertFunctionReturnEntry1HLNoPush();
    AddChainAnsNumber();
}
void AddChainAnsVariable(void) {
    MaybeAToHL();
    if (expr.outputRegister == OUTPUT_IN_HL) {
        LD_DE_IND_IX_OFF(entry2_operand);
    } else {
        LD_HL_IND_IX_OFF(entry2_operand);
    }
    ADD_HL_DE();
}
void AddFunctionVariable(void) {
    insertFunctionReturnEntry1HLNoPush();
    AddChainAnsVariable();
}
void AddVariableVariable(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    if (entry1_operand == entry2_operand) {
        ADD_HL_HL();
    } else {
        AddChainAnsVariable();
    }
}
void AddFunctionFunction(void) {
    insertFunctionReturnNoPush(entry1_operand, OUTPUT_IN_DE);
    insertFunctionReturn(entry2_operand, OUTPUT_IN_HL, NEED_PUSH);
    ADD_HL_DE();
}
void AddChainAnsFunction(void) {
    MaybeAToHL();
    insertFunctionReturn(entry2_operand, (expr.outputRegister == OUTPUT_IN_HL) ? OUTPUT_IN_DE : OUTPUT_IN_HL, NEED_PUSH);
    ADD_HL_DE();
}
void AddChainPushChainAns(void) {
    MaybeAToHL();
    if (expr.outputRegister == OUTPUT_IN_HL) {
        POP_DE();
    } else {
        POP_HL();
    }
    ADD_HL_DE();
}
void AddStringString(void) {
    /*
        Cases:
            <string1>+<string2>             --> strcpy(<TempString1>, <string1>) strcat(<TempString1>, <string2>)
            <string1>+<TempString1>         --> strcpy(<TempString2>, <string1>) strcat(<TempString2>, <TempString1>)
            <string1>+<TempString2>         --> strcpy(<TempString1>, <string1>) strcat(<TempString1>, <TempString2>)
            <TempString1>+<string2>         --> strcat(<TempString1>, <string2>)
            <TempString1>+<TempString2>     --> strcat(<TempString1>, <TempString2>)
            <TempString2>+<string2>         --> strcat(<TempString2>, <string2>)
            <TempString2>+<TempString1>     --> strcat(<TempString2>, <TempString1>)
            
        Output in TempString2 if:
            <TempString2>+X
            X+<TempString1>
    */
    
    if (entry1->type == TYPE_STRING) {
        LD_HL_STRING(entry1_operand);
        PUSH_HL();
        if (entry2_operand == ice.tempStrings[TempString1]) {
            LD_HL_IMM(ice.tempStrings[TempString2]);
        } else {
            LD_HL_IMM(ice.tempStrings[TempString1]);
        }
        PUSH_HL();
        CALL(__strcpy);
        POP_DE();
        LD_HL_STRING(entry2_operand);
        EX_SP_HL();
        PUSH_DE();
        CALL(__strcat);
        POP_BC();
        POP_BC();
    } else {
        LD_HL_STRING(entry2_operand);
        PUSH_HL();
        LD_HL_IMM(entry1_operand);
        PUSH_HL();
        CALL(__strcat);
        POP_BC();
        POP_BC();
    }
}
void SubChainAnsNumber(void) {
    uint24_t number = entry2_operand;
    
    MaybeAToHL();
    if (number < 5) {
        uint8_t a;
        for (a = 0; a < (uint8_t)number; a++) {
            if (expr.outputRegister == OUTPUT_IN_HL) {
                DEC_HL();
            } else {
                DEC_DE();
            }
        }
    } else {
        if (expr.outputRegister == OUTPUT_IN_HL) {
            LD_DE_IMM(~number);
        } else {
            LD_HL_NUMBER(~number);
        }
        ADD_HL_DE();
    }
}
void SubChainAnsVariable(void) {
    AnsToHL();
    LD_DE_IND_IX_OFF(entry2_operand);
}
void SubNumberVariable(void) {
    LD_HL_NUMBER(entry1_operand);
    SubChainAnsVariable();
}
void SubNumberFunction(void) {
    insertFunctionReturnNoPush(entry2_operand, OUTPUT_IN_DE);
    LD_HL_NUMBER(entry1_operand);
}
void SubNumberChainAns(void) {
    AnsToDE();
    LD_HL_NUMBER(entry1_operand);
}
void SubVariableNumber(void) {
    if (entry2_operand < 129 && entry2_operand > 3) {
        LD_IY_IND_IX_OFF(entry1_operand);
        LEA_HL_IY_OFF(-entry2_operand);
        ice.modifiedIY = true;
    } else {
        LD_HL_IND_IX_OFF(entry1_operand);
        SubChainAnsNumber();
    }
}
void SubVariableVariable(void) {
    LD_HL_IND_IX_OFF(entry1_operand);
    SubChainAnsVariable();
}
void SubVariableFunction(void) {
    insertFunctionReturnNoPush(entry2_operand, OUTPUT_IN_DE);
    LD_HL_IND_IX_OFF(entry1_operand);
}
void SubVariableChainAns(void) {
    AnsToDE();
    LD_HL_IND_IX_OFF(entry1_operand);
}
void SubFunctionNumber(void) {
    insertFunctionReturnEntry1HLNoPush();
    SubChainAnsNumber();
}
void SubFunctionVariable(void) {
    insertFunctionReturnEntry1HLNoPush();
    SubChainAnsVariable();
}
void SubFunctionFunction(void) {
    insertFunctionReturnNoPush(entry2_operand, OUTPUT_IN_DE);
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NEED_PUSH);
}
void SubFunctionChainAns(void) {
    AnsToDE();
    insertFunctionReturn(entry1_operand, OUTPUT_IN_HL, NEED_PUSH);
}
void SubChainAnsFunction(void) {
    AnsToHL();
    insertFunctionReturn(entry2_operand, OUTPUT_IN_DE, NEED_PUSH);
}
void SubChainPushChainAns(void) {
    AnsToDE();
    POP_HL();
}

void (*operatorChainPushChainAnsFunctions[17])(void) = {
    StoChainPushChainAns,
    BitAndChainPushChainAns,
    BitOrChainPushChainAns,
    BitXorChainPushChainAns,
    AndChainPushChainAns,
    XorChainPushChainAns,
    OrChainPushChainAns,
    EQChainPushChainAns,
    LTChainPushChainAns,
    GTChainPushChainAns,
    LEChainPushChainAns,
    GEChainPushChainAns,
    NEChainPushChainAns,
    MulChainPushChainAns,
    DivChainPushChainAns,
    AddChainPushChainAns,
    SubChainPushChainAns
};

void (*operatorFunctions[272])(void) = {
    OperatorError,
    StoNumberVariable,
    OperatorError,
    StoNumberChainAns,
    OperatorError,
    StoVariableVariable,
    OperatorError,
    StoVariableChainAns,
    OperatorError,
    StoFunctionVariable,
    OperatorError,
    StoFunctionChainAns,
    OperatorError,
    StoChainAnsVariable,
    OperatorError,
    StoChainAnsChainAns,
    
    OperatorError,
    OperatorError,
    OperatorError,
    OperatorError,
    BitAndVariableNumber,
    BitAndVariableVariable,
    OperatorError,
    OperatorError,
    BitAndFunctionNumber,
    BitAndFunctionVariable,
    BitAndFunctionFunction,
    OperatorError,
    BitAndChainAnsNumber,
    BitAndChainAnsVariable,
    BitAndChainAnsFunction,
    OperatorError,
    
    OperatorError,
    OperatorError,
    OperatorError,
    OperatorError,
    BitOrVariableNumber,
    BitOrVariableVariable,
    OperatorError,
    OperatorError,
    BitOrFunctionNumber,
    BitOrFunctionVariable,
    BitOrFunctionFunction,
    OperatorError,
    BitOrChainAnsNumber,
    BitOrChainAnsVariable,
    BitOrChainAnsFunction,
    OperatorError,
    
    OperatorError,
    OperatorError,
    OperatorError,
    OperatorError,
    BitXorVariableNumber,
    BitXorVariableVariable,
    OperatorError,
    OperatorError,
    BitXorFunctionNumber,
    BitXorFunctionVariable,
    BitXorFunctionFunction,
    OperatorError,
    BitXorChainAnsNumber,
    BitXorChainAnsVariable,
    BitXorChainAnsFunction,
    OperatorError,
    
    OperatorError,
    OperatorError,
    OperatorError,
    OperatorError,
    AndVariableNumber,
    AndVariableVariable,
    OperatorError,
    OperatorError,
    AndFunctionNumber,
    AndFunctionVariable,
    AndFunctionFunction,
    OperatorError,
    AndChainAnsNumber,
    AndChainAnsVariable,
    AndChainAnsFunction,
    OperatorError,
    
    OperatorError,
    OperatorError,
    OperatorError,
    OperatorError,
    XorVariableNumber,
    XorVariableVariable,
    OperatorError,
    OperatorError,
    XorFunctionNumber,
    XorFunctionVariable,
    XorFunctionFunction,
    OperatorError,
    XorChainAnsNumber,
    XorChainAnsVariable,
    XorChainAnsFunction,
    OperatorError,
    
    OperatorError,
    OperatorError,
    OperatorError,
    OperatorError,
    OrVariableNumber,
    OrVariableVariable,
    OperatorError,
    OperatorError,
    OrFunctionNumber,
    OrFunctionVariable,
    OrFunctionFunction,
    OperatorError,
    OrChainAnsNumber,
    OrChainAnsVariable,
    OrChainAnsFunction,
    OperatorError,
    
    OperatorError,
    OperatorError,
    OperatorError,
    OperatorError,
    EQVariableNumber,
    EQVariableVariable,
    OperatorError,
    OperatorError,
    EQFunctionNumber,
    EQFunctionVariable,
    EQFunctionFunction,
    OperatorError,
    EQChainAnsNumber,
    EQChainAnsVariable,
    EQChainAnsFunction,
    OperatorError,
    
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
    
    OperatorError,
    OperatorError,
    OperatorError,
    OperatorError,
    NEVariableNumber,
    NEVariableVariable,
    OperatorError,
    OperatorError,
    NEFunctionNumber,
    NEFunctionVariable,
    NEFunctionFunction,
    OperatorError,
    NEChainAnsNumber,
    NEChainAnsVariable,
    NEChainAnsFunction,
    OperatorError,
    
    OperatorError,
    OperatorError,
    OperatorError,
    OperatorError,
    MulVariableNumber,
    MulVariableVariable,
    OperatorError,
    OperatorError,
    MulFunctionNumber,
    MulFunctionVariable,
    MulFunctionFunction,
    OperatorError,
    MulChainAnsNumber,
    MulChainAnsVariable,
    MulChainAnsFunction,
    OperatorError,
    
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
    
    OperatorError,
    OperatorError,
    OperatorError,
    OperatorError,
    AddVariableNumber,
    AddVariableVariable,
    OperatorError,
    OperatorError,
    AddFunctionNumber,
    AddFunctionVariable,
    AddFunctionFunction,
    OperatorError,
    AddChainAnsNumber,
    AddChainAnsVariable,
    AddChainAnsFunction,
    OperatorError,
    
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
};