/*
 * Author: doe300
 *
 * See the file "LICENSE" for the full license governing this code.
 */

#include "CodeGenerator.h"

#include "../../lib/vc4asm/src/Validator.h"
#include "../InstructionWalker.h"
#include "../Module.h"
#include "../Profiler.h"
#include "GraphColoring.h"
#include "KernelInfo.h"
#include "log.h"

#include <assert.h>
#include <climits>
#include <map>
#include <sstream>

using namespace vc4c;
using namespace vc4c::qpu_asm;
using namespace vc4c::intermediate;

CodeGenerator::CodeGenerator(const Module& module, const Configuration& config) : config(config), module(module) {}

static FastMap<const Local*, std::size_t> mapLabels(Method& method)
{
    logging::debug() << "-----" << logging::endl;
    FastMap<const Local*, std::size_t> labelsMap;
    labelsMap.reserve(method.size());
    // index is in bytes, so an increment of 1 instructions, increments by 8 bytes
    std::size_t index = 0;
    auto it = method.walkAllInstructions();
    while(!it.isEndOfMethod())
    {
        BranchLabel* label = it.isEndOfBlock() ? nullptr : it.get<BranchLabel>();
        if(label != nullptr)
        {
            logging::debug() << "Mapping label '" << label->getLabel()->name << "' to byte-position " << index
                             << logging::endl;
            labelsMap[label->getLabel()] = index;

            it.nextInMethod();
        }
        else if(!it.isEndOfBlock() && it.has() && !it->mapsToASMInstruction())
        {
            // an instruction which has no equivalent in machine code -> drop
            logging::debug() << "Dropping instruction not mapped to assembler: " << it->to_string() << logging::endl;
            it.erase();
        }
        else
        {
            index += 8;
            it.nextInMethod();
        }
        if(it.isEndOfBlock() && !it.isEndOfMethod())
            // this handles empty basic blocks, so the index is not incremented in the next iteration
            it.nextInMethod();
    }
    logging::debug() << "Mapped " << labelsMap.size() << " labels to positions" << logging::endl;

    return labelsMap;
}

const FastModificationList<std::unique_ptr<qpu_asm::Instruction>>& CodeGenerator::generateInstructions(Method& method)
{
    PROFILE_COUNTER(vc4c::profiler::COUNTER_BACKEND + 0, "CodeGeneration (before)", method.countInstructions());
#ifdef MULTI_THREADED
    instructionsLock.lock();
#endif
    auto& generatedInstructions = allInstructions[&method];
#ifdef MULTI_THREADED
    instructionsLock.unlock();
#endif

    // check and fix possible errors with register-association
    PROFILE_START(initializeLocalsUses);
    GraphColoring coloring(method, method.walkAllInstructions());
    PROFILE_END(initializeLocalsUses);
    PROFILE_START(colorGraph);
    std::size_t round = 0;
    while(round < config.additionalOptions.registerResolverMaxRounds && !coloring.colorGraph())
    {
        if(coloring.fixErrors())
            break;
        ++round;
    }
    if(round >= config.additionalOptions.registerResolverMaxRounds)
    {
        logging::warn() << "Register conflict resolver has exceeded its maximum rounds, there might still be errors!"
                        << logging::endl;
    }
    PROFILE_END(colorGraph);

    // create label-map + remove labels
    const auto labelMap = mapLabels(method);

    // IMPORTANT: DO NOT OPTIMIZE, RE-ORDER, COMBINE, INSERT OR REMOVE ANY INSTRUCTION AFTER THIS POINT!!!
    // otherwise, labels/branches will be wrong

    // map to registers
    PROFILE_START(toRegisterMap);
    PROFILE_START(toRegisterMapGraph);
    auto registerMapping = coloring.toRegisterMap();
    PROFILE_END(toRegisterMapGraph);
    PROFILE_END(toRegisterMap);

    logging::debug() << "-----" << logging::endl;
    std::size_t index = 0;

    std::string s = "kernel " + method.name;

    for(const auto& bb : method)
    {
        if(bb.empty())
        {
            // show label comment for empty block with label comment for next block
            s = s.empty() ? bb.getLabel()->to_string() : s + "," + bb.getLabel()->to_string();
            continue;
        }

        auto it = bb.begin();
        auto label = dynamic_cast<const intermediate::BranchLabel*>(it->get());
        assert(label != nullptr);
        ++it;

        auto instr = it->get();
        if(instr->mapsToASMInstruction())
        {
            Instruction* mapped = instr->convertToAsm(registerMapping, labelMap, index);
            if(mapped != nullptr)
            {
                mapped->previousComment += s.empty() ? label->to_string() : s + ", " + label->to_string();
                s = "";
                generatedInstructions.emplace_back(mapped);
            }
            ++index;
        }
        ++it;

        while(it != bb.end())
        {
            auto instr = it->get();
            if(instr->mapsToASMInstruction())
            {
                Instruction* mapped = instr->convertToAsm(registerMapping, labelMap, index);
                if(mapped != nullptr)
                {
                    generatedInstructions.emplace_back(mapped);
                }
                ++index;
            }
            ++it;
        }
    }

    logging::debug() << "-----" << logging::endl;
    index = 0;
    for(const auto& instr : generatedInstructions)
    {
        CPPLOG_LAZY(
            logging::Level::DEBUG, log << std::hex << index << " " << instr->toHexString(true) << logging::endl);
        index += 8;
    }
    logging::debug() << "Generated " << std::dec << generatedInstructions.size() << " instructions!" << logging::endl;

    PROFILE_COUNTER_WITH_PREV(vc4c::profiler::COUNTER_BACKEND + 1000, "CodeGeneration (after)",
        generatedInstructions.size(), vc4c::profiler::COUNTER_BACKEND + 0);
    return generatedInstructions;
}

std::size_t CodeGenerator::writeOutput(std::ostream& stream)
{
    ModuleInfo moduleInfo;

    std::size_t maxStackSize = 0;
    for(const auto& m : module)
        maxStackSize = std::max(maxStackSize, m->calculateStackSize() * m->metaData.getWorkGroupSize());
    if(maxStackSize / sizeof(uint64_t) > std::numeric_limits<uint16_t>::max() || maxStackSize % sizeof(uint64_t) != 0)
        throw CompilationError(
            CompilationStep::CODE_GENERATION, "Stack-frame has unsupported size of", std::to_string(maxStackSize));
    moduleInfo.setStackFrameSize(Word(Byte(maxStackSize)));

    std::size_t numBytes = 0;
    // initial offset is zero
    std::size_t offset = 0;
    if(config.writeKernelInfo)
    {
        moduleInfo.kernelInfos.reserve(allInstructions.size());
        // generate kernel-infos
        for(const auto& pair : allInstructions)
        {
            moduleInfo.addKernelInfo(getKernelInfos(*pair.first, offset, pair.second.size()));
            offset += pair.second.size();
        }
        // add global offset (size of  header)
        std::ostringstream dummyStream;
        offset = moduleInfo.write(dummyStream, config.outputMode, module.globalData, Byte(maxStackSize));

        for(KernelInfo& info : moduleInfo.kernelInfos)
            info.setOffset(info.getOffset() + Word(offset));
    }
    // prepend module header to output
    // also write, if writeKernelInfo is not set, since global-data is written in here too
    logging::debug() << "Writing module header..." << logging::endl;
    numBytes += moduleInfo.write(stream, config.outputMode, module.globalData, Byte(maxStackSize)) * sizeof(uint64_t);

    for(const auto& pair : allInstructions)
    {
        switch(config.outputMode)
        {
        case OutputMode::ASSEMBLER:
            for(const auto& instr : pair.second)
            {
                stream << instr->toASMString() << std::endl;
                numBytes += 0; // doesn't matter here, since the number of bytes is unused for assembler output
            }
            break;
        case OutputMode::BINARY:
            for(const auto& instr : pair.second)
            {
                const uint64_t binary = instr->toBinaryCode();
                stream.write(reinterpret_cast<const char*>(&binary), 8);
                numBytes += 8;
            }
            break;
        case OutputMode::HEX:
            for(const auto& instr : pair.second)
            {
                stream << instr->toHexString(true) << std::endl;
                numBytes += 8; // doesn't matter here, since the number of bytes is unused for hexadecimal output
            }
        }
    }
    stream.flush();
    return numBytes;
}

// register/instruction mapping
void CodeGenerator::toMachineCode(Method& kernel)
{
    kernel.cleanLocals();
    const auto& instructions = generateInstructions(kernel);
#ifdef VERIFIER_HEADER
    std::vector<uint64_t> hexData;
    hexData.reserve(instructions.size());
    for(const auto& instr : instructions)
    {
        hexData.push_back(instr->toBinaryCode());
    }

    Validator v;
    v.OnMessage = [&instructions, this](const Message& msg) -> void {
        const auto& validatorMessage = dynamic_cast<const Validator::Message&>(msg);
        if(validatorMessage.Loc >= 0)
        {
            auto it = instructions.begin();
            std::advance(it, validatorMessage.Loc);
            logging::error() << "Validation-error '" << validatorMessage.Text << "' in: " << (*it)->toASMString()
                             << logging::endl;
            if(validatorMessage.RefLoc >= 0)
            {
                it = instructions.begin();
                std::advance(it, validatorMessage.RefLoc);
                logging::error() << "With reference to instruction: " << (*it)->toASMString() << logging::endl;
            }
        }
        if(config.stopWhenVerificationFailed)
            throw CompilationError(CompilationStep::VERIFIER, "vc4asm verification error", msg.toString());
    };
    v.Instructions = &hexData;
    logging::info() << "Validation-output: " << logging::endl;
    v.Validate();
    fflush(stderr);
#endif
}
