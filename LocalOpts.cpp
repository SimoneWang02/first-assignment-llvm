//===-- LocalOpts.cpp - Example Transformations --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/LocalOpts.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IRBuilder.h"
#include <iostream>
#include <vector>

using namespace llvm;

bool checkOperands(Instruction &Instr) {
  // Controllo se entrambi sono variabili
  if (!isa<ConstantInt>(Instr.getOperand(0)) && !isa<ConstantInt>(Instr.getOperand(1)))
    return false;
  
  // Controllo se entrambi sono costanti
  if (isa<ConstantInt>(Instr.getOperand(0)) && isa<ConstantInt>(Instr.getOperand(1)))
    return false;

  return true;
}

bool algebraicIdentity(Instruction &Instr) {
  if (!Instr.isBinaryOp() || !checkOperands(Instr))
    return false;

  bool isFirstOperandConst = isa<Constant>(Instr.getOperand(0));

  APInt ConstValue = dyn_cast<ConstantInt>(isFirstOperandConst ? Instr.getOperand(0) : Instr.getOperand(1))->getValue();

  if (
    (Instr.getOpcode() == Instruction::Add && ConstValue == 0) ||
    (Instr.getOpcode() == Instruction::Sub && ConstValue == 0) ||
    (Instr.getOpcode() == Instruction::Mul && ConstValue == 1) ||
    (Instr.getOpcode() == Instruction::SDiv && ConstValue == 1)
  ) {
    Instr.replaceAllUsesWith(Instr.getOperand(isFirstOperandConst ? 1 : 0));

    return true;
  }

  return false;
}

bool strengthReduction(Instruction &Instr) {
  if (!Instr.isBinaryOp() || !checkOperands(Instr))
    return false;

  bool isMul = Instr.getOpcode() == Instruction::Mul;
    
  if (!isMul && Instr.getOpcode() != Instruction::SDiv)
    return false;

  ConstantInt *ConstOp = dyn_cast<ConstantInt>(Instr.getOperand(isa<ConstantInt>(Instr.getOperand(0)) ? 0 : 1));

  APInt ConstVal = ConstOp->getValue();
  int NearestLog = ConstVal.nearestLogBase2();
  APInt Rest = ConstVal - (1 << NearestLog);

  // Se il resto è |x| > 1, allora non è ottimizzabile
  if (Rest.abs().ugt(1))
    return false;

  // Se è una divisione e il resto è diverso da 0, allora non è ottimizzabile
  if (!isMul && Rest != 0)
    return false;

  ConstantInt *ConstLog = ConstantInt::get(ConstOp->getContext(), APInt(32, NearestLog));
  Instruction *ShiftInst = BinaryOperator::Create(isMul ? Instruction::Shl : Instruction::AShr, Instr.getOperand(isa<ConstantInt>(Instr.getOperand(0)) ? 1 : 0), ConstLog);
  
  if (isMul && Rest != 0) {
    Instruction *RestInst = BinaryOperator::Create(Rest.ugt(0) ? Instruction::Add : Instruction::Sub, ShiftInst, Instr.getOperand(isa<ConstantInt>(Instr.getOperand(0)) ? 1 : 0));

    ShiftInst->insertAfter(&Instr);

    RestInst->insertAfter(ShiftInst);

    Instr.replaceAllUsesWith(RestInst);
  } else {
    ShiftInst->insertAfter(&Instr);

    Instr.replaceAllUsesWith(ShiftInst);
  }

  return true;
}

void multiInstructionOptimization(Instruction &Instr, std::vector<Instruction *> &toRemove) {
  if (!Instr.isBinaryOp() || !checkOperands(Instr))
    return;

  APInt InitConstant = dyn_cast<ConstantInt>(Instr.getOperand(isa<Constant>(Instr.getOperand(0)) ? 0 : 1))->getValue();

  // Ciclo tutti gli Usee di Instr
  for (auto Iter = Instr.use_begin(); Iter != Instr.use_end(); ++Iter) {
    Instruction *IterInstr = dyn_cast<Instruction>(Iter->getUser());

    if (!IterInstr->isBinaryOp() || !checkOperands(*IterInstr))
      continue;

    // Procedo solo se sono operazioni opposti
    if (
      (Instr.getOpcode() == Instruction::Add && IterInstr->getOpcode() == Instruction::Sub) ||
      (Instr.getOpcode() == Instruction::Sub && IterInstr->getOpcode() == Instruction::Add) ||
      (Instr.getOpcode() == Instruction::Mul && IterInstr->getOpcode() == Instruction::SDiv) ||
      (Instr.getOpcode() == Instruction::SDiv && IterInstr->getOpcode() == Instruction::Mul)
    ) {
      APInt IterConstant = dyn_cast<ConstantInt>(IterInstr->getOperand(isa<Constant>(Instr.getOperand(0)) ? 0 : 1))->getValue();

      // Procedo solo se hanno la stessa costante
      if (InitConstant == IterConstant) {
        IterInstr->replaceAllUsesWith(Instr.getOperand(isa<Constant>(Instr.getOperand(0)) ? 1 : 0));

        toRemove.push_back(IterInstr);
      }
    }
  }
}

bool runOnBasicBlock(BasicBlock &B) {
  std::vector<Instruction *> toRemove;

  for (Instruction &Instr : B) {
    if (
      algebraicIdentity(Instr) ||
      strengthReduction(Instr)
    ) {
      toRemove.push_back(&Instr);
    }

    multiInstructionOptimization(Instr, toRemove);
  }

  for (Instruction *Instr : toRemove) {
    Instr->eraseFromParent();
  }

  return true;
}

bool runOnFunction(Function &F) {
  bool Transformed = false;

  for (auto Iter = F.begin(); Iter != F.end(); ++Iter) {
    if (runOnBasicBlock(*Iter)) {
      Transformed = true;
    }
  }

  return Transformed;
}

PreservedAnalyses LocalOpts::run(Module &M, ModuleAnalysisManager &AM) {
  for (auto Fiter = M.begin(); Fiter != M.end(); ++Fiter)
    if (runOnFunction(*Fiter))
      return PreservedAnalyses::none();
  
  return PreservedAnalyses::all();
}
