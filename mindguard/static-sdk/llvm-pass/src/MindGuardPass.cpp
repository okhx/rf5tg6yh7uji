#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/ErrorHandling.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

std::uint64_t hashName(llvm::StringRef name) {
  std::uint64_t value = 0xcbf29ce484222325ULL;
  for (const unsigned char byte : name.bytes()) {
    value = (value ^ byte) * 0x100000001b3ULL;
  }
  return value;
}

std::uint64_t mix(std::uint64_t value, std::uint64_t salt) {
  value ^= salt;
  value ^= value >> 30U;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27U;
  value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

bool shouldProtect(const llvm::Function& function) {
  const auto name = function.getName();
  return !function.isDeclaration() &&
         (name.contains("materialize_embedded_") || name.contains("materialize_generated") ||
          name.contains("decode_embedded_variant") || name.contains("decode_share") ||
          name.contains("with_generated_") || name.contains("parse_generated_scalar") ||
          name.contains("output_junk_round") || name.contains("validate_watermark"));
}

bool isHotByteLoop(const llvm::Function& function) {
  const auto name = function.getName();
  return name.contains("output_junk_round") || name.contains("decode_embedded_variant") ||
         name.contains("decode_share_fast");
}

class MindGuardCfgPass final : public llvm::PassInfoMixin<MindGuardCfgPass> {
 public:
  llvm::PreservedAnalyses run(llvm::Module& module, llvm::ModuleAnalysisManager&) {
    std::vector<llvm::Function*> targets;
    for (auto& function : module) {
      if (shouldProtect(function)) targets.push_back(&function);
    }
    if (targets.empty()) return llvm::PreservedAnalyses::all();
    const auto buildSeed = readBuildSeed(module);
    module.getOrInsertNamedMetadata("mindguard.mba")
        ->addOperand(llvm::MDNode::get(module.getContext(),
                                     llvm::MDString::get(module.getContext(), "seed-depth")));
    auto* opaqueThunk = createOpaqueThunk(module, buildSeed);
    bool changed = false;
    unsigned ordinal = 0;
    for (auto* function : targets) {
      const auto salt = mix(hashName(function->getName()), buildSeed);
      const bool hotLoop = isHotByteLoop(*function);
      applyMba(*function, salt);
      if (!hotLoop) injectOpaquePredicate(*function, *opaqueThunk);
      if (hotLoop) {
        changed = true;
        continue;
      }
      const auto guardValue = mix(buildSeed, salt);
      auto* guard = createGuard(module, guardValue, ordinal, "cfg");
      const auto thunkGuardValue = mix(guardValue, salt ^ 0xa27d4eb2f165903cULL);
      auto* thunkGuard = createGuard(module, thunkGuardValue, ordinal, "thunk");
      const auto thunkSalt = mix(salt, 0x79b6d4e2138fac05ULL);
      auto* thunk = createGuardThunk(module, *guard, guardValue, thunkSalt, ordinal);
      applyMba(*thunk, mix(salt, thunkSalt));
      protect(*function, *guard, mix(guardValue, thunkSalt), mix(salt, buildSeed), thunk);
      protect(*thunk, *thunkGuard, thunkGuardValue, mix(thunkSalt, buildSeed), nullptr);
      ++ordinal;
      changed = true;
    }
    const auto opaqueSalt = mix(buildSeed, 0x3c16f8d97a420be5ULL);
    applyMba(*opaqueThunk, opaqueSalt);
    auto* opaqueGuard = createGuard(module, mix(buildSeed, opaqueSalt), ordinal, "opaque");
    protect(*opaqueThunk, *opaqueGuard, mix(buildSeed, opaqueSalt), opaqueSalt, nullptr);
    return changed ? llvm::PreservedAnalyses::none() : llvm::PreservedAnalyses::all();
  }

 private:
  static llvm::Value* mbaAdd(llvm::IRBuilder<>& builder, llvm::Value* left,
                             llvm::Value* right, unsigned depth,
                             std::uint64_t salt) {
    if (depth == 0) return builder.CreateAdd(left, right, "mg.mba.add");
    llvm::Value* first;
    llvm::Value* second;
    if ((salt & 1U) == 0) {
      first = builder.CreateXor(left, right, "mg.mba.xor");
      second = builder.CreateShl(builder.CreateAnd(left, right, "mg.mba.and"), 1,
                                 "mg.mba.twice");
    } else {
      first = builder.CreateOr(left, right, "mg.mba.or");
      second = builder.CreateAnd(left, right, "mg.mba.and");
    }
    return mbaAdd(builder, first, second, depth - 1U, salt >> 1U);
  }

  static void applyMba(llvm::Function& function, std::uint64_t salt) {
    std::vector<llvm::BinaryOperator*> additions;
    const bool hotLoop = isHotByteLoop(function);
    if (hotLoop) return;
    const auto limit = static_cast<std::size_t>(6U + (salt & 7U));
    for (auto& block : function) {
      for (auto& instruction : block) {
        auto* binary = llvm::dyn_cast<llvm::BinaryOperator>(&instruction);
        if (binary != nullptr && binary->getOpcode() == llvm::Instruction::Add &&
            binary->getType()->isIntegerTy() && additions.size() < limit) {
          additions.push_back(binary);
        }
      }
    }
    const auto depth = static_cast<unsigned>(1U + ((salt >> 9U) & 1U));
    for (std::size_t index = 0; index < additions.size(); ++index) {
      auto* addition = additions[index];
      llvm::IRBuilder<> builder(addition);
      auto* replacement = mbaAdd(builder, addition->getOperand(0), addition->getOperand(1),
                                 depth, mix(salt, index + 1U));
      addition->replaceAllUsesWith(replacement);
      addition->eraseFromParent();
    }
  }

  static llvm::Function* createOpaqueThunk(llvm::Module& module, std::uint64_t seed) {
    auto* integer = llvm::Type::getInt64Ty(module.getContext());
    auto* type = llvm::FunctionType::get(integer, {integer}, false);
    auto* thunk = llvm::Function::Create(type, llvm::GlobalValue::InternalLinkage,
                                        "__mindguard_opaque_stack_thunk", module);
    thunk->addFnAttr(llvm::Attribute::NoInline);
    thunk->addFnAttr(llvm::Attribute::NoUnwind);
    auto* entry = llvm::BasicBlock::Create(module.getContext(), "entry", thunk);
    llvm::IRBuilder<> builder(entry);
    auto* value = thunk->getArg(0);
    const auto key = mix(seed, 0x6e9a2d510c47bf83ULL) | 1ULL;
    auto* combined = mbaAdd(builder, value, builder.getInt64(key),
                            2U, seed ^ key);
    builder.CreateRet(builder.CreateSub(combined, builder.getInt64(key), "mg.opaque.identity"));
    return thunk;
  }

  static void injectOpaquePredicate(llvm::Function& function, llvm::Function& thunk) {
    auto& entry = function.getEntryBlock();
    auto* split = &*entry.getFirstInsertionPt();
    auto* real = entry.splitBasicBlock(split, "mg.opaque.real");
    entry.getTerminator()->eraseFromParent();
    llvm::IRBuilder<> builder(&entry);
    auto* slot = builder.CreateAlloca(builder.getInt64Ty(), nullptr, "mg.stack.invariant");
    slot->setAlignment(llvm::Align(8));
    auto* address = builder.CreatePtrToInt(slot, builder.getInt64Ty(), "mg.stack.address");
    auto* stored = builder.CreateStore(address, slot);
    stored->setVolatile(true);
    auto* first = builder.CreateLoad(builder.getInt64Ty(), slot, "mg.stack.first");
    first->setVolatile(true);
    auto* second = builder.CreateLoad(builder.getInt64Ty(), slot, "mg.stack.second");
    second->setVolatile(true);
    auto* reconstructed = builder.CreateCall(&thunk, {first}, "mg.stack.reconstructed");
    auto* valid = builder.CreateICmpEQ(reconstructed, second, "mg.stack.valid");
    auto* failed = llvm::BasicBlock::Create(function.getContext(), "mg.opaque.fail", &function);
    llvm::IRBuilder<> failedBuilder(failed);
    auto abortFunction = function.getParent()->getOrInsertFunction(
        "__mindguard_fail", llvm::FunctionType::get(failedBuilder.getVoidTy(), false));
    failedBuilder.CreateCall(abortFunction);
    failedBuilder.CreateUnreachable();
    builder.CreateCondBr(valid, real, failed);
  }

  static std::uint64_t readBuildSeed(llvm::Module& module) {
    const auto* global = module.getGlobalVariable("__mindguard_obfuscation_seed", true);
    if (global == nullptr) {
      global = module.getGlobalVariable("__mindguard_tu_obfuscation_seed", true);
    }
    const auto* initializer = global == nullptr ? nullptr : global->getInitializer();
    const auto* array = llvm::dyn_cast_or_null<llvm::ConstantDataArray>(initializer);
    if (array == nullptr || array->getNumElements() != 4 || array->getElementByteSize() != 8) {
      llvm::report_fatal_error("MindGuardPass requires a 256-bit build seed global");
    }
    std::uint64_t seed = 0x43cf92a6d1750be8ULL;
    for (unsigned index = 0; index < 4; ++index) {
      seed = mix(seed, array->getElementAsInteger(index));
    }
    return seed;
  }

  static llvm::GlobalVariable* createGuard(llvm::Module& module, std::uint64_t value,
                                           unsigned ordinal, llvm::StringRef kind) {
    auto name = std::string("__mindguard_") + kind.str() + "_guard_" +
                std::to_string(ordinal);
    auto* guard = new llvm::GlobalVariable(
        module, llvm::Type::getInt64Ty(module.getContext()), true,
        llvm::GlobalValue::InternalLinkage,
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(module.getContext()), value),
        name);
    guard->setAlignment(llvm::Align(8));
    return guard;
  }

  static llvm::Value* emitMix(llvm::IRBuilder<>& builder, llvm::Value* value,
                              std::uint64_t salt) {
    auto* state = builder.CreateXor(value, builder.getInt64(salt));
    state = builder.CreateXor(state, builder.CreateLShr(state, 30));
    state = builder.CreateMul(state, builder.getInt64(0xbf58476d1ce4e5b9ULL));
    state = builder.CreateXor(state, builder.CreateLShr(state, 27));
    state = builder.CreateMul(state, builder.getInt64(0x94d049bb133111ebULL));
    return builder.CreateXor(state, builder.CreateLShr(state, 31), "mg.state");
  }

  static llvm::Function* createGuardThunk(llvm::Module& module, llvm::GlobalVariable& guard,
                                          std::uint64_t guardValue, std::uint64_t salt,
                                          unsigned ordinal) {
    auto* type = llvm::FunctionType::get(llvm::Type::getInt64Ty(module.getContext()), false);
    auto* thunk = llvm::Function::Create(type, llvm::GlobalValue::InternalLinkage,
                                        "__mindguard_guard_thunk_" + std::to_string(ordinal), module);
    thunk->addFnAttr(llvm::Attribute::NoInline);
    thunk->addFnAttr(llvm::Attribute::NoUnwind);
    auto* entry = llvm::BasicBlock::Create(module.getContext(), "entry", thunk);
    llvm::IRBuilder<> builder(entry);
    auto* loaded = builder.CreateLoad(builder.getInt64Ty(), &guard, "mg.guard.source");
    loaded->setVolatile(true);
    auto* valid = llvm::BasicBlock::Create(module.getContext(), "mg.guard.valid", thunk);
    auto* failed = llvm::BasicBlock::Create(module.getContext(), "mg.guard.fail", thunk);
    builder.CreateCondBr(builder.CreateICmpEQ(loaded, builder.getInt64(guardValue)), valid, failed);
    llvm::IRBuilder<> validBuilder(valid);
    validBuilder.CreateRet(emitMix(validBuilder, loaded, salt));
    llvm::IRBuilder<> failedBuilder(failed);
    auto abortFunction = module.getOrInsertFunction(
        "__mindguard_fail", llvm::FunctionType::get(failedBuilder.getVoidTy(), false));
    failedBuilder.CreateCall(abortFunction);
    failedBuilder.CreateUnreachable();
    return thunk;
  }

  static void protect(llvm::Function& function, llvm::GlobalVariable& guard,
                      std::uint64_t guardValue, std::uint64_t salt,
                      llvm::Function* guardThunk) {
    std::vector<llvm::BasicBlock*> blocks;
    const auto blockLimit = isHotByteLoop(function)
                                ? 0U
                                : static_cast<std::size_t>(3U + (salt & 3U));
    for (auto& block : function) {
      if (!block.isEHPad() && !block.empty() && blocks.size() < blockLimit) blocks.push_back(&block);
    }
    for (std::size_t index = 0; index < blocks.size(); ++index) {
      protectBlock(function, *blocks[index], guard, guardValue,
                   mix(salt, static_cast<std::uint64_t>(index + 1U)), index,
                   guardThunk);
    }
  }

  static void protectBlock(llvm::Function& function, llvm::BasicBlock& block,
                           llvm::GlobalVariable& guard, std::uint64_t guardValue,
                           std::uint64_t salt, std::size_t ordinal,
                           llvm::Function* guardThunk) {
    auto& context = function.getContext();
    auto* split = &*block.getFirstInsertionPt();
    auto* real = block.splitBasicBlock(split, "mg.real." + std::to_string(ordinal));
    block.getTerminator()->eraseFromParent();

    llvm::IRBuilder<> builder(&block);
    llvm::Value* loaded;
    if (guardThunk != nullptr) {
      loaded = builder.CreateCall(guardThunk, {}, "mg.guard.thunk");
    } else {
      auto* guardLoad = builder.CreateLoad(builder.getInt64Ty(), &guard, "mg.guard");
      guardLoad->setVolatile(true);
      loaded = guardLoad;
      auto* validated = llvm::BasicBlock::Create(context, "mg.guard.validated", &function);
      auto* guardFailed = llvm::BasicBlock::Create(context, "mg.guard.rejected", &function);
      builder.CreateCondBr(builder.CreateICmpEQ(loaded, builder.getInt64(guardValue)),
                           validated, guardFailed);
      llvm::IRBuilder<> guardFailedBuilder(guardFailed);
      auto abortFunction = function.getParent()->getOrInsertFunction(
          "__mindguard_fail", llvm::FunctionType::get(guardFailedBuilder.getVoidTy(), false));
      guardFailedBuilder.CreateCall(abortFunction);
      guardFailedBuilder.CreateUnreachable();
      builder.SetInsertPoint(validated);
    }
    auto* state = emitMix(builder, loaded, salt);

    auto abortFunction = function.getParent()->getOrInsertFunction(
        "__mindguard_fail", llvm::FunctionType::get(builder.getVoidTy(), false));
    auto* failed = llvm::BasicBlock::Create(context, "mg.fail", &function);
    llvm::IRBuilder<> failedBuilder(failed);
    failedBuilder.CreateCall(abortFunction);
    failedBuilder.CreateUnreachable();
    auto* failedAlternate = llvm::BasicBlock::Create(context, "mg.fail.alt", &function);
    llvm::IRBuilder<> failedAlternateBuilder(failedAlternate);
    failedAlternateBuilder.CreateCall(abortFunction);
    failedAlternateBuilder.CreateUnreachable();

    const auto tableSize = static_cast<unsigned>(4U << ((salt >> 11U) & 1U));
    const auto realIndex = static_cast<unsigned>((salt >> 17U) & (tableSize - 1U));
    std::vector<llvm::BasicBlock*> destinations(tableSize);
    destinations[realIndex] = real;
    for (unsigned index = 0; index < tableSize; ++index) {
      if (index == realIndex) continue;
      auto* decoy = llvm::BasicBlock::Create(context, "mg.decoy", &function);
      llvm::IRBuilder<> decoyBuilder(decoy);
      auto* noise = decoyBuilder.CreateXor(state, decoyBuilder.getInt64(salt + index));
      auto* impossible = decoyBuilder.CreateICmpEQ(noise, decoyBuilder.getInt64(guardValue));
      decoyBuilder.CreateCondBr(impossible, failedAlternate, failed);
      destinations[index] = decoy;
    }
    std::vector<llvm::Constant*> addresses;
    addresses.reserve(tableSize);
    for (auto* destination : destinations) {
      addresses.push_back(llvm::BlockAddress::get(&function, destination));
    }
    auto* tableType = llvm::ArrayType::get(builder.getPtrTy(), tableSize);
    auto* table = new llvm::GlobalVariable(
        *function.getParent(), tableType, true, llvm::GlobalValue::InternalLinkage,
        llvm::ConstantArray::get(tableType, addresses), "__mindguard_threaded_targets");
    table->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    auto* index = builder.CreateAnd(
        builder.CreateXor(state, builder.getInt64(mix(guardValue, salt) ^ realIndex)),
        builder.getInt64(tableSize - 1U), "mg.thread.index");
    auto* slot = builder.CreateInBoundsGEP(
        tableType, table, {builder.getInt64(0), index}, "mg.thread.slot");
    auto* target = builder.CreateLoad(builder.getPtrTy(), slot, "mg.thread.target");
    auto* dispatch = builder.CreateIndirectBr(target, tableSize);
    for (auto* destination : destinations) dispatch->addDestination(destination);
  }
};

}  // namespace

extern "C" LLVM_ATTRIBUTE_WEAK LLVM_ATTRIBUTE_VISIBILITY_DEFAULT
llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "MindGuardPass", "0.1.0",
          [](llvm::PassBuilder& builder) {
            builder.registerOptimizerLastEPCallback(
                [](llvm::ModulePassManager& manager, llvm::OptimizationLevel) {
                  manager.addPass(MindGuardCfgPass{});
                });
          }};
}
